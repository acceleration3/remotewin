#include "guest/cursor/cursor_decoder.hpp"

#include "guest/cursor/cursor_image.hpp"
#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {

// GetCursorFrameInfo's rate is in jiffies, sixtieths of a second.
constexpr std::uint32_t kJiffiesPerSecond = 60;
constexpr std::uint32_t kMsPerSecond = 1000;

}  // namespace

CursorDecoder::CursorDecoder() {
    std::size_t next = 0;
    for (auto cursor = static_cast<int>(SystemCursor::Arrow); cursor <= static_cast<int>(SystemCursor::Person);
         ++cursor) {
        const auto which = static_cast<SystemCursor>(cursor);
        HCURSOR shared = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(idc_of(which)));
        if (shared != nullptr && next < system_.size()) system_[next++] = {shared, which};
    }
    if (HMODULE user = ::GetModuleHandleW(L"user32.dll"); user != nullptr) {
        frame_info_ = reinterpret_cast<FrameInfo>(
            reinterpret_cast<void*>(::GetProcAddress(user, "GetCursorFrameInfo")));
    }
    if (frame_info_ == nullptr) RWIN_WARN("cursor", "no GetCursorFrameInfo: animated cursors show their first step");
}

SystemCursor CursorDecoder::system_cursor_of(void* cursor_handle) const noexcept {
    for (const auto& [handle, which] : system_) {
        if (handle != nullptr && handle == cursor_handle) return which;
    }
    return SystemCursor::None;
}

std::optional<CursorPicture> CursorDecoder::decode(void* cursor_handle) const {
    CursorPicture picture;
    picture.id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(cursor_handle));
    picture.system = system_cursor_of(cursor_handle);
    std::uint32_t rate = 0;
    std::uint32_t steps = 1;
    if (frame_info_ != nullptr && frame_info_(cursor_handle, 0, 0, &rate, &steps) == nullptr) steps = 1;
    if (steps == 0 || steps > CursorPicture::kMaxFrames) steps = 1;
    picture.frames.reserve(steps);
    for (std::uint32_t step = 0; step < steps; ++step) {
        void* frame = cursor_handle;
        rate = 0;
        if (steps > 1) {
            std::uint32_t count = 0;
            frame = frame_info_(cursor_handle, 0, step, &rate, &count);
        }
        CursorImage image{frame};
        if (!image.valid()) break;
        picture.inverts = picture.inverts || image.inverts();
        picture.frames.push_back(CursorPicture::Frame{image.width(), image.height(), image.hotspot_x(),
                                                      image.hotspot_y(),
                                                      steps > 1 ? rate * kMsPerSecond / kJiffiesPerSecond : 0,
                                                      image.take_pixels()});
    }
    if (picture.frames.empty()) return std::nullopt;
    return picture;
}

}  // namespace rwin::guest
