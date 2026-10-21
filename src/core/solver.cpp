#include "core/solver.hpp"

#include <deque>
#include <map>
#include <set>
#include <sstream>

namespace qe {

namespace {

struct Node {
    std::vector<int> key;  // [room, vars...]
    int parent = -1;
    std::vector<std::string> moves;  // script lines from parent to here
    std::vector<int> preds;
};

struct Successor {
    std::vector<int> key;
    std::vector<std::string> moves;
};

void applyBranching(const DialogueLibrary& lib, const std::vector<Effect>& fx, int room,
                    Store s, std::vector<std::string> moves, std::vector<Successor>& out) {
    for (size_t i = 0; i < fx.size(); ++i) {
        const Effect& e = fx[i];
        if (e.kind == Effect::Set) {
            s.set(e.var, e.value);
        } else if (e.kind == Effect::Goto) {
            room = e.room;
        } else if (e.kind == Effect::Talk) {
            for (auto& o : exploreConversation(lib, e.text, s)) {
                std::vector<int> key{room};
                key.insert(key.end(), o.vars.begin(), o.vars.end());
                auto m = moves;
                for (int choice : o.choicePath) m.push_back("choose " + std::to_string(choice));
                out.push_back({key, m});
            }
            return;  // lint guarantees talk is last
        }
    }
    std::vector<int> key{room};
    key.insert(key.end(), s.values().begin(), s.values().end());
    out.push_back({key, moves});
}

std::vector<Successor> successors(const Content& c, const DialogueLibrary& lib, const std::vector<int>& key) {
    int room = key[0];
    Store s = c.vars;
    s.setValues(std::vector<int>(key.begin() + 1, key.end()));
    std::vector<int> held;
    for (int i = 0; i < int(c.items.size()); ++i)
        if (s.get(c.items[size_t(i)].hasVar) != 0) held.push_back(i);

    std::vector<Successor> out;
    for (auto& h : c.rooms[size_t(room)].hotspots) {
        if (!h.visible.holds(s)) continue;
        for (Verb v : {Verb::Walk, Verb::Look, Verb::Use, Verb::Talk})
            if (const Rule* r = c.findRule(room, h.id, v, -1, s))
                applyBranching(lib, r->effects, room, s, {std::string(verbName(v)) + " " + h.id}, out);
        for (int it : held)
            if (const Rule* r = c.findRule(room, h.id, Verb::Use, it, s))
                applyBranching(lib, r->effects, room, s, {"use " + c.items[size_t(it)].id + " " + h.id}, out);
    }
    for (size_t i = 0; i < held.size(); ++i)
        for (size_t j = i + 1; j < held.size(); ++j)
            if (const Combo* cb = c.findCombo(held[i], held[j], s))
                applyBranching(lib, cb->effects, room, s,
                               {"combine " + c.items[size_t(held[i])].id + " " + c.items[size_t(held[j])].id}, out);
    return out;
}

std::vector<std::string> pathTo(const std::vector<Node>& nodes, int id) {
    std::vector<int> chain;
    for (int n = id; n > 0; n = nodes[size_t(n)].parent) chain.push_back(n);
    std::vector<std::string> out;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        for (auto& m : nodes[size_t(*it)].moves) out.push_back(m);
    return out;
}

}  // namespace

SolveReport solve(const Content& c, const DialogueLibrary& lib, size_t maxStates) {
    SolveReport rep;
    std::vector<Node> nodes;
    std::map<std::vector<int>, int> index;
    std::vector<char> isWin;

    Store start = c.vars;
    start.resetToDefaults();
    std::vector<int> k0{c.startRoom};
    k0.insert(k0.end(), start.values().begin(), start.values().end());
    nodes.push_back({k0, -1, {}, {}});
    index[k0] = 0;

    // Forward BFS: shortest paths in moves.
    for (size_t at = 0; at < nodes.size(); ++at) {
        Store s = c.vars;
        s.setValues(std::vector<int>(nodes[at].key.begin() + 1, nodes[at].key.end()));
        bool win = c.win.holds(s);
        isWin.push_back(win);
        if (win) continue;  // the game ends here
        for (auto& succ : successors(c, lib, nodes[at].key)) {
            if (succ.key == nodes[at].key) continue;
            ++rep.transitions;
            auto [it, fresh] = index.emplace(succ.key, int(nodes.size()));
            if (fresh) {
                if (nodes.size() >= maxStates) throw std::runtime_error("state limit reached");
                nodes.push_back({succ.key, int(at), succ.moves, {}});
            }
            nodes[size_t(it->second)].preds.push_back(int(at));
        }
    }
    rep.states = nodes.size();

    // Backward search from every win.
    std::vector<char> canWin(nodes.size(), 0);
    std::deque<int> q;
    for (size_t i = 0; i < nodes.size(); ++i)
        if (isWin[i]) canWin[i] = 1, q.push_back(int(i)), ++rep.winStates;
    while (!q.empty()) {
        int n = q.front();
        q.pop_front();
        for (int p : nodes[size_t(n)].preds)
            if (!canWin[size_t(p)]) canWin[size_t(p)] = 1, q.push_back(p);
    }
    rep.winnable = canWin[0];

    // Shortest win: first win state in BFS order.
    for (size_t i = 0; i < nodes.size(); ++i)
        if (isWin[i]) {
            rep.shortestWin = pathTo(nodes, int(i));
            break;
        }

    for (size_t i = 0; i < nodes.size(); ++i) {
        if (canWin[i]) continue;
        ++rep.deadEndStates;
        int p = nodes[i].parent;
        if (p < 0 || !canWin[size_t(p)]) continue;
        SolveReport::DeadEnd d;
        d.path = pathTo(nodes, int(i));
        for (auto& m : nodes[i].moves) d.lastMove += (d.lastMove.empty() ? "" : " / ") + m;
        auto& a = nodes[size_t(p)].key;
        auto& b = nodes[i].key;
        std::ostringstream diff;
        if (a[0] != b[0]) diff << "room " << c.rooms[size_t(a[0])].id << "->" << c.rooms[size_t(b[0])].id << " ";
        for (size_t v = 1; v < a.size(); ++v)
            if (a[v] != b[v]) diff << c.vars.name(int(v - 1)) << " " << a[v] << "->" << b[v] << " ";
        d.diff = diff.str();
        rep.deadEnds.push_back(d);
    }
    return rep;
}

std::vector<std::string> lintContent(const Content& c, const DialogueLibrary& lib) {
    std::vector<std::string> problems;
    for (auto& room : c.rooms) {
        for (auto& [name, sp] : room.spawns)
            if (!room.nav.inside(sp)) problems.push_back(room.id + ": spawn '" + name + "' is not walkable");
        for (auto& h : room.hotspots) {
            Vec2 target = room.nav.clampInside(h.walkTo);
            if (dist(target, h.walkTo) > 2.0f)
                problems.push_back(room.id + "/" + h.id + ": walk-to point is outside the walkable area");
            for (auto& [name, sp] : room.spawns) {
                auto path = room.nav.findPath(sp, h.walkTo);
                Vec2 end = path.empty() ? room.nav.clampInside(sp) : path.back();
                if (dist(end, target) > 0.5f)
                    problems.push_back(room.id + "/" + h.id + ": unreachable from spawn '" + name + "'");
            }
        }
    }
    auto checkEffects = [&](const std::vector<Effect>& fx, const std::string& where) {
        for (size_t i = 0; i < fx.size(); ++i) {
            bool terminal = fx[i].kind == Effect::Goto || fx[i].kind == Effect::Talk;
            if (terminal && i + 1 != fx.size()) problems.push_back(where + ": goto/talk must be the last effect");
            if (fx[i].kind == Effect::Talk && !lib.hasKnot(fx[i].text))
                problems.push_back(where + ": ink knot '" + fx[i].text + "' not found");
        }
    };
    for (size_t i = 0; i < c.rules.size(); ++i) checkEffects(c.rules[i].effects, "rule #" + std::to_string(i));
    for (size_t i = 0; i < c.combos.size(); ++i) checkEffects(c.combos[i].effects, "combo #" + std::to_string(i));
    return problems;
}

std::string dependencyDot(const Content& c) {
    std::ostringstream o;
    o << "digraph puzzle {\n  rankdir=LR;\n  node [fontname=\"Helvetica\", fontsize=10];\n";
    std::set<int> used;
    auto ruleNode = [&](const std::string& id, const std::string& label, const Condition& cond,
                        const std::vector<Effect>& fx) {
        o << "  " << id << " [shape=box, label=\"" << label << "\"];\n";
        for (auto& [v, val] : cond.eq) {
            used.insert(v);
            o << "  v" << v << " -> " << id << (val == 0 ? " [style=dashed, label=\"=0\"]" : "") << ";\n";
        }
        for (auto& e : fx) {
            if (e.kind == Effect::Set) {
                used.insert(e.var);
                o << "  " << id << " -> v" << e.var << (e.value == 0 ? " [style=dashed, label=\"=0\"]" : "") << ";\n";
            } else if (e.kind == Effect::Talk) {
                o << "  " << id << " -> ink_" << e.text << ";\n";
                o << "  ink_" << e.text << " [shape=note, label=\"ink: " << e.text << "\"];\n";
            } else if (e.kind == Effect::Goto) {
                o << "  " << id << " -> room_" << c.rooms[size_t(e.room)].id << " [style=dotted];\n";
            }
        }
    };
    for (size_t i = 0; i < c.rules.size(); ++i) {
        auto& r = c.rules[i];
        std::string label = std::string(verbName(r.verb)) + " " +
                            (r.item >= 0 ? c.items[size_t(r.item)].id + " on " : "") + r.hotspot;
        ruleNode("r" + std::to_string(i), label, r.cond, r.effects);
    }
    for (size_t i = 0; i < c.combos.size(); ++i) {
        auto& cb = c.combos[i];
        Condition cond = cb.cond;
        cond.eq.push_back({c.items[size_t(cb.a)].hasVar, 1});
        cond.eq.push_back({c.items[size_t(cb.b)].hasVar, 1});
        ruleNode("c" + std::to_string(i), "combine " + c.items[size_t(cb.a)].id + " + " + c.items[size_t(cb.b)].id,
                 cond, cb.effects);
    }
    for (int v : used) o << "  v" << v << " [shape=ellipse, label=\"" << c.vars.name(v) << "\"];\n";
    o << "}\n";
    return o.str();
}

}  // namespace qe
