// Game content: rooms (Tiled .tmj maps), items, and the interaction rules.
//
// The rules table is the puzzle dependency graph: each rule says "in room R,
// doing VERB (with ITEM) on HOTSPOT, when these variables hold, has these
// effects". The engine executes it at runtime and the solver searches over it.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/geom.hpp"
#include "core/pathfind.hpp"
#include "core/store.hpp"

namespace qe {

enum class Verb : uint8_t { Walk, Look, Use, Talk };
const char* verbName(Verb v);
bool parseVerb(const std::string& s, Verb& out);

enum Facing : uint8_t { FaceLeft = 0, FaceDown = 1, FaceUp = 2, FaceRight = 3 };

// All (var == value) pairs must hold.
struct Condition {
    std::vector<std::pair<int, int>> eq;
    bool holds(const Store& s) const {
        for (auto& [id, v] : eq)
            if (s.get(id) != v) return false;
        return true;
    }
};

struct Effect {
    enum Kind : uint8_t { Say, Set, Goto, Talk } kind = Say;
    std::string text;            // Say: line; Talk: ink knot
    int var = -1, value = 0;     // Set (give/take are Set on has_<item>)
    int room = -1;               // Goto
    std::string spawn;           // Goto
};

struct Tileset {
    std::string name, image;  // image path relative to content root
    int firstgid = 1, columns = 1, tilecount = 0, tilew = 16, tileh = 16;
};

struct TileLayer {
    std::string name;
    bool foreground = false;  // drawn over actors
    std::vector<uint32_t> gids;
};

// A tile object, depth-sorted with the actors by its bottom edge.
struct Prop {
    std::string name;
    uint32_t gid = 0;
    Vec2 pos;  // bottom-left, as Tiled stores tile objects
    Condition visible;
};

struct Hotspot {
    std::string id, label, look;
    Rect rect;
    Vec2 walkTo;
    Facing face = FaceUp;
    Condition visible;
};

struct Room {
    std::string id, file;
    int width = 0, height = 0, tilew = 16, tileh = 16;  // in tiles
    std::vector<Tileset> tilesets;
    std::vector<TileLayer> layers;
    std::vector<Prop> props;
    std::vector<Hotspot> hotspots;
    std::map<std::string, Vec2> spawns;
    Pathfinder nav;
    float pixelWidth() const { return float(width * tilew); }
    float pixelHeight() const { return float(height * tileh); }
    int hotspotIndex(const std::string& hotspot) const;
};

struct Item {
    std::string id, name, look;
    int hasVar = -1;
    std::string tileset;
    int tile = 0;
};

struct Rule {
    int room = -1;  // -1 = any room
    std::string hotspot;
    Verb verb = Verb::Use;
    int item = -1;  // -1 = no item
    Condition cond;
    std::vector<Effect> effects;
};

struct Combo {
    int a = -1, b = -1;
    Condition cond;
    std::vector<Effect> effects;
};

struct PlayerDef {
    std::string tileset;
    float speed = 1.0f;  // pixels per tick
    int frameTicks = 8;
    std::map<std::string, std::vector<int>> anims;  // "walk_left" -> tiles
};

struct Content {
    std::string root, title;
    int revision = 1;
    int startRoom = 0;
    std::string startSpawn;
    std::vector<std::string> credits;
    Store vars;  // definitions + defaults
    std::vector<Item> items;
    std::vector<Rule> rules;
    std::vector<Combo> combos;
    Condition win;
    int wonVar = -1;
    std::vector<Room> rooms;
    PlayerDef player;
    std::string inkJson;  // path relative to root
    // Saves from an older content revision: var renames applied on load.
    std::map<int, std::map<std::string, std::string>> renames;
    std::vector<std::string> files;  // every file loaded, for hot reload
    uint64_t hash = 0;

    int roomIndex(const std::string& id) const;
    int itemIndex(const std::string& id) const;
    const Rule* findRule(int room, const std::string& hotspot, Verb verb, int item,
                         const Store& s) const;
    const Combo* findCombo(int a, int b, const Store& s) const;
};

// Throws std::runtime_error with a readable message on bad content.
std::shared_ptr<Content> loadContent(const std::string& root);

// "a=1,b=0" -> Condition; unknown variable names throw.
Condition parseCondition(const std::string& s, const Store& vars);

std::string readFile(const std::string& path);

}  // namespace qe
