// Checks that inkcpp handles each Ink feature the game's dialogue relies on
// (tests/data/features.ink), and the store <-> Ink variable bridge.
#include <stdexcept>

#include "check.hpp"
#include "core/dialogue.hpp"

using namespace qe;

namespace {
Store guardStore(int coin) {
    Store s;
    s.define("has_coin", coin);
    s.define("door_open", 0);
    s.define("only_in_cpp", 3);
    return s;
}
const DialogueLibrary& lib() {
    static DialogueLibrary l(qt::data("tests/data/features.ink.json"));
    return l;
}
std::vector<DialogueLine> drain(Conversation& c) {
    std::vector<DialogueLine> out;
    DialogueLine l;
    while (c.nextLine(l)) out.push_back(l);
    return out;
}
}  // namespace

TEST("ink: conditional text, tags and conditional choices") {
    Store s = guardStore(0);
    Conversation c(lib(), "greet", s);
    auto lines = drain(c);
    CHECK(lines.size() == 1);
    CHECK(lines.size() == 1 && lines[0].text == "Halt." && lines[0].speaker == "guard");
    CHECK(c.choices().size() == 2);  // no coin: the bribe choice is hidden
    CHECK(c.syncedVars().size() == 2);  // only_in_cpp is not declared in Ink
}

TEST("ink: variables written by Ink come back to the store") {
    Store s = guardStore(1);
    Conversation c(lib(), "greet", s);
    drain(c);
    CHECK(c.choices().size() == 3);
    c.choose(1);  // Offer a coin. -> stitch "bribe"
    auto lines = drain(c);
    CHECK(!lines.empty() && lines[0].text == "Fine, go on through.");
    CHECK(s.get("has_coin") == 0);
    CHECK(s.get("door_open") == 1);
    CHECK(s.get("only_in_cpp") == 3);
    // Back at the labelled gather: once-only choice taken, sticky one stays.
    CHECK(c.choices().size() == 2);
    CHECK(c.choices().size() == 2 && c.choices()[0] == "Ask about the door." && c.choices()[1] == "Leave.");
}

TEST("ink: once-only choices disappear, sticky ones stay, END finishes") {
    Store s = guardStore(0);
    Conversation c(lib(), "greet", s);
    drain(c);
    c.choose(0);
    drain(c);
    CHECK(c.choices().size() == 1 && c.choices()[0] == "Leave.");
    c.choose(0);
    auto lines = drain(c);
    CHECK(!lines.empty() && lines.back().speaker == "me");
    CHECK(c.finished());
}

TEST("ink: store values are pushed in at the start") {
    Store s = guardStore(0);
    s.set("door_open", 1);
    Conversation c(lib(), "greet", s);
    auto lines = drain(c);
    CHECK(!lines.empty() && lines[0].text == "The door is open.");
}

TEST("ink: explorer finds every distinct outcome") {
    auto none = exploreConversation(lib(), "greet", guardStore(0));
    CHECK(none.size() == 1);  // nothing changes without a coin
    auto coin = exploreConversation(lib(), "greet", guardStore(1));
    CHECK(coin.size() == 2);  // leave with the coin, or bribe and leave
    // Each outcome's choice path, replayed, really leads to that outcome.
    int bribes = 0;
    for (auto& o : coin) {
        Store s = guardStore(1);
        Conversation c(lib(), "greet", s);
        drain(c);
        for (int choice : o.choicePath) {
            c.choose(size_t(choice - 1));
            drain(c);
        }
        CHECK(c.finished());
        CHECK(s.values() == o.vars);
        bribes += o.vars[1] == 1;
    }
    CHECK(bribes == 1);
}

TEST("ink: unknown knot is an error") {
    Store s = guardStore(0);
    bool threw = false;
    try {
        Conversation c(lib(), "no_such_knot", s);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(lib().hasKnot("greet"));
    CHECK(!lib().hasKnot("nope"));
}
