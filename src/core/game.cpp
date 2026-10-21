#include "core/game.hpp"

#include <algorithm>
#include <cstring>

namespace qe {

namespace ui {
Rect verbButton(Verb v) {
    int i = v == Verb::Look ? 0 : v == Verb::Use ? 1 : 2;
    return {4.0f, float(190 + i * 16), 44.0f, 14.0f};
}
Rect inventorySlot(int i) { return {float(54 + (i % kInvCols) * 20), float(190 + (i / kInvCols) * 22), 19.0f, 20.0f}; }
Rect choiceRow(int i) { return {4.0f, float(180 + i * 12), 312.0f, 12.0f}; }
}  // namespace ui

namespace {
constexpr int kFadeTicks = 16;
constexpr float kArriveSlack = 2.0f;

int messageTicks(const std::string& text) { return std::min(60 + 4 * int(text.size()), 420); }
}  // namespace

Game::Game(std::shared_ptr<const Content> content) : content_(std::move(content)) {
    dialogue_ = std::make_shared<DialogueLibrary>(content_->root + "/" + content_->inkJson);
    newGame();
}

Game::~Game() = default;

void Game::newGame() {
    store_ = content_->vars;
    store_.resetToDefaults();
    mode_ = Mode::Play;
    tick_ = 0;
    messages_.clear();
    conv_.reset();
    won_ = false;
    resetSelection();
    enterRoom(content_->startRoom, content_->startSpawn);
}

void Game::reloadContent(std::shared_ptr<const Content> content) {
    Store next = content->vars;
    next.resetToDefaults();
    for (int i = 0; i < int(store_.size()); ++i) {
        int id = next.id(store_.name(i));
        if (id >= 0) next.set(id, store_.get(i));
    }
    std::string roomId = room().id;
    auto lib = std::make_shared<DialogueLibrary>(content->root + "/" + content->inkJson);
    content_ = std::move(content);
    dialogue_ = std::move(lib);
    store_ = next;
    conv_.reset();
    if (mode_ == Mode::Dialogue) mode_ = Mode::Play;
    messages_.clear();
    pending_.reset();
    path_.clear();
    int r = content_->roomIndex(roomId);
    room_ = r >= 0 ? r : content_->startRoom;
    pos_ = room().nav.clampInside(pos_);
    updateCamera(true);
}

// ---- input --------------------------------------------------------------

void Game::tick(const std::vector<InputEvent>& events) {
    bool advance = false;
    for (auto& e : events) {
        if (mode_ == Mode::Dialogue) {
            if (e.type == InputEvent::Click && showingChoices()) {
                for (int i = 0; i < int(conv_->choices().size()); ++i)
                    if (ui::choiceRow(i).contains({float(e.x), float(e.y)})) {
                        conv_->choose(size_t(i));
                        if (content_->win.holds(store_)) won_ = true;
                        break;
                    }
            } else {
                advance = true;
            }
        } else if (mode_ == Mode::Play) {
            click(e);
        }
    }

    switch (mode_) {
        case Mode::Play:
            updateWalk();
            if (!messages_.empty() && --messages_.front().ticks <= 0) messages_.erase(messages_.begin());
            if (won_ && messages_.empty() && path_.empty() && mode_ == Mode::Play) mode_ = Mode::Credits;
            break;
        case Mode::Dialogue:
            updateDialogue(advance);
            break;
        case Mode::Transition:
            ++fadeTicks_;
            if (fadeTicks_ == kFadeTicks) enterRoom(gotoRoom_, gotoSpawn_);
            if (fadeTicks_ >= 2 * kFadeTicks) {
                fadeTicks_ = 0;
                mode_ = Mode::Play;
            }
            break;
        case Mode::Credits:
            break;
    }
    updateCamera(false);
    ++tick_;
}

void Game::click(const InputEvent& e) {
    if (e.type == InputEvent::RightClick) {
        resetSelection();
        return;
    }
    if (!messages_.empty()) {  // a click skips the current line
        messages_.erase(messages_.begin());
        return;
    }
    if (e.y >= kSceneH)
        clickPanel(e.x, e.y);
    else
        clickScene(e.x, e.y);
}

void Game::clickPanel(int x, int y) {
    Vec2 p{float(x), float(y)};
    for (Verb v : {Verb::Look, Verb::Use, Verb::Talk})
        if (ui::verbButton(v).contains(p)) {
            verb_ = v;
            item_ = -1;
            return;
        }
    auto held = heldItems();
    for (int i = 0; i < int(held.size()); ++i)
        if (ui::inventorySlot(i).contains(p)) {
            clickItem(held[size_t(i)]);
            return;
        }
}

void Game::clickItem(int item) {
    const Item& it = content_->items[size_t(item)];
    if (verb_ == Verb::Look) {
        say(it.look.empty() ? "It's a " + it.name + "." : it.look);
        resetSelection();
    } else if (verb_ == Verb::Talk) {
        say("I'd rather not talk to the " + it.name + ".");
        resetSelection();
    } else if (item_ == item) {
        resetSelection();
    } else if (item_ >= 0) {
        const Combo* c = content_->findCombo(item_, item, store_);
        resetSelection();
        if (c)
            apply(c->effects);
        else
            say("Those don't go together.");
    } else {
        verb_ = Verb::Use;
        item_ = item;
    }
}

void Game::clickScene(int x, int y) {
    int h = hotspotAt(x, y);
    if (h < 0) {
        pending_.reset();
        resetSelection();
        walkTo({float(x) + camX_, float(y)});
        return;
    }
    pending_ = Pending{h, verb_, item_};
    resetSelection();
    walkTo(room().hotspots[size_t(h)].walkTo);
}

void Game::walkTo(Vec2 target) {
    path_ = room().nav.findPath(pos_, target);
}

// ---- actions ------------------------------------------------------------

void Game::act(int h, Verb verb, int item) {
    const Hotspot& hs = room().hotspots[size_t(h)];
    if (!hotspotVisible(hs)) return;
    if (const Rule* r = content_->findRule(room_, hs.id, verb, item, store_)) {
        apply(r->effects);
        return;
    }
    if (item >= 0) {
        say("That doesn't work.");
        return;
    }
    switch (verb) {
        case Verb::Look: say(hs.look.empty() ? "Nothing special about the " + hs.label + "." : hs.look); break;
        case Verb::Use: say("I can't use that."); break;
        case Verb::Talk: say("It doesn't talk back."); break;
        case Verb::Walk: break;
    }
}

void Game::apply(const std::vector<Effect>& effects) {
    for (auto& fx : effects) {
        switch (fx.kind) {
            case Effect::Say: say(fx.text); break;
            case Effect::Set: store_.set(fx.var, fx.value); break;
            case Effect::Goto:
                mode_ = Mode::Transition;
                fadeTicks_ = 0;
                gotoRoom_ = fx.room;
                gotoSpawn_ = fx.spawn;
                break;
            case Effect::Talk:
                conv_ = std::make_unique<Conversation>(*dialogue_, fx.text, store_);
                mode_ = Mode::Dialogue;
                break;
        }
    }
    if (content_->win.holds(store_)) won_ = true;
}

void Game::say(const std::string& text, const std::string& speaker) {
    messages_.push_back({speaker, text, messageTicks(text)});
}

void Game::enterRoom(int r, const std::string& spawn) {
    room_ = r;
    auto it = room().spawns.find(spawn);
    pos_ = room().nav.clampInside(it != room().spawns.end() ? it->second : Vec2{room().pixelWidth() / 2, 140});
    path_.clear();
    pending_.reset();
    updateCamera(true);
}

void Game::updateWalk() {
    if (!path_.empty()) {
        Vec2 d = path_.front() - pos_;
        float len = length(d);
        float speed = content_->player.speed;
        if (std::abs(d.x) > std::abs(d.y))
            face_ = d.x < 0 ? FaceLeft : FaceRight;
        else if (len > 0)
            face_ = d.y < 0 ? FaceUp : FaceDown;
        if (len <= speed) {
            pos_ = path_.front();
            path_.erase(path_.begin());
        } else {
            pos_ = pos_ + d * (speed / len);
        }
        ++walkTicks_;
        if (!path_.empty()) return;
    }
    walkTicks_ = 0;
    if (!pending_) return;
    Pending p = *pending_;
    pending_.reset();
    const Hotspot& hs = room().hotspots[size_t(p.hotspot)];
    if (dist(pos_, room().nav.clampInside(hs.walkTo)) > kArriveSlack) {
        say("I can't get there.");
        return;
    }
    face_ = hs.face;
    act(p.hotspot, p.verb, p.item);
}

void Game::updateDialogue(bool advance) {
    if (!messages_.empty()) {
        if (!advance && --messages_.front().ticks > 0) return;
        messages_.erase(messages_.begin());
    }
    DialogueLine line;
    if (conv_->nextLine(line)) {
        messages_.push_back({line.speaker, line.text, messageTicks(line.text) + 30});
        if (content_->win.holds(store_)) won_ = true;
        return;
    }
    if (!conv_->choices().empty()) return;  // waiting for a choice click
    conv_.reset();
    mode_ = Mode::Play;
    resetSelection();
    if (content_->win.holds(store_)) won_ = true;
}

void Game::updateCamera(bool snap) {
    float maxX = std::max(0.0f, room().pixelWidth() - float(kScreenW));
    float target = std::min(std::max(pos_.x - kScreenW / 2.0f, 0.0f), maxX);
    if (snap) {
        camX_ = target;
        return;
    }
    camX_ += (target - camX_) * 0.12f;
    if (std::abs(target - camX_) < 0.05f) camX_ = target;
}

// ---- queries --------------------------------------------------------------

int Game::playerTile() const {
    static const char* dirs[] = {"left", "down", "up", "right"};
    bool moving = !path_.empty();
    auto it = content_->player.anims.find(std::string(moving ? "walk_" : "idle_") + dirs[face_]);
    if (it == content_->player.anims.end() || it->second.empty()) return 0;
    if (!moving) return it->second[0];
    uint32_t frame = walkTicks_ / uint32_t(std::max(1, content_->player.frameTicks));
    return it->second[frame % it->second.size()];
}

float Game::fade() const {
    if (mode_ != Mode::Transition) return 0;
    int t = fadeTicks_ < kFadeTicks ? fadeTicks_ : 2 * kFadeTicks - fadeTicks_;
    return float(t) / kFadeTicks;
}

const std::vector<std::string>& Game::choices() const {
    static const std::vector<std::string> none;
    return conv_ ? conv_->choices() : none;
}

bool Game::showingChoices() const {
    return mode_ == Mode::Dialogue && conv_ && messages_.empty() && !conv_->choices().empty();
}

std::vector<int> Game::heldItems() const {
    std::vector<int> out;
    for (int i = 0; i < int(content_->items.size()); ++i)
        if (store_.get(content_->items[size_t(i)].hasVar) != 0) out.push_back(i);
    return out;
}

int Game::hotspotAt(int sx, int sy) const {
    if (sy < 0 || sy >= kSceneH) return -1;
    Vec2 w{float(sx) + camX_, float(sy)};
    auto& hs = room().hotspots;
    for (int i = int(hs.size()) - 1; i >= 0; --i)
        if (hotspotVisible(hs[size_t(i)]) && hs[size_t(i)].rect.contains(w)) return i;
    return -1;
}

std::string Game::sentence(int sx, int sy) const {
    std::string s;
    switch (verb_) {
        case Verb::Walk: s = "Walk to"; break;
        case Verb::Look: s = "Look at"; break;
        case Verb::Talk: s = "Talk to"; break;
        case Verb::Use: s = item_ >= 0 ? "Use " + content_->items[size_t(item_)].name + " with" : "Use"; break;
    }
    std::string target;
    int h = hotspotAt(sx, sy);
    if (h >= 0) target = room().hotspots[size_t(h)].label;
    if (sy >= kSceneH) {
        auto held = heldItems();
        for (int i = 0; i < int(held.size()); ++i)
            if (ui::inventorySlot(i).contains({float(sx), float(sy)}) && held[size_t(i)] != item_)
                target = content_->items[size_t(held[size_t(i)])].name;
    }
    return target.empty() ? s : s + " " + target;
}

bool Game::idle() const { return mode_ == Mode::Play && path_.empty() && !pending_ && messages_.empty(); }

uint64_t Game::stateHash() const {
    Fnv1a f;
    f.pod(tick_);
    f.pod(int32_t(room_));
    f.pod(store_.hash());
    uint32_t bits[3];
    std::memcpy(&bits[0], &pos_.x, 4);
    std::memcpy(&bits[1], &pos_.y, 4);
    std::memcpy(&bits[2], &camX_, 4);
    f.pod(bits);
    f.pod(uint8_t(face_));
    f.pod(uint8_t(mode_));
    f.pod(uint32_t(path_.size()));
    f.pod(uint32_t(messages_.size()));
    f.pod(int32_t(fadeTicks_));
    f.pod(uint8_t(won_));
    return f.h;
}

void Game::restore(int room, Vec2 pos, Facing face, const std::vector<int>& vars, uint32_t tick) {
    mode_ = Mode::Play;
    messages_.clear();
    conv_.reset();
    pending_.reset();
    path_.clear();
    resetSelection();
    fadeTicks_ = 0;
    room_ = room;
    pos_ = this->room().nav.clampInside(pos);
    face_ = face;
    store_.setValues(vars);
    tick_ = tick;
    won_ = content_->win.holds(store_);
    updateCamera(true);
}

}  // namespace qe
