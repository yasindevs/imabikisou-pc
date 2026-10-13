#include "engine/player.hpp"

#include <cstdio>
#include <stdexcept>
#include <tuple>

namespace imbk::engine {
namespace {

std::shared_ptr<const Image> blackScreen() {
    static const auto img = [] {
        auto i = std::make_shared<Image>();
        i->width = 1280;
        i->height = 720;
        i->pixels.assign(1280 * 720, 0xFF000000u);
        return i;
    }();
    return img;
}

} // namespace

Player::Player(const sns::Script& script, PictureLoader loadPicture)
    : script_(script), loadPicture_(std::move(loadPicture)), indexOf_(script.files.size()) {
    for (std::size_t f = 0; f < script.files.size(); ++f)
        for (std::size_t i = 0; i < script.files[f].code.size(); ++i) indexOf_[f][script.files[f].code[i].offset] = i;
}

void Player::start(const std::string& label) {
    for (std::size_t f = 0; f < script_.files.size(); ++f)
        for (const auto& l : script_.files[f].labels)
            if (l.name == label) {
                jump(f, label);
                return;
            }
    throw std::runtime_error("unknown label " + label);
}

void Player::jump(std::size_t file, const std::string& label) {
    if (file >= script_.files.size()) throw std::runtime_error("jump to a missing script file");
    for (const auto& l : script_.files[file].labels) {
        if (l.name != label) continue;
        const auto it = indexOf_[file].find(l.target);
        if (it == indexOf_[file].end()) throw std::runtime_error("label " + label + " does not point at an instruction");
        file_ = file;
        pc_ = it->second;
        label_ = label;
        return;
    }
    throw std::runtime_error("label " + label + " not found in " + script_.files[file].name);
}

TextBlock& Player::block() {
    if (page_.empty()) page_.emplace_back();
    return page_.back();
}

void Player::advance() {
    if (!waitClick_) return;
    waitClick_ = false;
    if (clearOnAdvance_) {
        page_.clear();
        clearOnAdvance_ = false;
    }
}

void Player::update(bool fastForward) {
    for (auto& [_, layer] : layers_) {
        if (layer.fade < layer.fadeLength) layer.fade = fastForward ? layer.fadeLength : layer.fade + 1;
        if (layer.fade >= layer.fadeLength) layer.previous.reset();
    }
    if (delay_ > 0) {
        delay_ = fastForward ? 0 : delay_ - 1;
        if (delay_ > 0) return;
    }
    if (waitClick_) {
        if (!fastForward) return;
        advance();
    }
    for (int n = 0; n < 10000 && !finished_ && step(); ++n) {}
}

// Executes one instruction. Returns false when the script has to wait (delay, click, end).
bool Player::step() {
    const auto& code = script_.files[file_].code;
    if (pc_ >= code.size()) {
        finished_ = true;
        return false;
    }
    const auto& in = code[pc_++];
    const auto a = script_.args(in);
    auto u16 = [&](std::size_t o) -> std::size_t { return o + 2 <= a.size() ? be16(a, o) : 0; };

    switch (in.opcode) {
    // --- text ---------------------------------------------------------------------------------------
    case sns::op::Text: {
        const std::string text(reinterpret_cast<const char*>(a.data()), a.size());
        if (inRuby_) {
            reading_ += text; // furigana are not drawn yet
        } else {
            block().text += text;
            rubyJustEnded_ = false;
        }
        return true;
    }
    case sns::op::RubyBegin: inRuby_ = true; reading_.clear(); return true;
    case sns::op::RubyEnd: inRuby_ = false; rubyJustEnded_ = true; return true;
    case sns::op::Name:
        // Character names are inserted by the game; until their table is found, use the reading
        // given by the furigana that precede the name, if any.
        block().text += rubyJustEnded_ && !reading_.empty() ? reading_ : std::string("\xE2\x97\x86"); // ◆
        rubyJustEnded_ = false;
        return true;
    case sns::op::Newline: block().text += '\n'; return true;
    case 0x12: { // text_pos: u16 x, u16 y
        TextBlock b;
        b.positioned = true;
        b.x = static_cast<int>(u16(0));
        b.y = static_cast<int>(u16(2));
        page_.push_back(std::move(b));
        return true;
    }
    case sns::op::Wait: waitClick_ = true; return false;
    case sns::op::Page: waitClick_ = true; clearOnAdvance_ = true; return false;
    case sns::op::MessageBegin:
    case sns::op::MessageEnd:
        // A line of dialogue stands on its own line: text before or after it on the same page starts a new one.
        if (!page_.empty() && !block().text.empty() && !block().text.ends_with('\n')) block().text += '\n';
        return true;
    case sns::op::NarrationBegin:
    case sns::op::NarrationEnd: return true;

    // --- timing -------------------------------------------------------------------------------------
    case 0x29: delay_ = a.size() >= 4 ? static_cast<int>(be32(a, 0)) : 0; return delay_ <= 0;

    // --- pictures, movies, transitions ---------------------------------------------------------------
    case 0x40: { // picture: u16 index, u8 layer, u8 wait
        const auto index = u16(0);
        const int layer = a.size() > 2 ? a[2] : 0;
        auto& l = layers_[layer];
        l.pending = index < script_.resources.pictures.size() ? loadPicture_(script_.resources.pictures[index]) : nullptr;
        l.pendingCaption.clear();
        l.hasPending = true;
        return true;
    }
    case 0x8E: { // movie: u16 index, u16, u8 layer, u8 wait — not decoded yet, shown as a captioned black screen
        const auto index = u16(0) & 0x7FFF;
        const int layer = a.size() > 4 ? a[4] : 0;
        auto& l = layers_[layer];
        l.pending = blackScreen();
        l.pendingCaption = "[vid\xC3\xA9o] " + (index < script_.resources.movies.size() ? script_.resources.movies[index] : "?");
        l.hasPending = true;
        return true;
    }
    case 0x8F: // movie_stop
        for (auto& [_, l] : layers_)
            if (l.caption.starts_with("[vid")) l.caption.clear();
        return true;
    case 0x59: // transition_set: u8 type, u8 layer, u8 ?, u16 frames
        transition_.type = a.size() > 0 ? a[0] : 0;
        transition_.layer = a.size() > 1 ? a[1] : -1;
        transition_.frames = static_cast<int>(u16(3));
        return true;
    case 0x5A: return true; // transition_wait: pictures are loaded synchronously, always ready
    case 0x5B: {            // transition_start: u8 layer
        const int layer = a.empty() ? 0 : a[0];
        auto& l = layers_[layer];
        if (l.hasPending) {
            l.previous = l.image;
            l.image = l.pending;
            l.caption = l.pendingCaption;
            l.pending.reset();
            l.hasPending = false;
            l.fade = 0;
            l.fadeLength = transition_.layer == layer ? transition_.frames : 0;
        }
        return true;
    }
    case 0x60: // picture_clear: u8 layer
        if (!a.empty()) layers_.erase(a[0]);
        return true;

    // --- sound (not played yet) ---------------------------------------------------------------------
    case 0x88: {
        const auto index = u16(0) & 0x3FFF;
        bgm_ = index < script_.resources.bgm.size() ? script_.resources.bgm[index] : "?";
        return true;
    }

    // --- flow ---------------------------------------------------------------------------------------
    case sns::op::Label: label_ = script_.labelName(in); return true;
    case sns::op::Jump:
    case sns::op::Branch: // conditions are not evaluated yet: the first branch reached is taken
        if (const auto ref = script_.labelRef(in)) jump(ref->file, ref->name);
        return true;
    case 0x51: // call
        if (const auto ref = script_.labelRef(in)) {
            callStack_.emplace_back(file_, pc_);
            jump(ref->file, ref->name);
        }
        return true;
    case 0x52: // ret
        if (!callStack_.empty()) {
            std::tie(file_, pc_) = callStack_.back();
            callStack_.pop_back();
        }
        return true;
    case 0x4E: ++skipped_[in.opcode]; return true; // if_flag: flags not modelled yet, never taken
    case sns::op::Return:
        if (!callStack_.empty()) {
            std::tie(file_, pc_) = callStack_.back();
            callStack_.pop_back();
            return true;
        }
        finished_ = true;
        return false;
    case 0x1E: case 0x96: case 0x97: case 0x98: return true; // scene markers (flowchart, place, title)
    }
    ++skipped_[in.opcode];
    return true;
}

std::string Player::skippedSummary() const {
    std::string s;
    char buf[32];
    for (const auto& [op, n] : skipped_) {
        std::snprintf(buf, sizeof buf, "%02X(%s)x%d ", op, std::string(sns::mnemonic(op)).c_str(), n);
        s += buf;
    }
    return s;
}

} // namespace imbk::engine
