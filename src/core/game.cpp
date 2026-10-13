#include "core/game.hpp"

#include <algorithm>

namespace imbk {

Game Game::open(const std::filesystem::path& input) {
    Game g;
    if (std::filesystem::is_directory(input)) {
        g.root_ = input;
        if (!std::filesystem::exists(input / "PS3_GAME"))
            throw std::runtime_error(input.string() + " does not contain PS3_GAME/ (expected the disc root)");
    } else {
        g.iso_ = std::make_shared<const Iso9660>(input);
        if (!g.iso_->find("PS3_GAME")) throw std::runtime_error(input.string() + " is not a PS3 game disc image");
    }
    return g;
}

Slice Game::file(std::string_view path) const {
    if (iso_) return iso_->open(path);
    const auto p = root_ / std::filesystem::path(std::string(path));
    if (!std::filesystem::is_regular_file(p)) throw std::runtime_error("missing game file: " + p.string());
    return Slice::wholeFile(p);
}

bool Game::exists(std::string_view path) const {
    if (iso_) return iso_->find(path).has_value();
    return std::filesystem::exists(root_ / std::filesystem::path(std::string(path)));
}

std::vector<std::string> Game::list(std::string_view dirPath) const {
    std::vector<std::string> names;
    if (iso_) {
        for (const auto& e : iso_->list(dirPath))
            if (!e.isDirectory) names.push_back(e.name);
    } else {
        for (const auto& e : std::filesystem::directory_iterator(root_ / std::filesystem::path(std::string(dirPath))))
            if (e.is_regular_file()) names.push_back(e.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> Game::chunkNames() const {
    auto names = list(kChunkDir);
    std::erase_if(names, [](const std::string& n) { return !n.ends_with(".afs"); });
    return names;
}

std::string Game::describe() const { return iso_ ? "ISO image" : "disc folder " + root_.string(); }

} // namespace imbk
