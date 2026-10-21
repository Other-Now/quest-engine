// Run the game without a window: a walkthrough script through the bot, or a
// recorded replay. Used by CI to play from start to credits on every push.
//
//   qe-headless <assets> --script walkthrough.txt [--record out.qrp] [--expect-win]
//   qe-headless <assets> --replay run.qrp            (fails if the final hash differs)
//   qe-headless <assets> --replay run.qrp --sim-bench 20
//   qe-headless <assets> --script part.txt --save out.json   (save where the script stops)
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/game.hpp"
#include "core/replay.hpp"
#include "core/save.hpp"

using namespace qe;

static int usage() {
    std::fprintf(stderr,
                 "usage: qe-headless <assets> (--script FILE [--record OUT] | --replay FILE [--sim-bench N]) "
                 "[--expect-win]\n");
    return 2;
}

static void printState(const Game& g) {
    std::printf("tick %u  room %s  mode %s  hash %s\n", g.tickCount(), g.room().id.c_str(),
                g.mode() == Mode::Credits ? "credits" : "play", hex64(g.stateHash()).c_str());
}

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    std::string assets = argv[1], script, record, replay, savePath;
    bool expectWin = false;
    int benchRuns = 0;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--script" && i + 1 < argc) script = argv[++i];
        else if (a == "--record" && i + 1 < argc) record = argv[++i];
        else if (a == "--replay" && i + 1 < argc) replay = argv[++i];
        else if (a == "--sim-bench" && i + 1 < argc) benchRuns = std::atoi(argv[++i]);
        else if (a == "--save" && i + 1 < argc) savePath = argv[++i];
        else if (a == "--expect-win") expectWin = true;
        else return usage();
    }
    try {
        Game game(loadContent(assets));
        if (!script.empty()) {
            BotResult res = runWalkthrough(game, loadScript(script));
            printState(game);
            std::printf("walkthrough: %zu clicks over %u ticks (%.1f s of game time)\n",
                        res.recording.events.size(), res.recording.endTick,
                        res.recording.endTick / double(kTicksPerSecond));
            if (!res.ok) {
                std::fprintf(stderr, "walkthrough failed at %s\n", res.error.c_str());
                return 1;
            }
            if (!savePath.empty()) {
                saveGameFile(game, savePath);
                std::printf("saved %s\n", savePath.c_str());
            }
            if (!record.empty()) {
                writeRecording(res.recording, record);
                std::printf("recorded %s\n", record.c_str());
            }
            // Prove the recording reproduces the run before trusting it.
            uint64_t again = playRecording(game, res.recording);
            if (again != res.recording.finalHash) {
                std::fprintf(stderr, "replay of the bot's own recording diverged: %s vs %s\n",
                             hex64(again).c_str(), hex64(res.recording.finalHash).c_str());
                return 1;
            }
            std::printf("replay of recording: identical final hash\n");
        } else if (!replay.empty()) {
            Recording rec = readRecording(replay);
            if (rec.contentHash != game.content().hash)
                std::fprintf(stderr, "warning: recording was made with different content (%s vs %s)\n",
                             hex64(rec.contentHash).c_str(), hex64(game.content().hash).c_str());
            uint64_t h = playRecording(game, rec);
            printState(game);
            if (rec.finalHash && h != rec.finalHash) {
                std::fprintf(stderr, "REPLAY DIVERGED: expected %s got %s\n", hex64(rec.finalHash).c_str(),
                             hex64(h).c_str());
                return 1;
            }
            if (rec.finalHash) std::printf("replay matches recorded final hash\n");
            if (benchRuns > 0) {
                auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < benchRuns; ++i) playRecording(game, rec);
                double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
                std::printf("sim: %d runs x %u ticks, %.2f us/tick\n", benchRuns, rec.endTick,
                            us / (double(benchRuns) * rec.endTick));
            }
        } else {
            return usage();
        }
        if (expectWin && game.mode() != Mode::Credits) {
            std::fprintf(stderr, "expected to reach the credits\n");
            return 1;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
