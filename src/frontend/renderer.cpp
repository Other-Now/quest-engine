#include "frontend/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <stb_image.h>

#include "font8x8_basic.h"

namespace qe {

namespace {
constexpr SDL_Color kWhite{255, 255, 255, 255};
constexpr SDL_Color kGrey{150, 150, 160, 255};
constexpr SDL_Color kYellow{255, 220, 90, 255};
constexpr SDL_Color kCyan{120, 220, 255, 255};
constexpr SDL_Color kPanel{24, 20, 36, 255};
constexpr SDL_Color kSlot{44, 38, 62, 255};
constexpr int kGlyph = 8;

std::vector<std::string> wrap(const std::string& s, size_t width) {
    std::vector<std::string> lines;
    std::string line, word;
    auto flush = [&] {
        if (line.size() + (line.empty() ? 0 : 1) + word.size() > width && !line.empty()) {
            lines.push_back(line);
            line.clear();
        }
        if (!word.empty()) line += (line.empty() ? "" : " ") + word;
        word.clear();
    };
    for (char ch : s) {
        if (ch == ' ')
            flush();
        else
            word += ch;
    }
    flush();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

SDL_Color speakerColor(const std::string& speaker) {
    if (speaker == "me" || speaker.empty()) return kWhite;
    if (speaker == "tobin") return kYellow;
    return kCyan;
}
}  // namespace

Renderer::Renderer(SDL_Renderer* r) : r_(r) {
    // 128 glyphs from the public-domain font8x8, in one white strip.
    std::vector<uint32_t> px(128 * kGlyph * kGlyph, 0);
    for (int c = 0; c < 128; ++c)
        for (int y = 0; y < kGlyph; ++y)
            for (int x = 0; x < kGlyph; ++x)
                if (font8x8_basic[c][y] & (1 << x)) px[size_t(y * 128 * kGlyph + c * kGlyph + x)] = 0xffffffffu;
    font_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, 128 * kGlyph, kGlyph);
    SDL_UpdateTexture(font_, nullptr, px.data(), 128 * kGlyph * 4);
    SDL_SetTextureBlendMode(font_, SDL_BLENDMODE_BLEND);
}

Renderer::~Renderer() {
    flushTextures();
    SDL_DestroyTexture(font_);
}

void Renderer::flushTextures() {
    for (auto& [k, t] : textures_) SDL_DestroyTexture(t);
    textures_.clear();
}

SDL_Texture* Renderer::texture(const std::string& root, const std::string& image) {
    auto it = textures_.find(image);
    if (it != textures_.end()) return it->second;
    int w = 0, h = 0, n = 0;
    std::string path = root + "/" + image;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
    SDL_Texture* t = nullptr;
    if (data) {
        t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
        SDL_UpdateTexture(t, nullptr, data, w * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        stbi_image_free(data);
    } else {
        SDL_Log("cannot load %s: %s", path.c_str(), stbi_failure_reason());
    }
    textures_[image] = t;
    return t;
}

const Tileset* Renderer::tileset(const Room& room, const std::string& name) const {
    for (auto& ts : room.tilesets)
        if (ts.name == name) return &ts;
    return nullptr;
}

void Renderer::drawTile(const Game& g, const Tileset& ts, int index, int x, int y) {
    SDL_Texture* t = texture(g.content().root, ts.image);
    if (!t) return;
    SDL_Rect src{(index % ts.columns) * ts.tilew, (index / ts.columns) * ts.tileh, ts.tilew, ts.tileh};
    SDL_Rect dst{x, y, ts.tilew, ts.tileh};
    SDL_RenderCopy(r_, t, &src, &dst);
}

void Renderer::drawGid(const Game& g, uint32_t gid, int x, int y) {
    if (!gid) return;
    const Tileset* best = nullptr;
    for (auto& ts : g.room().tilesets)
        if (int(gid) >= ts.firstgid && (!best || ts.firstgid > best->firstgid)) best = &ts;
    if (best) drawTile(g, *best, int(gid) - best->firstgid, x, y);
}

void Renderer::fill(Rect r, SDL_Color c) {
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_Rect rr{int(r.x), int(r.y), int(r.w), int(r.h)};
    SDL_RenderFillRect(r_, &rr);
}

void Renderer::text(const std::string& s, int x, int y, SDL_Color c) {
    for (int pass = 0; pass < 2; ++pass) {  // drop shadow, then the text
        SDL_Color col = pass == 0 ? SDL_Color{0, 0, 0, 255} : c;
        SDL_SetTextureColorMod(font_, col.r, col.g, col.b);
        int cx = x + (pass == 0 ? 1 : 0), cy = y + (pass == 0 ? 1 : 0);
        for (unsigned char ch : s) {
            if (ch < 128 && ch != ' ') {
                SDL_Rect src{ch * kGlyph, 0, kGlyph, kGlyph}, dst{cx, cy, kGlyph, kGlyph};
                SDL_RenderCopy(r_, font_, &src, &dst);
            }
            cx += kGlyph;
        }
    }
}

void Renderer::textCentered(const std::string& s, int cx, int y, SDL_Color c) {
    text(s, cx - int(s.size()) * kGlyph / 2, y, c);
}

// ---- scene --------------------------------------------------------------

void Renderer::drawScene(const Game& g, bool debug) {
    const Room& room = g.room();
    int cam = int(std::floor(g.cameraX()));
    auto layer = [&](bool foreground) {
        for (auto& l : room.layers) {
            if (l.foreground != foreground) continue;
            for (int ty = 0; ty < room.height; ++ty)
                for (int tx = 0; tx < room.width; ++tx) {
                    int x = tx * room.tilew - cam;
                    if (x <= -room.tilew || x >= kScreenW) continue;
                    drawGid(g, l.gids[size_t(ty * room.width + tx)], x, ty * room.tileh);
                }
        }
    };
    layer(false);

    // Props and the player, painted back to front by where they touch the floor.
    struct Drawable {
        float baseline;
        int order;
        const Prop* prop;
    };
    std::vector<Drawable> list;
    for (size_t i = 0; i < room.props.size(); ++i)
        if (g.propVisible(room.props[i])) list.push_back({room.props[i].pos.y, int(i), &room.props[i]});
    list.push_back({g.playerPos().y, int(room.props.size()), nullptr});
    std::stable_sort(list.begin(), list.end(), [](const Drawable& a, const Drawable& b) {
        return a.baseline != b.baseline ? a.baseline < b.baseline : a.order < b.order;
    });
    for (auto& d : list) {
        if (d.prop) {
            drawGid(g, d.prop->gid, int(d.prop->pos.x) - cam, int(d.prop->pos.y) - 16);
        } else if (const Tileset* ts = tileset(room, g.content().player.tileset)) {
            Vec2 p = g.playerPos();
            drawTile(g, *ts, g.playerTile(), int(std::floor(p.x)) - 8 - cam, int(std::floor(p.y)) - 16);
        }
    }
    layer(true);
    if (debug) drawDebug(g);
}

void Renderer::drawDebug(const Game& g) {
    const Room& room = g.room();
    float cam = std::floor(g.cameraX());
    auto poly = [&](const Polygon& p, SDL_Color c) {
        SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, 255);
        for (size_t i = 0; i < p.size(); ++i) {
            Vec2 a = p[i], b = p[(i + 1) % p.size()];
            SDL_RenderDrawLineF(r_, a.x - cam, a.y, b.x - cam, b.y);
        }
    };
    const Pathfinder& nav = room.nav;
    poly(nav.area().outer, {255, 60, 60, 255});
    for (auto& h : nav.area().holes) poly(h, {255, 150, 40, 255});
    SDL_SetRenderDrawColor(r_, 80, 80, 255, 255);
    for (size_t i = 0; i < nav.nodes().size(); ++i)
        for (int j : nav.edges()[i]) {
            Vec2 a = nav.nodes()[i], b = nav.nodes()[size_t(j)];
            SDL_RenderDrawLineF(r_, a.x - cam, a.y, b.x - cam, b.y);
        }
    SDL_SetRenderDrawColor(r_, 80, 255, 80, 255);
    Vec2 from = g.playerPos();
    for (Vec2 p : g.path()) {
        SDL_RenderDrawLineF(r_, from.x - cam, from.y, p.x - cam, p.y);
        from = p;
    }
    SDL_SetRenderDrawColor(r_, 0, 255, 255, 255);
    for (auto& h : room.hotspots) {
        if (!g.hotspotVisible(h)) continue;
        SDL_FRect rr{h.rect.x - cam, h.rect.y, h.rect.w, h.rect.h};
        SDL_RenderDrawRectF(r_, &rr);
    }
}

void Renderer::drawMessage(const Game& g) {
    const Message* m = g.message();
    if (!m) return;
    auto lines = wrap(m->text, 36);
    int h = int(lines.size()) * 10 + 6;
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    fill({8, 6, 304, float(h)}, {0, 0, 0, 170});
    int y = 9;
    for (auto& l : lines) {
        textCentered(l, kScreenW / 2, y, speakerColor(m->speaker));
        y += 10;
    }
}

void Renderer::drawPanel(const Game& g, int mx, int my) {
    fill({0, float(kSceneH), float(kScreenW), float(kScreenH - kSceneH)}, kPanel);
    if (g.mode() == Mode::Dialogue) {
        if (!g.showingChoices()) return;
        auto& ch = g.choices();
        for (int i = 0; i < int(ch.size()); ++i) {
            Rect row = ui::choiceRow(i);
            bool hover = row.contains({float(mx), float(my)});
            text(ch[size_t(i)].substr(0, 38), int(row.x) + 4, int(row.y) + 2, hover ? kYellow : kWhite);
        }
        return;
    }
    textCentered(g.sentence(mx, my), kScreenW / 2, ui::kSentenceY, kCyan);
    const char* names[] = {"Look", "Use", "Talk"};
    Verb verbs[] = {Verb::Look, Verb::Use, Verb::Talk};
    for (int i = 0; i < 3; ++i) {
        Rect b = ui::verbButton(verbs[i]);
        bool on = g.selectedVerb() == verbs[i];
        bool hover = b.contains({float(mx), float(my)});
        fill(b, on ? SDL_Color{70, 60, 110, 255} : kSlot);
        textCentered(names[i], int(b.x + b.w / 2), int(b.y) + 3, on ? kYellow : hover ? kWhite : kGrey);
    }
    auto held = g.heldItems();
    for (int i = 0; i < ui::kInvCols * ui::kInvRows; ++i) {
        Rect s = ui::inventorySlot(i);
        bool selected = i < int(held.size()) && held[size_t(i)] == g.selectedItem();
        fill(s, selected ? SDL_Color{90, 80, 140, 255} : kSlot);
        if (i < int(held.size())) {
            const Item& it = g.content().items[size_t(held[size_t(i)])];
            if (const Tileset* ts = tileset(g.room(), it.tileset)) drawTile(g, *ts, it.tile, int(s.x) + 2, int(s.y) + 2);
        }
    }
}

void Renderer::drawCursor(const Game& g, int mx, int my) {
    if (mx < 0 || my < 0) return;
    // Cursor states: busy (dots), carrying an item (its icon), over a
    // hotspot (yellow), or plain (white crosshair).
    bool busy = g.mode() == Mode::Transition || (g.mode() == Mode::Dialogue && !g.showingChoices());
    if (busy) {
        text("...", mx - 12, my - 4, kWhite);
        return;
    }
    if (g.selectedItem() >= 0) {
        const Item& it = g.content().items[size_t(g.selectedItem())];
        if (const Tileset* ts = tileset(g.room(), it.tileset)) drawTile(g, *ts, it.tile, mx - 8, my - 8);
    }
    bool over = g.hotspotAt(mx, my) >= 0;
    SDL_Color c = over ? kYellow : kWhite;
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderDrawLine(r_, mx - 4, my + 1, mx + 4, my + 1);
    SDL_RenderDrawLine(r_, mx + 1, my - 4, mx + 1, my + 4);
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, 255);
    SDL_RenderDrawLine(r_, mx - 4, my, mx + 4, my);
    SDL_RenderDrawLine(r_, mx, my - 4, mx, my + 4);
}

void Renderer::draw(const Game& g, int mx, int my, bool debug) {
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
    SDL_Rect scene{0, 0, kScreenW, kSceneH};
    SDL_RenderSetClipRect(r_, &scene);
    drawScene(g, debug);
    drawMessage(g);
    SDL_RenderSetClipRect(r_, nullptr);
    drawPanel(g, mx, my);
    if (g.mode() == Mode::Credits) {
        fill({0, 0, float(kScreenW), float(kScreenH)}, {0, 0, 0, 220});
        int y = 60;
        for (auto& line : g.content().credits) {
            textCentered(line, kScreenW / 2, y, y == 60 ? kYellow : kWhite);
            y += 12;
        }
    }
    float f = g.fade();
    if (f > 0) fill({0, 0, float(kScreenW), float(kScreenH)}, {0, 0, 0, Uint8(std::min(255.0f, f * 255))});
    drawCursor(g, mx, my);
}

}  // namespace qe
