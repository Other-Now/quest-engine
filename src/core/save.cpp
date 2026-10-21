#include "core/save.hpp"

#include <fstream>
#include <stdexcept>

namespace qe {

using json = nlohmann::json;

json saveGame(const Game& g) {
    json vars = json::object();
    for (int i = 0; i < int(g.store().size()); ++i) vars[g.store().name(i)] = g.store().get(i);
    return {
        {"format", kSaveFormat},
        {"content_rev", g.content().revision},
        {"room", g.room().id},
        {"x", g.playerPos().x},
        {"y", g.playerPos().y},
        {"facing", int(g.facing())},
        {"tick", g.tickCount()},
        {"vars", vars},
    };
}

void migrateSave(json& doc) {
    int v = doc.value("format", 0);
    if (v < 1 || v > kSaveFormat) throw std::runtime_error("unsupported save format " + std::to_string(v));
}

void loadGame(Game& g, json doc) {
    migrateSave(doc);
    const Content& c = g.content();
    int room = c.roomIndex(doc.at("room"));
    if (room < 0) throw std::runtime_error("save refers to unknown room");

    // Content renames, oldest revision first.
    json vars = doc.at("vars");
    int rev = doc.value("content_rev", 1);
    for (auto& [r, renames] : c.renames) {
        if (r <= rev) continue;
        for (auto& [from, to] : renames)
            if (vars.contains(from)) {
                vars[to] = vars[from];
                vars.erase(from);
            }
    }
    Store s = c.vars;
    s.resetToDefaults();
    for (auto& [name, value] : vars.items()) {
        int id = s.id(name);
        if (id >= 0) s.set(id, value.get<int>());  // removed variables are dropped
    }
    g.restore(room, {doc.at("x").get<float>(), doc.at("y").get<float>()}, Facing(doc.value("facing", 1)),
              s.values(), doc.value("tick", 0u));
}

void saveGameFile(const Game& g, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path);
    f << saveGame(g).dump(2) << "\n";
}

void loadGameFile(Game& g, const std::string& path) { loadGame(g, json::parse(readFile(path))); }

}  // namespace qe
