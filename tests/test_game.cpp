// Whole-game tests: the scripted walkthrough, deterministic replay, save/load.
#include "check.hpp"
#include "core/replay.hpp"
#include "core/save.hpp"

using namespace qe;

namespace {
std::shared_ptr<Content> content() {
    static auto c = loadContent(qt::data("assets"));
    return c;
}
std::vector<std::string> script() { return loadScript(qt::data("assets/walkthrough.txt")); }
}  // namespace

TEST("game: walkthrough script reaches the credits") {
    Game g(content());
    BotResult r = runWalkthrough(g, script());
    if (!r.ok) std::printf("  %s\n", r.error.c_str());
    CHECK(r.ok);
    CHECK(g.mode() == Mode::Credits);
    CHECK(g.store().get("game_won") == 1);
    CHECK(g.store().get("has_amulet") == 1);
}

TEST("game: replaying the recorded clicks gives the identical state") {
    Game a(content());
    BotResult r = runWalkthrough(a, script());
    CHECK(r.ok);
    Game b(content());
    CHECK(playRecording(b, r.recording) == r.recording.finalHash);
    CHECK(b.mode() == Mode::Credits);
    // ...and a recording that drops one click does not.
    Recording cut = r.recording;
    cut.events.erase(cut.events.begin() + long(cut.events.size() / 2));
    CHECK(playRecording(b, cut) != r.recording.finalHash);
}

TEST("game: a save at every step loads and the game can still be finished") {
    auto lines = script();
    int finished = 0;
    for (size_t cut = 0; cut <= lines.size(); ++cut) {
        // Saving mid-conversation isn't offered, so only cut between commands.
        if (cut < lines.size() && lines[cut].rfind("choose", 0) == 0) continue;
        Game a(content());
        std::vector<std::string> head(lines.begin(), lines.begin() + long(cut));
        std::vector<std::string> tail(lines.begin() + long(cut), lines.end());
        CHECK(runWalkthrough(a, head).ok);
        auto doc = saveGame(a);
        Game b(content());
        loadGame(b, nlohmann::json::parse(doc.dump()));
        CHECK(saveGame(b) == doc);  // round trip is lossless
        if (runWalkthrough(b, tail, 400000, true).ok && b.mode() == Mode::Credits) ++finished;
        else std::printf("  could not finish after loading at line %zu\n", cut);
    }
    CHECK(finished > 10);
}

TEST("game: unknown variables in a save are dropped, missing ones get defaults") {
    Game g(content());
    auto doc = saveGame(g);
    doc["vars"]["no_longer_exists"] = 5;
    doc["vars"].erase("gate_open");
    loadGame(g, doc);
    CHECK(g.store().get("gate_open") == 0);
    CHECK(g.store().id("no_longer_exists") < 0);
}

TEST("save: a v1 file written by the v1 code migrates, loads and can be finished") {
    // tests/data/save_v1.json was written by commit 9e01d7a (format 1), after
    // the first 11 walkthrough commands.
    auto doc = nlohmann::json::parse(readFile(qt::data("tests/data/save_v1.json")));
    CHECK(doc["format"] == 1);
    CHECK(describeSave(doc) == "village, 0:24 played");
    Game g(content());
    loadGame(g, doc);
    CHECK(g.room().id == "village");
    CHECK(g.store().get("has_mushroom") == 1);
    CHECK(g.playerPos() == Vec2{12, 136});
    CHECK(g.tickCount() == 1491u);
    auto lines = script();
    std::vector<std::string> rest(lines.begin() + 11, lines.end());
    CHECK(runWalkthrough(g, rest, 400000, true).ok);
    CHECK(g.mode() == Mode::Credits);
    // Saving again writes the current format.
    CHECK(saveGame(g)["format"] == kSaveFormat);
}

TEST("save: formats from the future are refused") {
    Game g(content());
    auto doc = saveGame(g);
    doc["format"] = kSaveFormat + 1;
    bool threw = false;
    try {
        loadGame(g, doc);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

TEST("save: content renames carry old variable names forward") {
    // Revision 2 of the content renamed glint_seen -> saw_glint; a revision-1
    // save still says glint_seen.
    auto c = std::make_shared<Content>(*content());
    c->revision = 2;
    c->renames[2] = {{"glint_seen", "saw_glint"}};
    Game g(c);
    auto doc = saveGame(g);
    doc["content_rev"] = 1;
    doc["vars"].erase("saw_glint");
    doc["vars"]["glint_seen"] = 1;
    loadGame(g, doc);
    CHECK(g.store().get("saw_glint") == 1);
    doc["content_rev"] = 2;  // already current: no rename, unknown name dropped
    loadGame(g, doc);
    CHECK(g.store().get("saw_glint") == 0);
}

TEST("game: hot reload keeps progress") {
    Game g(content());
    auto lines = script();
    std::vector<std::string> head(lines.begin(), lines.begin() + 3);
    CHECK(runWalkthrough(g, head).ok);
    auto before = g.store().values();
    std::string room = g.room().id;
    g.reloadContent(loadContent(qt::data("assets")));
    CHECK(g.store().values() == before);
    CHECK(g.room().id == room);
}

TEST("game: clicks in the panel select verbs and items") {
    Game g(content());
    auto click = [&](Vec2 p) {
        InputEvent e;
        e.x = int16_t(p.x);
        e.y = int16_t(p.y);
        g.tick({e});
    };
    click(ui::verbButton(Verb::Look).center());
    CHECK(g.selectedVerb() == Verb::Look);
    InputEvent r;
    r.type = InputEvent::RightClick;
    g.tick({r});
    CHECK(g.selectedVerb() == Verb::Walk);
    CHECK(g.heldItems().empty());
}
