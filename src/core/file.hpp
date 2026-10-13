#pragma once

#include "core/binary.hpp"

#include <filesystem>
#include <fstream>
#include <memory>

namespace imbk {

// Read-only random access to a file on disk (the ISO can be ~10 GB, so nothing is loaded eagerly).
class File {
public:
    explicit File(const std::filesystem::path& path);

    const std::filesystem::path& path() const { return path_; }
    std::uint64_t size() const { return size_; }
    void read(std::uint64_t offset, void* dst, std::size_t size) const;

private:
    std::filesystem::path path_;
    std::uint64_t size_ = 0;
    mutable std::ifstream in_;
};

// A byte range inside a File: a whole file on disk, a file inside the ISO, or an entry inside an archive.
class Slice {
public:
    Slice() = default;
    Slice(std::shared_ptr<const File> file, std::uint64_t offset, std::uint64_t size);
    static Slice wholeFile(const std::filesystem::path& path);

    std::uint64_t size() const { return size_; }
    Bytes read(std::uint64_t offset, std::size_t size) const;
    Bytes readAll() const;
    Slice sub(std::uint64_t offset, std::uint64_t size) const;
    void copyTo(const std::filesystem::path& dst) const;

private:
    std::shared_ptr<const File> file_;
    std::uint64_t offset_ = 0;
    std::uint64_t size_ = 0;
};

} // namespace imbk
