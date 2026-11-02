#include "probe/ani_file.hpp"

#include <cstdio>
#include <format>
#include <memory>
#include <utility>

namespace rwin::probe {

namespace {

constexpr std::uint32_t kBytesPerPixel = 4;
constexpr std::uint16_t kCursorType = 2;
constexpr std::uint32_t kIconDirSize = 6 + 16;
constexpr std::uint32_t kBitmapHeaderSize = 40;
constexpr std::uint32_t kAniHeaderSize = 36;
constexpr std::uint32_t kAfIcon = 1;

class Bytes {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value));
        u8(static_cast<std::uint8_t>(value >> 8));
    }
    void u32(std::uint32_t value) {
        u16(static_cast<std::uint16_t>(value));
        u16(static_cast<std::uint16_t>(value >> 16));
    }
    void tag(const char (&name)[5]) {
        for (int i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>(name[i]));
    }
    void append(const std::vector<std::uint8_t>& more) { bytes_.insert(bytes_.end(), more.begin(), more.end()); }
    void chunk(const char (&name)[5], const std::vector<std::uint8_t>& body) {
        tag(name);
        u32(static_cast<std::uint32_t>(body.size()));
        append(body);
        if (body.size() % 2 != 0) u8(0);
    }
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

struct FileCloser {
    void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};

}

void AniFile::add(Frame frame) { frames_.push_back(std::move(frame)); }

std::vector<std::uint8_t> AniFile::cur_bytes(const Frame& frame) {
    const std::uint32_t mask_row = ((frame.side + 31) / 32) * 4;
    const std::uint32_t colour_size = frame.side * frame.side * kBytesPerPixel;
    const std::uint32_t image_size = kBitmapHeaderSize + colour_size + mask_row * frame.side;
    Bytes out;
    out.u16(0);
    out.u16(kCursorType);
    out.u16(1);
    out.u8(static_cast<std::uint8_t>(frame.side));
    out.u8(static_cast<std::uint8_t>(frame.side));
    out.u8(0);
    out.u8(0);
    out.u16(static_cast<std::uint16_t>(frame.hotspot_x));
    out.u16(static_cast<std::uint16_t>(frame.hotspot_y));
    out.u32(image_size);
    out.u32(kIconDirSize);
    out.u32(kBitmapHeaderSize);
    out.u32(frame.side);
    out.u32(frame.side * 2);
    out.u16(1);
    out.u16(32);
    for (int i = 0; i < 6; ++i) out.u32(0);
    for (std::uint32_t row = frame.side; row-- > 0;) {
        const auto* from = frame.bgra.data() + static_cast<std::size_t>(row) * frame.side * kBytesPerPixel;
        for (std::uint32_t at = 0; at < frame.side * kBytesPerPixel; ++at) out.u8(from[at]);
    }
    for (std::uint32_t at = 0; at < mask_row * frame.side; ++at) out.u8(0);
    return out.bytes();
}

bool AniFile::write(const std::wstring& path, std::string& failure) const {
    Bytes header;
    header.u32(kAniHeaderSize);
    header.u32(static_cast<std::uint32_t>(frames_.size()));
    header.u32(static_cast<std::uint32_t>(frames_.size()));
    for (int i = 0; i < 4; ++i) header.u32(0);
    header.u32(frames_.empty() ? 0 : frames_.front().jiffies);
    header.u32(kAfIcon);
    Bytes rates;
    for (const Frame& frame : frames_) rates.u32(frame.jiffies);
    Bytes list;
    list.tag("fram");
    for (const Frame& frame : frames_) list.chunk("icon", cur_bytes(frame));

    Bytes body;
    body.tag("ACON");
    body.chunk("anih", header.bytes());
    body.chunk("rate", rates.bytes());
    body.chunk("LIST", list.bytes());
    Bytes file;
    file.chunk("RIFF", body.bytes());

    const std::unique_ptr<std::FILE, FileCloser> out{::_wfopen(path.c_str(), L"wb")};
    if (out == nullptr || std::fwrite(file.bytes().data(), 1, file.bytes().size(), out.get()) != file.bytes().size()) {
        failure = std::format("cannot write the animated cursor file ({} bytes)", file.bytes().size());
        return false;
    }
    return true;
}

}
