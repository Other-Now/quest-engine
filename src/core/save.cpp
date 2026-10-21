#include "core/save.hpp"

#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace qe {

using json = nlohmann::json;

json saveGame(const Game& g) {
    json vars = json::object();
    for (int i = 0; i < int(g.store().size()); ++i) vars[g.store().name(i)] = g.store().get(i);
    return {
        {"format", kSaveFormat},
        {"content_rev", g.content().revision},
        {"meta",
         {{"title", g.content().title},
          {"room", g.room().id},
          {"play_time_s", double(g.tickCount()) / kTicksPerSecond}}},
        {"player",
         {{"room", g.room().id}, {"x", g.playerPos().x}, {"y", g.playerPos().y}, {"facing", int(g.facing())}}},
        {"tick", g.tickCount()},
        {"vars", vars},
    };
}

namespace {
// v1 -> v2: group the player fields; derive the metadata v1 never stored.
void migrate1to2(json& doc) {
    json player = json::object();
    for (const char* k : {"room", "x", "y", "facing"}) {
        if (doc.contains(k)) player[k] = doc[k];
        doc.erase(k);
    }
    doc["player"] = player;
    doc["meta"] = {{"title", ""},
                   {"room", player.value("room", "")},
                   {"play_time_s", double(doc.value("tick", 0u)) / kTicksPerSecond}};
    doc["format"] = 2;
}
}  // namespace

void migrateSave(json& doc) {
    int v = doc.value("format", 0);
    if (v < 1 || v > kSaveFormat) throw std::runtime_error("unsupported save format " + std::to_string(v));
    // One step per version, so a save from any old version has a path forward.
    if (v == 1) migrate1to2(doc), v = 2;
}

std::string describeSave(const json& doc) {
    json d = doc;
    migrateSave(d);
    int secs = int(d["meta"].value("play_time_s", 0.0));
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d:%02d played", secs / 60, secs % 60);
    return d["meta"].value("room", "?") + ", " + buf;
}

void loadGame(Game& g, json doc) {
    migrateSave(doc);
    const Content& c = g.content();
    const json& player = doc.at("player");
    int room = c.roomIndex(player.at("room"));
    if (room < 0) throw std::runtime_error("save refers to unknown room");

    // Content renames, oldest revision first.
    json vars = doc.at("vars");
    int rev = doc.value("content_rev", 1);
    for (auto& [r, renames] : c.renames) {
        if (r <= rev) continue;
        for (auto& [from, to] : renames)
            if (vars.contains(from)) {
                vars[to] = vars[from];
                vars.erase(from);
            }
    }
    Store s = c.vars;
    s.resetToDefaults();
    for (auto& [name, value] : vars.items()) {
        int id = s.id(name);
        if (id >= 0) s.set(id, value.get<int>());  // removed variables are dropped
    }
    g.restore(room, {player.at("x").get<float>(), player.at("y").get<float>()}, Facing(player.value("facing", 1)),
              s.values(), doc.value("tick", 0u));
}

void saveGameFile(const Game& g, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path);
    f << saveGame(g).dump(2) << "\n";
}

void loadGameFile(Game& g, const std::string& path) { loadGame(g, json::parse(readFile(path))); }

}  // namespace qe
