#include "core/file.hpp"

#include <algorithm>

namespace imbk {

File::File(const std::filesystem::path& path) : path_(path), in_(path, std::ios::binary) {
    if (!in_) throw std::runtime_error("cannot open " + path.string());
    size_ = std::filesystem::file_size(path);
}

void File::read(std::uint64_t offset, void* dst, std::size_t size) const {
    if (offset > size_ || size > size_ - offset) throw FormatError("read past end of " + path_.string());
    in_.clear();
    in_.seekg(static_cast<std::streamoff>(offset));
    in_.read(static_cast<char*>(dst), static_cast<std::streamsize>(size));
    if (!in_) throw std::runtime_error("I/O error while reading " + path_.string());
}

Slice::Slice(std::shared_ptr<const File> file, std::uint64_t offset, std::uint64_t size)
    : file_(std::move(file)), offset_(offset), size_(size) {
    if (offset > file_->size() || size > file_->size() - offset)
        throw FormatError("slice out of bounds in " + file_->path().string());
}

Slice Slice::wholeFile(const std::filesystem::path& path) {
    auto file = std::make_shared<const File>(path);
    const auto size = file->size();
    return Slice(std::move(file), 0, size);
}

Bytes Slice::read(std::uint64_t offset, std::size_t size) const {
    if (offset > size_ || size > size_ - offset) throw FormatError("read past end of slice");
    Bytes out(size);
    if (size) file_->read(offset_ + offset, out.data(), size);
    return out;
}

Bytes Slice::readAll() const { return read(0, static_cast<std::size_t>(size_)); }

Slice Slice::sub(std::uint64_t offset, std::uint64_t size) const {
    if (offset > size_ || size > size_ - offset) throw FormatError("sub-slice out of bounds");
    return Slice(file_, offset_ + offset, size);
}

void Slice::copyTo(const std::filesystem::path& dst) const {
    std::ofstream out(dst, std::ios::binary);
    if (!out) throw std::runtime_error("cannot create " + dst.string());
    constexpr std::size_t kChunk = 8 << 20;
    Bytes buf;
    for (std::uint64_t done = 0; done < size_;) {
        const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(kChunk, size_ - done));
        buf.resize(n);
        file_->read(offset_ + done, buf.data(), n);
        out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(n));
        done += n;
    }
    if (!out) throw std::runtime_error("write error on " + dst.string());
}

} // namespace imbk
