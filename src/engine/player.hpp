#pragma once

#include "core/sns.hpp"
#include "engine/image.hpp"

#include <functional>
#include <map>
#include <memory>
#include <unordered_map>

namespace imbk::engine {

// A block of text on the current page: flowing in the text area, or placed at screen coordinates by
// text_pos (12) in "cinematic" scenes. Coordinates are in the game's 1280x720 space.
struct TextBlock {
    bool positioned = false;
    int x = 0, y = 0;
    std::string text; // UTF-8, '\n' = line break
};

struct Layer {
    std::shared_ptr<const Image> image;    // shown
    std::shared_ptr<const Image> previous; // fading out during a transition
    std::shared_ptr<const Image> pending;  // set by picture/movie, shown by transition_start
    bool hasPending = false;
    std::string caption, pendingCaption; // placeholder label (e.g. a movie that is not played yet)
    int fade = 0, fadeLength = 0;        // frames

    float progress() const { return fadeLength <= 0 ? 1.0f : static_cast<float>(fade) / fadeLength; }
};

// Runs the main script (st.sns) one frame at a time and exposes what should be on screen.
// Platform independent: rendering, input and audio live in the front-end.
class Player {
public:
    using PictureLoader = std::function<std::shared_ptr<const Image>(const std::string& name)>;

    Player(const sns::Script& script, PictureLoader loadPicture);

    void start(const std::string& label); // throws if the label does not exist
    void update(bool fastForward);        // call once per frame (60 Hz)
    void advance();                       // player clicked / pressed confirm

    const std::map<int, Layer>& layers() const { return layers_; }
    const std::vector<TextBlock>& page() const { return page_; }
    bool waitingForClick() const { return waitClick_; }
    bool finished() const { return finished_; }
    const std::string& label() const { return label_; }
    const std::string& bgm() const { return bgm_; }
    std::string skippedSummary() const; // opcodes not implemented yet, with counts

private:
    bool step();
    void jump(std::size_t file, const std::string& label);
    TextBlock& block();

    const sns::Script& script_;
    PictureLoader loadPicture_;
    std::vector<std::unordered_map<std::uint32_t, std::size_t>> indexOf_; // per file: code offset -> index

    std::size_t file_ = 0, pc_ = 0;
    std::vector<std::pair<std::size_t, std::size_t>> callStack_;
    std::map<int, Layer> layers_;
    struct Transition {
        int type = 0, layer = -1, frames = 0;
    } transition_;
    std::vector<TextBlock> page_;
    bool inRuby_ = false, rubyJustEnded_ = false;
    std::string reading_;
    int delay_ = 0;
    bool waitClick_ = false, clearOnAdvance_ = false, finished_ = false;
    std::string label_, bgm_;
    std::map<std::uint8_t, int> skipped_;
};

} // namespace imbk::engine
