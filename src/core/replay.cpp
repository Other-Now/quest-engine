#include "core/replay.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>

namespace qe {

std::string hex64(uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

uint64_t parseHex64(const std::string& s) { return std::stoull(s, nullptr, 16); }

void writeRecording(const Recording& r, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path);
    f << "QRP 1\ncontent " << hex64(r.contentHash) << "\n";
    for (auto& e : r.events)
        f << (e.ev.type == InputEvent::Click ? "click " : "rclick ") << e.tick << ' ' << e.ev.x << ' ' << e.ev.y
          << "\n";
    f << "end " << r.endTick << "\n";
    if (r.finalHash) f << "final " << hex64(r.finalHash) << "\n";
}

Recording readRecording(const std::string& path) {
    std::istringstream in(readFile(path));
    Recording r;
    std::string word;
    in >> word;
    int version = 0;
    in >> version;
    if (word != "QRP" || version != 1) throw std::runtime_error(path + ": not a QRP 1 recording");
    while (in >> word) {
        if (word == "content") {
            in >> word;
            r.contentHash = parseHex64(word);
        } else if (word == "click" || word == "rclick") {
            TimedEvent e;
            e.ev.type = word == "click" ? InputEvent::Click : InputEvent::RightClick;
            in >> e.tick >> e.ev.x >> e.ev.y;
            r.events.push_back(e);
        } else if (word == "end") {
            in >> r.endTick;
        } else if (word == "final") {
            in >> word;
            r.finalHash = parseHex64(word);
        } else {
            throw std::runtime_error(path + ": unexpected '" + word + "'");
        }
    }
    return r;
}

uint64_t playRecording(Game& g, const Recording& r) {
    g.newGame();
    size_t next = 0;
    std::vector<InputEvent> evs;
    for (uint32_t t = 0; t < r.endTick; ++t) {
        evs.clear();
        while (next < r.events.size() && r.events[next].tick == t) evs.push_back(r.events[next++].ev);
        g.tick(evs);
    }
    return g.stateHash();
}

// ---- walkthrough bot -------------------------------------------------------

std::vector<std::string> loadScript(const std::string& path) {
    std::istringstream in(readFile(path));
    std::vector<std::string> out;
    std::string line;
    while (std::getline(in, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        size_t start = line.find_first_not_of(' ');
        if (start != std::string::npos) out.push_back(line.substr(start));
    }
    return out;
}

namespace {

constexpr int kReadTicks = 24;  // the bot "reads" each line this long, then clicks on

InputEvent clickAt(Vec2 p) {
    InputEvent e;
    e.x = int16_t(p.x + 0.5f);
    e.y = int16_t(p.y + 0.5f);
    return e;
}

struct Bot {
    Game& g;
    uint32_t maxTicks;
    Recording rec;
    int sinceMessage = 0;

    void step(std::vector<InputEvent> evs = {}) {
        if (g.tickCount() >= maxTicks) throw std::runtime_error("walkthrough ran out of ticks");
        for (auto& e : evs) rec.events.push_back({g.tickCount(), e});
        g.tick(evs);
    }
    // Tick until pred() holds, clicking through any lines on screen.
    void waitUntil(const std::function<bool()>& pred) {
        while (!pred()) {
            if (g.message() && !g.showingChoices() && ++sinceMessage >= kReadTicks) {
                sinceMessage = 0;
                step({clickAt({160, float(ui::kSentenceY)})});
            } else {
                step();
            }
        }
    }
    void waitIdle() {
        waitUntil([&] { return g.idle() || g.mode() == Mode::Credits; });
    }
    void click(Vec2 p) { step({clickAt(p)}); }

    int itemSlot(const std::string& id) {
        int item = g.content().itemIndex(id);
        auto held = g.heldItems();
        auto it = std::find(held.begin(), held.end(), item);
        if (item < 0 || it == held.end()) throw std::runtime_error("item not in inventory: " + id);
        return int(it - held.begin());
    }

    // Click a hotspot, first walking it on-screen if the camera can't see it.
    void clickHotspot(const std::string& id, Verb verb, int itemSlotIdx) {
        int h = g.room().hotspotIndex(id);
        if (h < 0) throw std::runtime_error("no hotspot '" + id + "' in room " + g.room().id);
        const Hotspot& hs = g.room().hotspots[size_t(h)];
        if (!g.hotspotVisible(hs)) throw std::runtime_error("hotspot not visible: " + id);
        for (int attempt = 0; attempt < 8; ++attempt) {
            Vec2 c = hs.rect.center();
            float sx = c.x - g.cameraX();
            if (sx >= 2 && sx < kScreenW - 2 && g.hotspotAt(int(sx + 0.5f), int(c.y + 0.5f)) == h) {
                if (verb != Verb::Walk) click(ui::verbButton(verb).center());
                if (itemSlotIdx >= 0) click(ui::inventorySlot(itemSlotIdx).center());
                click({sx, c.y});
                return;
            }
            // Walk toward it along any free bit of floor at the screen edge.
            float tx = std::min(std::max(hs.walkTo.x - g.cameraX(), 8.0f), float(kScreenW - 8));
            bool clicked = false;
            for (float y = float(kSceneH - 4); y > 16 && !clicked; y -= 4) {
                Vec2 world{tx + g.cameraX(), y};
                if (g.hotspotAt(int(tx + 0.5f), int(y + 0.5f)) < 0 && g.room().nav.inside(world)) {
                    click({tx, y});
                    clicked = true;
                }
            }
            if (!clicked) break;
            waitIdle();
        }
        throw std::runtime_error("could not get hotspot on screen: " + id);
    }

    void run(const std::string& line) {
        std::istringstream in(line);
        std::vector<std::string> w;
        for (std::string s; in >> s;) w.push_back(s);
        if (w.empty()) return;
        Verb verb;
        if (w[0] == "choose" && w.size() == 2) {
            waitUntil([&] { return g.showingChoices(); });
            int n = std::stoi(w[1]);
            if (n < 1 || n > int(g.choices().size())) throw std::runtime_error("no choice " + w[1]);
            click(ui::choiceRow(n - 1).center());
            return;
        }
        if (w[0] == "wait" && w.size() == 2) {
            for (int i = std::stoi(w[1]); i > 0; --i) step();
            return;
        }
        waitIdle();
        if (g.mode() == Mode::Credits) throw std::runtime_error("game already over at: " + line);
        if (w[0] == "combine" && w.size() == 3) {
            click(ui::verbButton(Verb::Use).center());
            click(ui::inventorySlot(itemSlot(w[1])).center());
            click(ui::inventorySlot(itemSlot(w[2])).center());
            return;
        }
        if (!parseVerb(w[0], verb)) throw std::runtime_error("unknown command: " + line);
        if (w.size() == 2) {
            if (g.room().hotspotIndex(w[1]) < 0 && g.content().itemIndex(w[1]) >= 0) {
                click(ui::verbButton(verb).center());  // look/use an inventory item
                click(ui::inventorySlot(itemSlot(w[1])).center());
                return;
            }
            clickHotspot(w[1], verb, -1);
        } else if (w.size() == 3 && verb == Verb::Use) {
            clickHotspot(w[2], Verb::Use, itemSlot(w[1]));
        } else {
            throw std::runtime_error("bad command: " + line);
        }
    }
};

}  // namespace

BotResult runWalkthrough(Game& g, const std::vector<std::string>& script, uint32_t maxTicks,
                         bool fromCurrentState) {
    if (!fromCurrentState) g.newGame();
    Bot bot{g, maxTicks, {}};
    bot.rec.contentHash = g.content().hash;
    BotResult res;
    size_t lineNo = 0;
    try {
        for (auto& line : script) {
            ++lineNo;
            bot.run(line);
        }
        bot.waitIdle();
        bot.step();  // settle: one tick after the last action
        res.ok = true;
    } catch (const std::exception& e) {
        res.error = "line " + std::to_string(lineNo) + ": " + e.what();
    }
    bot.rec.endTick = g.tickCount();
    bot.rec.finalHash = g.stateHash();
    res.recording = bot.rec;
    return res;
}

}  // namespace qe
