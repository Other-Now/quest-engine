// Draws a Game with SDL2's 2D renderer at the 320x240 logical resolution.
// Read-only view of the sim: nothing here changes game state.
#pragma once
#include <map>
#include <string>

#include <SDL.h>

#include "core/game.hpp"

namespace qe {

class Renderer {
public:
    explicit Renderer(SDL_Renderer* r);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void draw(const Game& g, int mouseX, int mouseY, bool debug);
    void flushTextures();  // after a content hot reload

private:
    SDL_Texture* texture(const std::string& root, const std::string& image);
    const Tileset* tileset(const Room& room, const std::string& name) const;
    void drawGid(const Game& g, uint32_t gid, int x, int y);
    void drawTile(const Game& g, const Tileset& ts, int index, int x, int y);
    void drawScene(const Game& g, bool debug);
    void drawDebug(const Game& g);
    void drawPanel(const Game& g, int mx, int my);
    void drawMessage(const Game& g);
    void drawCursor(const Game& g, int mx, int my);
    void fill(Rect r, SDL_Color c);
    void text(const std::string& s, int x, int y, SDL_Color c);
    void textCentered(const std::string& s, int cx, int y, SDL_Color c);

    SDL_Renderer* r_;
    SDL_Texture* font_ = nullptr;
    std::map<std::string, SDL_Texture*> textures_;
};

}  // namespace qe
