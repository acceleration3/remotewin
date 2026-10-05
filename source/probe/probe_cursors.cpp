#include "probe/probe_cursors.hpp"

#include "logging/logger.hpp"
#include "probe/ani_file.hpp"
#include "proto/probe/probe_action.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <vector>

namespace rwin::probe {

namespace {

constexpr std::uint32_t kColourSide = 32;
constexpr std::uint32_t kColourHotspot = 4;
constexpr std::uint32_t kMonochromeSide = 32;
constexpr std::uint32_t kMonochromeHotspot = 16;
constexpr std::uint32_t kFirstSystemCursor = 32512;
constexpr std::array<std::uint32_t, ProbeCursors::kAnimatedSteps> kStepJiffies{6, 12, 6, 30};
constexpr std::array<std::uint32_t, ProbeCursors::kAnimatedSteps> kStepColours{0xFFE03030, 0xFF30C040, 0xFF3060E0,
                                                                               0xFFD040D0};

struct BitmapDeleter {
    void operator()(HBITMAP__* bitmap) const noexcept { ::DeleteObject(bitmap); }
};
using OwnedBitmap = std::unique_ptr<HBITMAP__, BitmapDeleter>;

void put(std::vector<std::uint8_t>& bgra, std::size_t at, std::uint32_t argb) {
    bgra[at * 4 + 0] = static_cast<std::uint8_t>(argb);
    bgra[at * 4 + 1] = static_cast<std::uint8_t>(argb >> 8);
    bgra[at * 4 + 2] = static_cast<std::uint8_t>(argb >> 16);
    bgra[at * 4 + 3] = static_cast<std::uint8_t>(argb >> 24);
}

// A disc in four colours with a translucent ring and a soft edge: alpha
// that is neither 0 nor 255 is what a colour cursor has and a monochrome
// one cannot.
std::vector<std::uint8_t> disc(std::uint32_t side) {
    std::vector<std::uint8_t> bgra(static_cast<std::size_t>(side) * side * 4, 0);
    const double middle = side / 2.0;
    const double radius = side * 0.45;
    for (std::uint32_t y = 0; y < side; ++y) {
        for (std::uint32_t x = 0; x < side; ++x) {
            const double distance = std::hypot(x + 0.5 - middle, y + 0.5 - middle);
            if (distance > radius) continue;
            const bool right = x >= side / 2;
            const bool lower = y >= side / 2;
            std::uint32_t rgb = right ? (lower ? 0xF0D020 : 0x2060F0) : (lower ? 0x20B040 : 0xE02020);
            const std::uint32_t alpha = distance > radius - 1.5 ? 0x40 : distance > radius * 0.75 ? 0x90 : 0xFF;
            put(bgra, static_cast<std::size_t>(y) * side + x, (alpha << 24) | rgb);
        }
    }
    return bgra;
}

// A square of one colour with a white bar that moves a quarter along at
// each step, so every step differs from the others in pixels too.
std::vector<std::uint8_t> step_picture(std::uint32_t step) {
    const std::uint32_t side = ProbeCursors::kAnimatedSide;
    std::vector<std::uint8_t> bgra(static_cast<std::size_t>(side) * side * 4, 0);
    for (std::uint32_t y = 2; y < side - 2; ++y) {
        for (std::uint32_t x = 2; x < side - 2; ++x) {
            const bool bar = x / 7 == step + 1;
            put(bgra, static_cast<std::size_t>(y) * side + x, bar ? 0xFFFFFFFF : kStepColours[step]);
        }
    }
    return bgra;
}

HCURSOR cursor_from(const std::vector<std::uint8_t>& bgra, std::uint32_t side, std::uint32_t hotspot) {
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = static_cast<LONG>(side);
    header.bV5Height = -static_cast<LONG>(side);
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC screen = ::GetDC(nullptr);
    const OwnedBitmap colour{::CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits,
                                                nullptr, 0)};
    ::ReleaseDC(nullptr, screen);
    if (colour == nullptr || bits == nullptr) return nullptr;
    std::memcpy(bits, bgra.data(), bgra.size());
    const OwnedBitmap mask{::CreateBitmap(static_cast<int>(side), static_cast<int>(side), 1, 1, nullptr)};
    ICONINFO described{FALSE, hotspot, hotspot, mask.get(), colour.get()};
    return ::CreateIconIndirect(&described);
}

}  // namespace

ProbeCursors::ProbeCursors()
    : colour_(make_colour(kColourSide, kColourHotspot)),
      monochrome_(make_monochrome()),
      animated_(load_animated(animated_failure_)),
      large_(make_colour(kLargeSide, kLargeSide / 2)) {}

ProbeCursors::~ProbeCursors() {
    for (HCURSOR cursor : {colour_, monochrome_, animated_, large_}) {
        if (cursor != nullptr) ::DestroyCursor(cursor);
    }
}

HCURSOR ProbeCursors::make_colour(std::uint32_t side, std::uint32_t hotspot) {
    return cursor_from(disc(side), side, hotspot);
}

// AND 1 XOR 0 transparent, AND 0 XOR 0 black, AND 0 XOR 1 white, AND 1
// XOR 1 inverted: a black frame, a white square, a middle that inverts.
HCURSOR ProbeCursors::make_monochrome() {
    constexpr std::uint32_t kRow = kMonochromeSide / 8;
    std::array<std::uint8_t, kRow * kMonochromeSide> and_plane{};
    std::array<std::uint8_t, kRow * kMonochromeSide> xor_plane{};
    const auto set = [](auto& plane, std::uint32_t x, std::uint32_t y) {
        plane[y * kRow + x / 8] |= static_cast<std::uint8_t>(0x80 >> (x % 8));
    };
    for (std::uint32_t y = 0; y < kMonochromeSide; ++y) {
        for (std::uint32_t x = 0; x < kMonochromeSide; ++x) {
            const bool outside = x < 4 || y < 4 || x >= kMonochromeSide - 4 || y >= kMonochromeSide - 4;
            const bool frame = !outside && (x < 6 || y < 6 || x >= kMonochromeSide - 6 || y >= kMonochromeSide - 6);
            const bool middle = x >= 10 && y >= 10 && x < kMonochromeSide - 10 && y < kMonochromeSide - 10;
            if (outside || middle) set(and_plane, x, y);
            if (middle || (!outside && !frame)) set(xor_plane, x, y);
        }
    }
    return ::CreateCursor(::GetModuleHandleW(nullptr), kMonochromeHotspot, kMonochromeHotspot, kMonochromeSide,
                          kMonochromeSide, and_plane.data(), xor_plane.data());
}

HCURSOR ProbeCursors::load_animated(std::string& failure) {
    AniFile file;
    for (std::uint32_t step = 0; step < kAnimatedSteps; ++step) {
        file.add(AniFile::Frame{kAnimatedSide, 1, 1, kStepJiffies[step], step_picture(step)});
    }
    wchar_t folder[MAX_PATH] = {};
    ::GetTempPathW(MAX_PATH, folder);
    const std::wstring path = std::wstring{folder} + L"rwin-probe.ani";
    if (!file.write(path, failure)) return nullptr;
    // LR_LOADFROMFILE at size 0: the file's own size, as LoadCursorFromFile
    // would give it.
    auto* cursor = static_cast<HCURSOR>(::LoadImageW(nullptr, path.c_str(), IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE));
    if (cursor == nullptr) failure = std::format("LoadImage of the animated cursor failed: error {}", ::GetLastError());
    return cursor;
}

std::optional<HCURSOR> ProbeCursors::cursor_for(std::uint32_t code, std::string& failure) const {
    using Made = ProbeAction::MadeCursor;
    if (code >= kFirstSystemCursor) {
        HCURSOR shared = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(code));
        if (shared == nullptr) failure = std::format("LoadCursor({}) failed: error {}", code, ::GetLastError());
        return shared == nullptr ? std::nullopt : std::optional<HCURSOR>{shared};
    }
    HCURSOR made = nullptr;
    switch (static_cast<Made>(code)) {
        case Made::None:
            return std::optional<HCURSOR>{nullptr};
        case Made::Colour:
            made = colour_;
            break;
        case Made::Monochrome:
            made = monochrome_;
            break;
        case Made::Animated:
            made = animated_;
            if (made == nullptr) failure = animated_failure_;
            break;
        case Made::Large:
            made = large_;
            break;
    }
    if (made == nullptr && failure.empty()) failure = std::format("no cursor {}", code);
    return made == nullptr ? std::nullopt : std::optional<HCURSOR>{made};
}

}  // namespace rwin::probe
