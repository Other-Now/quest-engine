#include "core/dialogue.hpp"

#include <set>
#include <sstream>
#include <stdexcept>

#include <choice.h>
#include <compiler.h>
#include <globals.h>
#include <runner.h>
#include <story.h>

namespace qe {

using namespace ink::runtime;

DialogueLibrary::DialogueLibrary(const std::string& inkJsonPath) {
    std::ostringstream bin;
    ink::compiler::compilation_results results;
    ink::compiler::run(inkJsonPath.c_str(), bin, &results);
    if (!results.errors.empty()) throw std::runtime_error(inkJsonPath + ": " + results.errors.front());
    std::string data = bin.str();
    auto* buf = new unsigned char[data.size()];
    std::copy(data.begin(), data.end(), buf);
    story_.reset(story::from_binary(buf, static_cast<ink::size_t>(data.size()), true));
}

DialogueLibrary::~DialogueLibrary() = default;

bool DialogueLibrary::hasKnot(const std::string& knot) const {
    runner r = story_->new_runner();
    return r->move_to(ink::hash_string(knot.c_str()));
}

struct Conversation::Impl {
    globals g;
    runner r;
};

Conversation::Conversation(const DialogueLibrary& lib, const std::string& knot, Store& store)
    : impl_(new Impl), store_(store) {
    impl_->g = lib.story().new_globals();
    impl_->r = lib.story().new_runner(impl_->g);
    impl_->r->set_rng_seed(1);  // deterministic replays; the content avoids shuffles anyway
    for (int i = 0; i < int(store.size()); ++i)
        if (impl_->g->set<int32_t>(store.name(i).c_str(), store.get(i))) synced_.push_back(i);
    if (!impl_->r->move_to(ink::hash_string(knot.c_str())))
        throw std::runtime_error("ink knot not found: " + knot);
    refreshChoices();
}

Conversation::~Conversation() = default;

void Conversation::pull() {
    for (int id : synced_) {
        auto v = impl_->g->get<int32_t>(store_.name(id).c_str());
        if (v) store_.set(id, *v);
    }
}

void Conversation::refreshChoices() {
    choices_.clear();
    if (impl_->r->can_continue()) return;
    for (const choice& c : *impl_->r) choices_.push_back(c.text());
}

bool Conversation::nextLine(DialogueLine& out) {
    while (impl_->r->can_continue()) {
        std::string text = impl_->r->getline();
        while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
        out.speaker.clear();
        for (ink::size_t i = 0; i < impl_->r->num_tags(); ++i) {
            std::string tag = impl_->r->get_tag(i);
            if (tag.rfind("speaker:", 0) == 0) out.speaker = tag.substr(8);
        }
        pull();
        if (text.empty()) continue;
        out.text = text;
        refreshChoices();
        return true;
    }
    refreshChoices();
    return false;
}

void Conversation::choose(size_t index) {
    impl_->r->choose(static_cast<ink::size_t>(index));
    pull();
    refreshChoices();
}

bool Conversation::finished() const { return !impl_->r->can_continue() && choices_.empty(); }

// ---- exploration for the solver ------------------------------------------

namespace {
struct Node {
    std::vector<int> vars;
    std::vector<std::string> choices;
    bool finished;
};

Node runPath(const DialogueLibrary& lib, const std::string& knot, const Store& start,
             const std::vector<int>& path) {
    Store s = start;
    Conversation conv(lib, knot, s);
    DialogueLine line;
    while (conv.nextLine(line)) {
    }
    for (int c : path) {
        conv.choose(size_t(c));
        while (conv.nextLine(line)) {
        }
    }
    return {s.values(), conv.choices(), conv.finished()};
}
}  // namespace

std::vector<DialogueOutcome> exploreConversation(const DialogueLibrary& lib, const std::string& knot,
                                                 const Store& start, int maxDepth) {
    std::vector<DialogueOutcome> out;
    std::set<std::vector<int>> seenOutcome;
    std::set<std::pair<std::vector<int>, std::vector<std::string>>> seenNode;
    std::vector<int> path;
    // Iterative-deepening would be overkill: conversations are a handful of
    // choices deep. Re-running from the knot for each branch keeps Ink's own
    // state out of the picture.
    auto dfs = [&](auto&& self) -> void {
        Node n = runPath(lib, knot, start, path);
        if (n.finished) {
            if (seenOutcome.insert(n.vars).second) {
                DialogueOutcome o{n.vars, {}};
                for (int c : path) o.choicePath.push_back(c + 1);
                out.push_back(o);
            }
            return;
        }
        if (int(path.size()) >= maxDepth) return;
        if (!seenNode.insert({n.vars, n.choices}).second) return;
        for (int i = 0; i < int(n.choices.size()); ++i) {
            path.push_back(i);
            self(self);
            path.pop_back();
        }
    };
    dfs(dfs);
    return out;
}

}  // namespace qe
