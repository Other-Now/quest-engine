#include <stdexcept>

#include "check.hpp"
#include "core/content.hpp"
#include "core/store.hpp"

using namespace qe;

TEST("store: define, get, set, defaults") {
    Store s;
    int a = s.define("gate_open", 0);
    int b = s.define("key_in_well", 1);
    CHECK(s.define("gate_open", 7) == a);  // redefinition keeps the first
    CHECK(s.get(a) == 0 && s.get(b) == 1);
    s.set("gate_open", 1);
    CHECK(s.get("gate_open") == 1);
    CHECK(s.id("nope") == -1);
    bool threw = false;
    try {
        s.get("nope");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    s.resetToDefaults();
    CHECK(s.get(a) == 0 && s.get(b) == 1);
}

TEST("store: hash covers every value and copies are independent") {
    Store s;
    s.define("x", 0);
    s.define("y", 0);
    uint64_t h0 = s.hash();
    Store t = s;
    t.set("y", 1);
    CHECK(s.hash() == h0);
    CHECK(t.hash() != h0);
    t.set("y", 0);
    CHECK(t.hash() == h0);
}

TEST("store: conditions parse names and values") {
    Store s;
    s.define("a", 1);
    s.define("b", 0);
    Condition c = parseCondition("a=1,b=0", s);
    CHECK(c.holds(s));
    s.set("b", 2);
    CHECK(!c.holds(s));
    CHECK(parseCondition("", s).holds(s));
    CHECK(parseCondition("a", s).holds(s));  // bare name means =1
    bool threw = false;
    try {
        parseCondition("missing=1", s);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}
