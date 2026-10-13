#pragma once

#include "core/iso9660.hpp"

namespace imbk {

// The user's copy of the game: either the .iso itself or a folder holding the disc contents
// (a mounted drive like "D:\" or an extracted copy with PS3_GAME/ inside).
class Game {
public:
    static constexpr std::string_view kChunkDir = "PS3_GAME/USRDIR/data/chunk";

    static Game open(const std::filesystem::path& input);

    // Path relative to the disc root, '/'-separated.
    Slice file(std::string_view path) const;
    bool exists(std::string_view path) const;
    std::vector<std::string> list(std::string_view dirPath) const; // file names only

    Slice chunk(std::string_view name) const { return file(std::string(kChunkDir) + "/" + std::string(name)); }
    std::vector<std::string> chunkNames() const;

    std::string describe() const;
    bool isIso() const { return iso_ != nullptr; }
    std::vector<std::string> allFiles() const; // every file of the disc, relative and '/'-separated

private:
    std::shared_ptr<const Iso9660> iso_;
    std::filesystem::path root_;
};

} // namespace imbk
