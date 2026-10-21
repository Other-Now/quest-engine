// Puzzle solver: proves the game can be finished and finds dead ends.
//
// A solver state is (room, every store variable). Moves are exactly what the
// engine allows: any rule that fires on a visible hotspot of the current room
// (with no item, or with any held item), any item combination, and every
// branch of any conversation the rules start (explored with the real Ink
// runtime). Walking between hotspots is assumed possible; lintContent() checks
// that separately with the real pathfinder.
//
// A dead end is a reachable state from which no sequence of moves reaches a
// winning state. Those are found by searching backwards from the wins.
#pragma once
#include <string>
#include <vector>

#include "core/content.hpp"
#include "core/dialogue.hpp"

namespace qe {

struct SolveReport {
    size_t states = 0, transitions = 0;
    size_t winStates = 0;
    bool winnable = false;
    std::vector<std::string> shortestWin;  // walkthrough script lines
    size_t deadEndStates = 0;
    struct DeadEnd {
        std::vector<std::string> path;  // shortest script that falls in
        std::string lastMove;
        std::string diff;               // what changed on that last move
    };
    // One per "door" into the dead region: dead states whose parent was not dead.
    std::vector<DeadEnd> deadEnds;
};

SolveReport solve(const Content& c, const DialogueLibrary& lib, size_t maxStates = 2000000);

// Static checks the solver relies on: every hotspot's walk-to point is
// walkable and reachable from every spawn, every Ink knot exists, and
// goto/talk effects come last in their lists. Returns problems found.
std::vector<std::string> lintContent(const Content& c, const DialogueLibrary& lib);

// Graphviz view of the dependency graph: which moves need and produce what.
std::string dependencyDot(const Content& c);

}  // namespace qe
