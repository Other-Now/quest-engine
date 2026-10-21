#include "check.hpp"
#include "core/replay.hpp"
#include "core/solver.hpp"

using namespace qe;

namespace {
std::shared_ptr<Content> content() {
    static auto c = loadContent(qt::data("assets"));
    return c;
}
const DialogueLibrary& lib() {
    static DialogueLibrary l(qt::data("assets/") + content()->inkJson);
    return l;
}
}  // namespace

TEST("solver: the shipped game is completable with no dead ends") {
    SolveReport r = solve(*content(), lib());
    CHECK(r.winnable);
    CHECK(r.deadEndStates == 0);
    CHECK(!r.shortestWin.empty());
    CHECK(lintContent(*content(), lib()).empty());
}

TEST("solver: its shortest solution actually plays through in the engine") {
    SolveReport r = solve(*content(), lib());
    Game g(content());
    BotResult b = runWalkthrough(g, r.shortestWin);
    if (!b.ok) std::printf("  %s\n", b.error.c_str());
    CHECK(b.ok && g.mode() == Mode::Credits);
}

TEST("solver: finds a planted dead end and the move that causes it") {
    // A plausible designer addition: feed the mushroom to the squirrels.
    // The mushroom is the only way to get the axe, so this strands the player.
    Content c = *content();
    Rule r;
    r.room = c.roomIndex("orchard");
    r.hotspot = "trees";
    r.verb = Verb::Use;
    r.item = c.itemIndex("mushroom");
    Effect take;
    take.kind = Effect::Set;
    take.var = c.items[size_t(r.item)].hasVar;
    take.value = 0;
    r.effects = {take};
    c.rules.insert(c.rules.begin(), r);
    SolveReport rep = solve(c, lib());
    CHECK(rep.winnable);
    CHECK(rep.deadEndStates > 0);
    bool named = false;
    for (auto& d : rep.deadEnds) named |= d.lastMove == "use mushroom trees";
    CHECK(named);
}

TEST("solver: an unwinnable game is reported as such") {
    Content c = *content();
    c.combos.clear();  // no way to make the bucket on a rope
    SolveReport rep = solve(c, lib());
    CHECK(!rep.winnable);
    CHECK(rep.shortestWin.empty());
}

TEST("lint: catches a hotspot you cannot walk to") {
    Content c = *content();
    c.rooms[0].hotspots[0].walkTo = {-50, -50};
    CHECK(!lintContent(c, lib()).empty());
}
