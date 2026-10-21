// Deterministic input replay and the walkthrough bot.
//
// A recording is just the clicks and the tick each one arrived on. Because the
// sim is a fixed-step function of (state, clicks), replaying the clicks from
// a new game reproduces the run exactly; the final state hash proves it.
#pragma once
#include <string>
#include <vector>

#include "core/game.hpp"

namespace qe {

struct TimedEvent {
    uint32_t tick = 0;
    InputEvent ev;
};

struct Recording {
    uint64_t contentHash = 0;
    std::vector<TimedEvent> events;
    uint32_t endTick = 0;
    uint64_t finalHash = 0;  // 0 = not recorded
};

void writeRecording(const Recording& r, const std::string& path);
Recording readRecording(const std::string& path);

// newGame(), then feed the events tick by tick up to endTick. Returns the
// final state hash.
uint64_t playRecording(Game& g, const Recording& r);

// The walkthrough bot: turns script lines ("use rope_bucket well",
// "talk tobin", "choose 2") into the clicks a player would make, plays them,
// and returns them as a recording. With fromCurrentState the bot carries on
// from wherever the game is (e.g. after loading a save).
struct BotResult {
    bool ok = false;
    std::string error;
    Recording recording;
};
std::vector<std::string> loadScript(const std::string& path);
BotResult runWalkthrough(Game& g, const std::vector<std::string>& script, uint32_t maxTicks = 400000,
                         bool fromCurrentState = false);

std::string hex64(uint64_t v);
uint64_t parseHex64(const std::string& s);

}  // namespace qe
