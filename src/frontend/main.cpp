// The playable game: SDL2 window, fixed-timestep loop, input, hot reload,
// recording/replay, and frame-time measurement.
//
//   quest [--assets DIR] [--record OUT.qrp] [--replay IN.qrp | --script WALKTHROUGH.txt]
//         [--bench] [--no-vsync] [--debug] [--screenshot-at TICK OUT.bmp [--quit-after-shot]]
//
// Keys: F2 debug overlay (walkable area, visibility graph, path, hotspots),
//       F5 save, F9 load, Esc quit.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "core/replay.hpp"
#include "core/save.hpp"
#include "frontend/renderer.hpp"

using namespace qe;
namespace fs = std::filesystem;

namespace {

struct FrameStats {
    std::vector<double> work, interval;  // milliseconds
    void report(const char* label) const {
        auto pct = [](std::vector<double> v, double p) {
            if (v.empty()) return 0.0;
            std::sort(v.begin(), v.end());
            return v[std::min(v.size() - 1, size_t(p * double(v.size())))];
        };
        double total = 0;
        for (double d : interval) total += d;
        std::printf("FRAMES %s: %zu frames | work p50 %.3f ms p99 %.3f ms max %.3f ms | "
                    "interval p50 %.2f ms p99 %.2f ms | %.1f fps\n",
                    label, work.size(), pct(work, 0.5), pct(work, 0.99), pct(work, 1.0), pct(interval, 0.5),
                    pct(interval, 0.99), total > 0 ? 1000.0 * double(interval.size()) / total : 0.0);
        std::fflush(stdout);
    }
};

double nowMs() {
#ifdef __EMSCRIPTEN__
    // SDL_GetPerformanceCounter only ticked in whole milliseconds in the web
    // build (every frame measured 0 or 1 ms). performance.now() is finer
    // (Chrome clamps it to 0.1 ms on a page without cross-origin isolation).
    return emscripten_get_now();
#else
    return 1000.0 * double(SDL_GetPerformanceCounter()) / double(SDL_GetPerformanceFrequency());
#endif
}

struct App {
    std::string assets = "assets", recordPath, replayPath, scriptPath, shotPath, savePath = "saves/slot1.json";
    bool bench = false, vsync = true, debug = false, running = true, quitAfterShot = false;
    uint32_t shotTick = 0;

    SDL_Window* window = nullptr;
    SDL_Renderer* sdl = nullptr;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Game> game;

    // input source: live clicks, or a recording (from --replay or --script)
    bool playback = false;
    Recording playbackRec;
    size_t playbackNext = 0;
    std::vector<InputEvent> pending;
    Recording recorded;

    double last = 0, acc = 0;
    FrameStats stats;
    uint32_t frames = 0;
    std::map<std::string, fs::file_time_type> mtimes;

    void snapshotMtimes() {
#ifndef __EMSCRIPTEN__
        mtimes.clear();
        std::error_code ec;
        for (auto& f : game->content().files) mtimes[f] = fs::last_write_time(assets + "/" + f, ec);
#endif
    }

    // Hot reload: poll the content files; on change, reload but keep progress.
    void checkReload() {
#ifndef __EMSCRIPTEN__
        std::error_code ec;
        bool changed = false;
        for (auto& [f, t] : mtimes)
            if (fs::last_write_time(assets + "/" + f, ec) != t) changed = true;
        if (!changed) return;
        try {
            auto content = loadContent(assets);
            game->reloadContent(content);
            renderer->flushTextures();
            std::printf("hot reload: content %s\n", hex64(content->hash).c_str());
        } catch (const std::exception& e) {
            std::printf("hot reload failed, keeping the old content: %s\n", e.what());
        }
        snapshotMtimes();  // don't retry a broken file every poll
#endif
    }

    void onKey(SDL_Keycode k) {
        if (k == SDLK_F2) debug = !debug;
        if (k == SDLK_ESCAPE) running = false;
        if (k == SDLK_F5 && game->mode() == Mode::Play) {
            std::error_code ec;
            fs::create_directories(fs::path(savePath).parent_path(), ec);
            saveGameFile(*game, savePath);
            std::printf("saved %s\n", savePath.c_str());
        }
        if (k == SDLK_F9) {
            try {
                auto doc = nlohmann::json::parse(readFile(savePath));
                std::printf("loading %s (%s)\n", savePath.c_str(), describeSave(doc).c_str());
                loadGame(*game, doc);
            } catch (const std::exception& e) {
                std::printf("load failed: %s\n", e.what());
            }
        }
    }

    void frame() {
        double start = nowMs();
        if (last > 0) stats.interval.push_back(start - last);
        double dt = last > 0 ? start - last : 0;
        last = start;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN && !ev.key.repeat) onKey(ev.key.keysym.sym);
            if (ev.type == SDL_MOUSEBUTTONDOWN && !playback) {
                InputEvent e;
                e.type = ev.button.button == SDL_BUTTON_RIGHT ? InputEvent::RightClick : InputEvent::Click;
                e.x = int16_t(ev.button.x);  // already in logical coordinates
                e.y = int16_t(ev.button.y);
                pending.push_back(e);
            }
        }

        // Fixed timestep: the sim always advances in 1/60 s steps. Cap the
        // catch-up so a long stall can't snowball (the "spiral of death").
        const double step = 1000.0 / kTicksPerSecond;
        acc = std::min(acc + dt, 5 * step);
        if (frames == 0) acc = step;
        while (acc >= step) {
            acc -= step;
            std::vector<InputEvent> evs;
            if (playback) {
                while (playbackNext < playbackRec.events.size() &&
                       playbackRec.events[playbackNext].tick == game->tickCount())
                    evs.push_back(playbackRec.events[playbackNext++].ev);
            } else {
                evs.swap(pending);
                for (auto& e : evs) recorded.events.push_back({game->tickCount(), e});
            }
            game->tick(evs);
        }
        if (++frames % 30 == 0) checkReload();

        int mx = -1, my = -1;
        if (SDL_GetMouseFocus()) {
            int wx, wy;
            SDL_GetMouseState(&wx, &wy);
            float lx, ly;
            SDL_RenderWindowToLogical(sdl, wx, wy, &lx, &ly);
            mx = int(lx);
            my = int(ly);
        }
        renderer->draw(*game, mx, my, debug);
        stats.work.push_back(nowMs() - start);  // CPU time to simulate + build the frame
        if (!shotPath.empty() && game->tickCount() >= shotTick) {
            int w, h;
            SDL_GetRendererOutputSize(sdl, &w, &h);
            SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_RenderReadPixels(sdl, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
            SDL_SaveBMP(s, shotPath.c_str());
            SDL_FreeSurface(s);
            std::printf("screenshot %s at tick %u\n", shotPath.c_str(), game->tickCount());
            shotPath.clear();
            if (quitAfterShot) running = false;
        }
        SDL_RenderPresent(sdl);

        if (bench && playback && game->tickCount() >= playbackRec.endTick + 30) {
            stats.report(
#ifdef __EMSCRIPTEN__
                "browser"
#else
                "desktop"
#endif
            );
            running = false;
        }
    }

    void shutdown() {
        if (!recordPath.empty() && !playback) {
            recorded.contentHash = game->content().hash;
            recorded.endTick = game->tickCount();
            recorded.finalHash = game->stateHash();
            writeRecording(recorded, recordPath);
            std::printf("recorded %zu clicks to %s\n", recorded.events.size(), recordPath.c_str());
        }
        if (!bench) stats.report("session");
    }
};

App* gApp = nullptr;

#ifdef __EMSCRIPTEN__
void webFrame() {
    gApp->frame();
    if (!gApp->running) {
        gApp->shutdown();
        emscripten_cancel_main_loop();
    }
}
#endif

}  // namespace

int main(int argc, char** argv) {
    static App app;
    gApp = &app;
#ifdef __EMSCRIPTEN__
    app.assets = "/assets";
    app.savePath = "/saves/slot1.json";
#endif
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--assets") app.assets = next();
        else if (a == "--record") app.recordPath = next();
        else if (a == "--replay") app.replayPath = next();
        else if (a == "--script") app.scriptPath = next();
        else if (a == "--bench") app.bench = true;
        else if (a == "--no-vsync") app.vsync = false;
        else if (a == "--debug") app.debug = true;
        else if (a == "--quit-after-shot") app.quitAfterShot = true;
        else if (a == "--screenshot-at") {
            app.shotTick = uint32_t(std::stoul(next()));
            app.shotPath = next();
        }
    }

    try {
        app.game = std::make_unique<Game>(loadContent(app.assets));
        if (!app.scriptPath.empty()) {
            // Let the bot play it headless first; then show its clicks in real time.
            Game tmp(app.game->contentPtr());
            BotResult r = runWalkthrough(tmp, loadScript(app.scriptPath));
            if (!r.ok) throw std::runtime_error("walkthrough failed: " + r.error);
            app.playbackRec = r.recording;
            app.playback = true;
        } else if (!app.replayPath.empty()) {
            app.playbackRec = readRecording(app.replayPath);
            app.playback = true;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    app.window = SDL_CreateWindow(app.game->content().title.c_str(), SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED, kScreenW * 3, kScreenH * 3, SDL_WINDOW_RESIZABLE);
    app.sdl = SDL_CreateRenderer(app.window, -1,
                                 SDL_RENDERER_ACCELERATED | (app.vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    if (!app.window || !app.sdl) {
        std::fprintf(stderr, "SDL window/renderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(app.sdl, kScreenW, kScreenH);
    SDL_RenderSetIntegerScale(app.sdl, SDL_TRUE);
    SDL_ShowCursor(SDL_DISABLE);
    app.renderer = std::make_unique<Renderer>(app.sdl);
    app.snapshotMtimes();

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(webFrame, 0, 1);
#else
    while (app.running) app.frame();
    app.shutdown();
    app.renderer.reset();
    SDL_DestroyRenderer(app.sdl);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
#endif
    return 0;
}
