// The game simulation: one fixed 60 Hz step per tick(), no SDL.
//
// Input is a list of clicks in logical screen coordinates (320x240), so the
// same code runs the SDL frontend, the headless replays in CI, the walkthrough
// bot and save/load tests. Same clicks on the same ticks give the same state.
#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/content.hpp"
#include "core/dialogue.hpp"

namespace qe {

constexpr int kScreenW = 320, kScreenH = 240, kSceneH = 176;
constexpr int kTicksPerSecond = 60;

struct InputEvent {
    enum Type : uint8_t { Click, RightClick } type = Click;
    int16_t x = 0, y = 0;
};

// Panel layout, shared by the renderer (to draw) and the sim (to hit-test).
namespace ui {
constexpr int kSentenceY = 179;
constexpr int kInvCols = 13, kInvRows = 2;
Rect verbButton(Verb v);  // Look, Use, Talk
Rect inventorySlot(int i);
Rect choiceRow(int i);
}  // namespace ui

enum class Mode : uint8_t { Play, Dialogue, Transition, Credits };

struct Message {
    std::string speaker, text;
    int ticks = 0;
};

class Game {
public:
    explicit Game(std::shared_ptr<const Content> content);
    ~Game();

    void newGame();
    void tick(const std::vector<InputEvent>& events);

    // Swap in reloaded content, keeping progress (hot reload).
    void reloadContent(std::shared_ptr<const Content> content);

    // ---- queries (renderer, bot, tests) ----
    const Content& content() const { return *content_; }
    std::shared_ptr<const Content> contentPtr() const { return content_; }
    const DialogueLibrary& dialogue() const { return *dialogue_; }
    const Store& store() const { return store_; }
    int roomIndex() const { return room_; }
    const Room& room() const { return content_->rooms[size_t(room_)]; }
    Mode mode() const { return mode_; }
    uint32_t tickCount() const { return tick_; }
    Vec2 playerPos() const { return pos_; }
    Facing facing() const { return face_; }
    bool walking() const { return !path_.empty(); }
    const std::vector<Vec2>& path() const { return path_; }
    int playerTile() const;
    float cameraX() const { return camX_; }
    float fade() const;  // 0 = clear, 1 = black
    const Message* message() const { return messages_.empty() ? nullptr : &messages_.front(); }
    const std::vector<std::string>& choices() const;
    bool showingChoices() const;
    Verb selectedVerb() const { return verb_; }
    int selectedItem() const { return item_; }
    std::vector<int> heldItems() const;
    bool hotspotVisible(const Hotspot& h) const { return h.visible.holds(store_); }
    bool propVisible(const Prop& p) const { return p.visible.holds(store_); }
    int hotspotAt(int sx, int sy) const;  // screen coords -> hotspot index or -1
    std::string sentence(int sx, int sy) const;  // "Use bucket with well"
    // Nothing in flight: ready for the next command.
    bool idle() const;
    uint64_t stateHash() const;

    // ---- save/load support ----
    void restore(int room, Vec2 pos, Facing face, const std::vector<int>& vars, uint32_t tick);

private:
    void click(const InputEvent& e);
    void clickPanel(int x, int y);
    void clickScene(int x, int y);
    void clickItem(int item);
    void walkTo(Vec2 target);
    void act(int hotspot, Verb verb, int item);
    void apply(const std::vector<Effect>& effects);
    void say(const std::string& text, const std::string& speaker = "me");
    void enterRoom(int room, const std::string& spawn);
    void updateWalk();
    void updateDialogue(bool clicked);
    void updateCamera(bool snap);
    void resetSelection() { verb_ = Verb::Walk; item_ = -1; }

    std::shared_ptr<const Content> content_;
    std::shared_ptr<DialogueLibrary> dialogue_;
    Store store_;
    Mode mode_ = Mode::Play;
    uint32_t tick_ = 0;
    int room_ = 0;
    Vec2 pos_;
    Facing face_ = FaceDown;
    std::vector<Vec2> path_;
    uint32_t walkTicks_ = 0;
    float camX_ = 0;
    struct Pending {
        int hotspot = -1;
        Verb verb = Verb::Walk;
        int item = -1;
    };
    std::optional<Pending> pending_;
    Verb verb_ = Verb::Walk;
    int item_ = -1;
    std::vector<Message> messages_;
    // Room transition: fade out, switch, fade in.
    int fadeTicks_ = 0;
    int gotoRoom_ = -1;
    std::string gotoSpawn_;
    std::unique_ptr<Conversation> conv_;
    bool won_ = false;
};

}  // namespace qe
