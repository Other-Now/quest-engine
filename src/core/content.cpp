#include "core/content.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace qe {

using json = nlohmann::json;

const char* verbName(Verb v) {
    switch (v) {
        case Verb::Walk: return "walk";
        case Verb::Look: return "look";
        case Verb::Use: return "use";
        case Verb::Talk: return "talk";
    }
    return "?";
}

bool parseVerb(const std::string& s, Verb& out) {
    for (Verb v : {Verb::Walk, Verb::Look, Verb::Use, Verb::Talk})
        if (s == verbName(v)) return out = v, true;
    return false;
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string dirOf(const std::string& p) {
    auto s = p.find_last_of("/\\");
    return s == std::string::npos ? "" : p.substr(0, s + 1);
}

// "rooms/../tilesets/x.tsj" -> "tilesets/x.tsj"
static std::string normalize(const std::string& p) {
    std::vector<std::string> parts;
    std::stringstream ss(p);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
    }
    std::string out;
    for (auto& x : parts) out += (out.empty() ? "" : "/") + x;
    return out;
}

Condition parseCondition(const std::string& s, const Store& vars) {
    Condition c;
    std::stringstream ss(s);
    std::string term;
    while (std::getline(ss, term, ',')) {
        if (term.empty()) continue;
        auto eq = term.find('=');
        std::string name = term.substr(0, eq);
        int value = eq == std::string::npos ? 1 : std::stoi(term.substr(eq + 1));
        int id = vars.id(name);
        if (id < 0) throw std::runtime_error("condition uses unknown variable '" + name + "'");
        c.eq.push_back({id, value});
    }
    return c;
}

static Condition jsonCondition(const json& j, const Store& vars, const std::string& where) {
    Condition c;
    if (j.is_null()) return c;
    for (auto& [k, v] : j.items()) {
        int id = vars.id(k);
        if (id < 0) throw std::runtime_error(where + ": unknown variable '" + k + "'");
        c.eq.push_back({id, v.get<int>()});
    }
    return c;
}

int Room::hotspotIndex(const std::string& hotspot) const {
    for (size_t i = 0; i < hotspots.size(); ++i)
        if (hotspots[i].id == hotspot) return int(i);
    return -1;
}

int Content::roomIndex(const std::string& id) const {
    for (size_t i = 0; i < rooms.size(); ++i)
        if (rooms[i].id == id) return int(i);
    return -1;
}

int Content::itemIndex(const std::string& id) const {
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].id == id) return int(i);
    return -1;
}

const Rule* Content::findRule(int room, const std::string& hotspot, Verb verb, int item,
                              const Store& s) const {
    for (auto& r : rules)
        if ((r.room < 0 || r.room == room) && r.verb == verb && r.item == item &&
            r.hotspot == hotspot && r.cond.holds(s))
            return &r;
    return nullptr;
}

const Combo* Content::findCombo(int a, int b, const Store& s) const {
    for (auto& c : combos)
        if (((c.a == a && c.b == b) || (c.a == b && c.b == a)) && c.cond.holds(s)) return &c;
    return nullptr;
}

// ---- Tiled ----------------------------------------------------------------

static json props(const json& obj) {
    json out = json::object();
    if (obj.contains("properties"))
        for (auto& p : obj["properties"]) out[p["name"].get<std::string>()] = p["value"];
    return out;
}

static Facing parseFacing(const std::string& s) {
    if (s == "left") return FaceLeft;
    if (s == "right") return FaceRight;
    if (s == "down") return FaceDown;
    return FaceUp;
}

static Room loadRoom(Content& c, const std::string& id, const std::string& rel) {
    Room room;
    room.id = id;
    room.file = rel;
    std::string path = c.root + "/" + rel;
    c.files.push_back(rel);
    json m = json::parse(readFile(path));
    room.width = m.at("width");
    room.height = m.at("height");
    room.tilew = m.at("tilewidth");
    room.tileh = m.at("tileheight");
    std::string dir = dirOf(rel);

    for (auto& ts : m.at("tilesets")) {
        Tileset t;
        t.firstgid = ts.at("firstgid");
        std::string tsRel = normalize(dir + ts.at("source").get<std::string>());
        c.files.push_back(tsRel);
        json tj = json::parse(readFile(c.root + "/" + tsRel));
        t.name = tj.at("name");
        t.columns = tj.at("columns");
        t.tilecount = tj.at("tilecount");
        t.tilew = tj.at("tilewidth");
        t.tileh = tj.at("tileheight");
        t.image = normalize(dirOf(tsRel) + tj.at("image").get<std::string>());
        room.tilesets.push_back(t);
    }

    bool haveOuter = false;
    WalkArea walk;
    for (auto& layer : m.at("layers")) {
        std::string type = layer.at("type"), lname = layer.at("name");
        if (type == "tilelayer") {
            TileLayer tl;
            tl.name = lname;
            tl.foreground = props(layer).value("foreground", false);
            for (auto& g : layer.at("data")) tl.gids.push_back(g.get<uint32_t>() & 0x0fffffffu);
            room.layers.push_back(std::move(tl));
            continue;
        }
        if (type != "objectgroup") continue;
        for (auto& o : layer.at("objects")) {
            json p = props(o);
            std::string oname = o.value("name", "");
            float x = o.at("x"), y = o.at("y");
            if (lname == "props") {
                Prop pr;
                pr.name = oname;
                pr.gid = o.at("gid").get<uint32_t>() & 0x0fffffffu;
                pr.pos = {x, y};
                pr.visible = parseCondition(p.value("visible_if", ""), c.vars);
                room.props.push_back(pr);
            } else if (lname == "hotspots") {
                Hotspot h;
                h.id = oname;
                h.label = p.value("label", oname);
                h.look = p.value("look", "");
                h.rect = {x, y, o.at("width"), o.at("height")};
                h.walkTo = {p.value("walk_x", h.rect.center().x), p.value("walk_y", y + h.rect.h)};
                h.face = parseFacing(p.value("face", "up"));
                h.visible = parseCondition(p.value("visible_if", ""), c.vars);
                room.hotspots.push_back(h);
            } else if (lname == "walkable") {
                Polygon poly;
                for (auto& pt : o.at("polygon")) poly.push_back({x + pt.at("x").get<float>(), y + pt.at("y").get<float>()});
                if (oname == "hole") {
                    walk.holes.push_back(poly);
                } else {
                    walk.outer = poly;
                    haveOuter = true;
                }
            } else if (lname == "spawns") {
                room.spawns[oname] = {x, y};
            }
        }
    }
    if (!haveOuter) throw std::runtime_error(rel + ": no walkable polygon named 'outer'");
    room.nav = Pathfinder(std::move(walk));
    return room;
}

// ---- game.json --------------------------------------------------------------

static std::vector<Effect> parseEffects(const Content& c, const json& list, const std::string& where) {
    std::vector<Effect> out;
    for (auto& e : list) {
        Effect fx;
        if (e.contains("say")) {
            fx.kind = Effect::Say;
            fx.text = e["say"];
            out.push_back(fx);
        } else if (e.contains("set")) {
            for (auto& [k, v] : e["set"].items()) {
                fx.kind = Effect::Set;
                fx.var = c.vars.id(k);
                if (fx.var < 0) throw std::runtime_error(where + ": set of unknown variable '" + k + "'");
                fx.value = v.get<int>();
                out.push_back(fx);
            }
        } else if (e.contains("give") || e.contains("take")) {
            bool give = e.contains("give");
            std::string id = give ? e["give"] : e["take"];
            int it = c.itemIndex(id);
            if (it < 0) throw std::runtime_error(where + ": unknown item '" + id + "'");
            fx.kind = Effect::Set;
            fx.var = c.items[size_t(it)].hasVar;
            fx.value = give ? 1 : 0;
            out.push_back(fx);
        } else if (e.contains("goto")) {
            std::string g = e["goto"];
            auto colon = g.find(':');
            fx.kind = Effect::Goto;
            fx.room = c.roomIndex(g.substr(0, colon));
            fx.spawn = colon == std::string::npos ? "" : g.substr(colon + 1);
            if (fx.room < 0) throw std::runtime_error(where + ": goto unknown room in '" + g + "'");
            if (!c.rooms[size_t(fx.room)].spawns.count(fx.spawn))
                throw std::runtime_error(where + ": goto unknown spawn in '" + g + "'");
            out.push_back(fx);
        } else if (e.contains("talk")) {
            fx.kind = Effect::Talk;
            fx.text = e["talk"];
            out.push_back(fx);
        } else {
            throw std::runtime_error(where + ": unknown effect " + e.dump());
        }
    }
    return out;
}

std::shared_ptr<Content> loadContent(const std::string& root) {
    auto c = std::make_shared<Content>();
    c->root = root;
    c->files.push_back("game.json");
    json g = json::parse(readFile(root + "/game.json"));
    c->title = g.value("title", "untitled");
    c->revision = g.value("revision", 1);
    for (auto& line : g.value("credits", json::array())) c->credits.push_back(line);

    for (auto& [k, v] : g.at("vars").items()) c->vars.define(k, v.get<int>());
    c->wonVar = c->vars.define("game_won", 0);
    for (auto& it : g.at("items")) {
        Item item;
        item.id = it.at("id");
        item.name = it.value("name", item.id);
        item.look = it.value("look", "");
        std::string icon = it.at("icon");
        auto colon = icon.find(':');
        item.tileset = icon.substr(0, colon);
        item.tile = std::stoi(icon.substr(colon + 1));
        item.hasVar = c->vars.define("has_" + item.id, it.value("start", 0));
        c->items.push_back(item);
    }

    auto& pl = g.at("player");
    c->player.tileset = pl.at("tileset");
    c->player.speed = pl.value("speed", 1.0f);
    c->player.frameTicks = pl.value("frame_ticks", 8);
    for (auto& [k, v] : pl.at("anims").items()) c->player.anims[k] = v.get<std::vector<int>>();

    // Rooms before rules, so rules can be checked against them.
    for (auto& [id, file] : g.at("rooms").items()) c->rooms.push_back(loadRoom(*c, id, file));
    auto& start = g.at("start");
    c->startRoom = c->roomIndex(start.at("room"));
    c->startSpawn = start.at("spawn");
    if (c->startRoom < 0) throw std::runtime_error("start room not found");

    int n = 0;
    for (auto& r : g.at("rules")) {
        std::string where = "rule #" + std::to_string(n++);
        Rule rule;
        std::string room = r.value("room", "*");
        rule.room = room == "*" ? -1 : c->roomIndex(room);
        if (room != "*" && rule.room < 0) throw std::runtime_error(where + ": unknown room " + room);
        rule.hotspot = r.at("on");
        if (rule.room >= 0 && c->rooms[size_t(rule.room)].hotspotIndex(rule.hotspot) < 0)
            throw std::runtime_error(where + ": room " + room + " has no hotspot " + rule.hotspot);
        if (!parseVerb(r.at("verb"), rule.verb)) throw std::runtime_error(where + ": bad verb");
        if (r.contains("item")) {
            rule.item = c->itemIndex(r["item"]);
            if (rule.item < 0) throw std::runtime_error(where + ": unknown item");
        }
        rule.cond = jsonCondition(r.value("if", json()), c->vars, where);
        rule.effects = parseEffects(*c, r.at("do"), where);
        c->rules.push_back(std::move(rule));
    }
    n = 0;
    for (auto& r : g.value("combos", json::array())) {
        std::string where = "combo #" + std::to_string(n++);
        Combo cb;
        cb.a = c->itemIndex(r.at("a"));
        cb.b = c->itemIndex(r.at("b"));
        if (cb.a < 0 || cb.b < 0) throw std::runtime_error(where + ": unknown item");
        cb.cond = jsonCondition(r.value("if", json()), c->vars, where);
        cb.effects = parseEffects(*c, r.at("do"), where);
        c->combos.push_back(std::move(cb));
    }
    c->win = jsonCondition(g.at("win"), c->vars, "win");
    c->inkJson = g.at("dialogue");
    c->files.push_back(c->inkJson);
    for (auto& [rev, m] : g.value("renames", json::object()).items())
        for (auto& [from, to] : m.items()) c->renames[std::stoi(rev)][from] = to.get<std::string>();

    Fnv1a f;
    for (auto& file : c->files) f.str(readFile(root + "/" + file));
    c->hash = f.h;
    return c;
}

}  // namespace qe
