// Ink dialogue through inkcpp, bridged to the shared Store.
//
// The writer's .ink is compiled by inklecate (or exported from Inky) to JSON;
// at load time inkcpp's compiler turns that JSON into its binary format.
//
// Contract with the writer: any Ink VAR whose name matches a store variable is
// synced. It is pushed into Ink when a conversation starts and pulled back
// after every line and choice. Each conversation gets fresh Ink globals, so
// state that must persist (or that the solver must see) lives in the store,
// never in Ink visit counts.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/store.hpp"

namespace ink::runtime {
class story;
}

namespace qe {

struct DialogueLine {
    std::string speaker;  // from a "#speaker:name" tag; empty = narrator
    std::string text;
};

class DialogueLibrary {
public:
    // Compile an inklecate JSON file. Throws on failure.
    explicit DialogueLibrary(const std::string& inkJsonPath);
    ~DialogueLibrary();
    ink::runtime::story& story() const { return *story_; }
    bool hasKnot(const std::string& knot) const;

private:
    std::unique_ptr<ink::runtime::story> story_;
};

class Conversation {
public:
    Conversation(const DialogueLibrary& lib, const std::string& knot, Store& store);
    ~Conversation();
    Conversation(const Conversation&) = delete;
    Conversation& operator=(const Conversation&) = delete;

    // Next line of output, if any. Pulls Ink vars into the store afterwards.
    bool nextLine(DialogueLine& out);
    const std::vector<std::string>& choices() const { return choices_; }
    void choose(size_t index);
    bool finished() const;

    // Store variables that this Ink story declares (and therefore syncs).
    const std::vector<int>& syncedVars() const { return synced_; }

private:
    void pull();
    void refreshChoices();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Store& store_;
    std::vector<int> synced_;
    std::vector<std::string> choices_;
};

// Every distinct end state of one conversation, with the choices that reach
// it. Used by the solver: it runs the real Ink story down every branch.
struct DialogueOutcome {
    std::vector<int> vars;
    std::vector<int> choicePath;  // 1-based, as the walkthrough script writes them
};
std::vector<DialogueOutcome> exploreConversation(const DialogueLibrary& lib, const std::string& knot,
                                                 const Store& start, int maxDepth = 16);

}  // namespace qe
