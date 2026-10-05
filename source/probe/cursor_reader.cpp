#include "probe/cursor_reader.hpp"

#include "logging/logger.hpp"
#include "probe/pointer_actions.hpp"
#include "proto/system_cursor.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <memory>
#include <vector>

namespace rwin::probe {

namespace {

constexpr std::uint32_t kJiffiesPerSecond = 60;
constexpr std::uint32_t kMsPerSecond = 1000;
constexpr std::uint32_t kMaxSteps = 512;
constexpr USHORT kGenericDesktop = 1;
constexpr USHORT kMouseUsage = 2;
constexpr std::uint32_t kWhite = 0x00FFFFFF;
constexpr std::uint32_t kBlack = 0x00000000;

struct BitmapDeleter {
    void operator()(HBITMAP__* bitmap) const noexcept { ::DeleteObject(bitmap); }
};
using OwnedBitmap = std::unique_ptr<HBITMAP__, BitmapDeleter>;

struct DcDeleter {
    void operator()(HDC__* dc) const noexcept { ::DeleteDC(dc); }
};
using OwnedDc = std::unique_ptr<HDC__, DcDeleter>;

std::uint64_t as_handle(const void* pointer) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

// The cursor drawn as Windows draws it on screen - alpha blended, the
// monochrome AND and XOR applied - over one flat colour.
std::vector<std::uint8_t> drawn_over(HCURSOR cursor, std::uint32_t step, std::uint32_t width, std::uint32_t height,
                                     std::uint32_t background) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    const OwnedDc dc{::CreateCompatibleDC(nullptr)};
    const OwnedBitmap target{::CreateDIBSection(dc.get(), &info, DIB_RGB_COLORS, &bits, nullptr, 0)};
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    if (dc == nullptr || target == nullptr || bits == nullptr) return pixels;
    auto* words = static_cast<std::uint32_t*>(bits);
    for (std::size_t at = 0; at < static_cast<std::size_t>(width) * height; ++at) words[at] = background;
    HGDIOBJ previous = ::SelectObject(dc.get(), target.get());
    if (::DrawIconEx(dc.get(), 0, 0, cursor, static_cast<int>(width), static_cast<int>(height), step, nullptr,
                     DI_NORMAL)
        == FALSE) {
        RWIN_WARN("cursor", "DrawIconEx of {:#x} step {} failed: error {}", as_handle(cursor), step, ::GetLastError());
    }
    ::GdiFlush();
    ::SelectObject(dc.get(), previous);
    const auto* from = static_cast<const std::uint8_t*>(bits);
    pixels.assign(from, from + pixels.size());
    for (std::size_t at = 3; at < pixels.size(); at += 4) pixels[at] = 0xFF;
    return pixels;
}

}  // namespace

CursorReader::CursorReader(const PointerActions& pointer) : pointer_(pointer) {
    std::size_t next = 0;
    for (auto cursor = static_cast<int>(SystemCursor::Arrow); cursor <= static_cast<int>(SystemCursor::Person);
         ++cursor) {
        const std::uint32_t idc = idc_of(static_cast<SystemCursor>(cursor));
        HCURSOR shared = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(idc));
        if (shared != nullptr && next < system_.size()) system_[next++] = {shared, idc};
    }
    if (HMODULE user = ::GetModuleHandleW(L"user32.dll"); user != nullptr) {
        frame_info_ = reinterpret_cast<FrameInfo>(
            reinterpret_cast<void*>(::GetProcAddress(user, "GetCursorFrameInfo")));
    }
}

std::uint32_t CursorReader::system_idc_of(HCURSOR cursor) const noexcept {
    for (const auto& [handle, idc] : system_) {
        if (handle != nullptr && handle == cursor) return idc;
    }
    return 0;
}

CursorTruth CursorReader::read() const {
    CursorTruth truth;
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (::GetCursorInfo(&info) != FALSE) {
        truth.flags = info.flags;
        truth.handle = as_handle(info.hCursor);
        truth.x = info.ptScreenPos.x;
        truth.y = info.ptScreenPos.y;
        truth.under = as_handle(::WindowFromPoint(info.ptScreenPos));
        truth.system_idc = system_idc_of(info.hCursor);
    }
    truth.display_count = pointer_.display_count();
    truth.recentring = pointer_.recentring();
    truth.put_back_by = pointer_.put_back_by();
    truth.put_back = pointer_.put_back();
    RECT clip{};
    ::GetClipCursor(&clip);
    truth.clip = {clip.left, clip.top, clip.right, clip.bottom};
    const int left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    truth.clipped = clip.left > left || clip.top > top
                    || clip.right < left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN)
                    || clip.bottom < top + ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    truth.capture = as_handle(::GetCapture());
    read_pointer_settings(truth);
    if (info.hCursor != nullptr) read_frames(info.hCursor, truth);
    return truth;
}

void CursorReader::read_pointer_settings(CursorTruth& truth) {
    UINT count = 0;
    ::GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE));
    std::vector<RAWINPUTDEVICE> devices(count);
    if (count != 0 && ::GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE)) != static_cast<UINT>(-1)) {
        for (const RAWINPUTDEVICE& device : devices) {
            if (device.usUsagePage != kGenericDesktop || device.usUsage != kMouseUsage) continue;
            truth.raw_mouse = true;
            truth.raw_flags = device.dwFlags;
            truth.raw_target = as_handle(device.hwndTarget);
        }
    }
    int speed = 0;
    ::SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &speed, 0);
    truth.mouse_speed = speed;
    int mouse[3] = {};
    ::SystemParametersInfoW(SPI_GETMOUSE, 0, mouse, 0);
    truth.enhance_precision = mouse[2] != 0;
    truth.metric_width = static_cast<std::uint32_t>(::GetSystemMetrics(SM_CXCURSOR));
    truth.metric_height = static_cast<std::uint32_t>(::GetSystemMetrics(SM_CYCURSOR));
}

// An animated cursor is drawn a step at a time (DrawIconEx's step); each
// step's own picture, from GetCursorFrameInfo, says its size and hotspot.
void CursorReader::read_frames(HCURSOR cursor, CursorTruth& truth) const {
    std::uint32_t jiffies = 0;
    std::uint32_t steps = 1;
    if (frame_info_ == nullptr || frame_info_(cursor, 0, 0, &jiffies, &steps) == nullptr || steps == 0
        || steps > kMaxSteps) {
        steps = 1;
    }
    for (std::uint32_t step = 0; step < steps; ++step) {
        HCURSOR picture = cursor;
        jiffies = 0;
        if (steps > 1) {
            std::uint32_t ignored = 0;
            picture = static_cast<HCURSOR>(frame_info_(cursor, 0, step, &jiffies, &ignored));
        }
        ICONINFO icon{};
        if (picture == nullptr || ::GetIconInfo(picture, &icon) == FALSE) break;
        BITMAP described{};
        ::GetObjectW(icon.hbmColor != nullptr ? icon.hbmColor : icon.hbmMask, sizeof(described), &described);
        const auto width = static_cast<std::uint32_t>(described.bmWidth);
        const auto height =
            static_cast<std::uint32_t>(icon.hbmColor != nullptr ? described.bmHeight : described.bmHeight / 2);
        if (icon.hbmColor != nullptr) ::DeleteObject(icon.hbmColor);
        if (icon.hbmMask != nullptr) ::DeleteObject(icon.hbmMask);
        truth.frames.push_back(CursorTruth::Frame{width, height, icon.xHotspot, icon.yHotspot,
                                                  steps > 1 ? jiffies * kMsPerSecond / kJiffiesPerSecond : 0,
                                                  drawn_over(cursor, step, width, height, kWhite),
                                                  drawn_over(cursor, step, width, height, kBlack)});
    }
}

}  // namespace rwin::probe
