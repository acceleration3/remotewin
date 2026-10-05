#include "probe/window_reading.hpp"

// clang-format off
#include <windows.h>
#include <dwmapi.h>
// clang-format on

#include <memory>
#include <string>
#include <vector>

namespace rwin::probe {

namespace {

struct RegionDeleter {
    void operator()(HRGN__* region) const noexcept { ::DeleteObject(region); }
};
using OwnedRegion = std::unique_ptr<HRGN__, RegionDeleter>;

std::string narrow(const wchar_t* text, int length) {
    if (length <= 0) return {};
    const int bytes =
        ::WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, length, out.data(), bytes, nullptr, nullptr);
    return out;
}

WindowTruth::Rect rect_of(const RECT& rect) {
    return WindowTruth::Rect{rect.left, rect.top, rect.right, rect.bottom};
}

// The window rectangle, the visible frame within it, and the client area,
// all on the screen.
void read_rectangles(HWND window, WindowTruth& truth) {
    RECT rect{};
    ::GetWindowRect(window, &rect);
    truth.window = rect_of(rect);
    RECT bounds{};
    truth.visible = SUCCEEDED(::DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS,
                                                      &bounds, sizeof(bounds)))
                        ? rect_of(bounds)
                        : truth.window;
    // MapWindowPoints with the two corners of a RECT, because a
    // WS_EX_LAYOUTRTL window's client origin is its right edge: mapped as
    // a rectangle, left and right come back in screen order.
    RECT client{};
    ::GetClientRect(window, &client);
    ::MapWindowPoints(window, HWND_DESKTOP, reinterpret_cast<POINT*>(&client), 2);
    truth.client = rect_of(client);
}

// Fails, and leaves nothing set, for a window drawn with
// UpdateLayeredWindow: its alpha is in its pixels, not in attributes.
void read_layering(HWND window, WindowTruth& truth) {
    COLORREF key = 0;
    BYTE alpha = 255;
    DWORD flags = 0;
    if (::GetLayeredWindowAttributes(window, &key, &alpha, &flags) == FALSE) return;
    truth.layered_flags = flags;
    truth.layered_alpha = alpha;
    truth.color_key = key;
}

void read_region(HWND window, WindowTruth& truth) {
    const OwnedRegion region{::CreateRectRgn(0, 0, 0, 0)};
    const int kind = ::GetWindowRgn(window, region.get());
    if (kind != SIMPLEREGION && kind != COMPLEXREGION) return;
    truth.region = kind == SIMPLEREGION ? WindowTruth::RegionKind::Simple
                                        : WindowTruth::RegionKind::Complex;
    RECT box{};
    ::GetRgnBox(region.get(), &box);
    truth.region_box = rect_of(box);
    const DWORD bytes = ::GetRegionData(region.get(), 0, nullptr);
    if (bytes < sizeof(RGNDATAHEADER)) return;
    std::vector<std::uint8_t> raw(bytes);
    auto* described = reinterpret_cast<RGNDATA*>(raw.data());
    if (::GetRegionData(region.get(), bytes, described) != 0) {
        truth.region_rects = described->rdh.nCount;
    }
}

std::string text_of(HWND window, int (*get)(HWND, wchar_t*, int)) {
    wchar_t text[256] = {};
    return narrow(text, get(window, text, 256));
}

int window_title(HWND window, wchar_t* into, int size) { return ::GetWindowTextW(window, into, size); }

int window_class(HWND window, wchar_t* into, int size) { return ::GetClassNameW(window, into, size); }

bool owner_chain_disabled(HWND window) {
    for (HWND owner = ::GetWindow(window, GW_OWNER); owner != nullptr;
         owner = ::GetWindow(owner, GW_OWNER)) {
        if (::IsWindowEnabled(owner) == FALSE) return true;
    }
    return false;
}

// The thread's activation and focus, as they touch this window.
void read_activation(HWND window, WindowTruth& truth) {
    truth.foreground = ::GetForegroundWindow() == window;
    truth.active = ::GetActiveWindow() == window;
    HWND focus = ::GetFocus();
    truth.focused = focus != nullptr && (focus == window || ::IsChild(window, focus) != FALSE);
    truth.owner_disabled = owner_chain_disabled(window);
    HWND popup = ::GetWindow(window, GW_ENABLEDPOPUP);
    truth.enabled_popup = popup == window ? 0 : reinterpret_cast<std::uintptr_t>(popup);
}

}  // namespace

WindowTruth read_window(HWND window) {
    WindowTruth truth;
    truth.handle = reinterpret_cast<std::uintptr_t>(window);
    truth.exists = ::IsWindow(window) != FALSE;
    if (!truth.exists) return truth;
    truth.owner = reinterpret_cast<std::uintptr_t>(::GetWindow(window, GW_OWNER));
    DWORD process_id = 0;
    ::GetWindowThreadProcessId(window, &process_id);
    truth.process_id = process_id;
    truth.style = static_cast<std::uint32_t>(::GetWindowLongPtrW(window, GWL_STYLE));
    truth.extended_style = static_cast<std::uint32_t>(::GetWindowLongPtrW(window, GWL_EXSTYLE));
    truth.class_style = static_cast<std::uint32_t>(::GetClassLongPtrW(window, GCL_STYLE));
    truth.title = text_of(window, window_title);
    truth.class_name = text_of(window, window_class);

    read_rectangles(window, truth);

    truth.shown = ::IsWindowVisible(window) != FALSE;
    truth.iconic = ::IsIconic(window) != FALSE;
    truth.zoomed = ::IsZoomed(window) != FALSE;
    truth.enabled = ::IsWindowEnabled(window) != FALSE;
    read_activation(window, truth);
    BOOL framed = FALSE;
    truth.dwm_frame = SUCCEEDED(::DwmGetWindowAttribute(window, DWMWA_NCRENDERING_ENABLED,
                                                        &framed, sizeof(framed)))
                      && framed != FALSE;
    truth.dpi = ::GetDpiForWindow(window);

    if ((truth.extended_style & WS_EX_LAYERED) != 0) read_layering(window, truth);
    read_region(window, truth);
    return truth;
}

}  // namespace rwin::probe
