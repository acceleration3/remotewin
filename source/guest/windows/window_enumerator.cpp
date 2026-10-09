#include "guest/windows/window_enumerator.hpp"

#include "guest/windows/application_process.hpp"
#include "guest/windows/launch_scope.hpp"
#include "guest/windows/window_surface.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
#include <dwmapi.h>
// clang-format on

namespace rwin::guest {

namespace {

std::string text_of(HWND window, int (*get)(HWND, wchar_t*, int)) {
    wchar_t wide[512] = {};
    const int length = get(window, wide, 512);
    if (length <= 0) return {};
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide, length, nullptr, 0,
                                            nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string out(static_cast<std::size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, length, out.data(), bytes, nullptr, nullptr);
    return out;
}

int window_title(HWND window, wchar_t* into, int size) {
    return ::GetWindowTextW(window, into, size);
}

int window_class(HWND window, wchar_t* into, int size) {
    return ::GetClassNameW(window, into, size);
}

// Mapped, sized, visible — and still not drawn. DWM knows; nothing in
// user32 does.
bool is_cloaked(HWND window) {
    DWORD cloaked = 0;
    if (FAILED(::DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked,
                                       sizeof(cloaked)))) {
        return false;
    }
    return cloaked != 0;
}

bool worth_showing(HWND window) {
    if (::IsWindowVisible(window) == FALSE) return false;
    if (is_cloaked(window)) return false;
    // Tool windows are NOT skipped. Windows registers a menu as a tool
    // window, so dropping them dropped every menu an application opened —
    // and palettes and inspectors with them, which are just as much a
    // window as anything else here.
    RECT rect{};
    if (::GetWindowRect(window, &rect) == FALSE) return false;
    return rect.right > rect.left && rect.bottom > rect.top;
}

// Whether this is the window Windows itself opens to show a menu.
//
// Windows registers that window under the system class atom 32768 — the
// class every menu on the desktop is an instance of, named "#32768" in
// the documentation of the system classes. Read from the class table,
// which never enters another thread: asking the window with MN_GETHMENU
// did, and a thread in the middle of taking its menu down did not answer.
bool is_windows_menu(HWND window) {
    constexpr ULONG_PTR kMenuClassAtom = 32768;
    return ::GetClassLongPtrW(window, GCW_ATOM) == kMenuClassAtom;
}

HWND root_owner_of(HWND window) {
    HWND root = ::GetAncestor(window, GA_ROOTOWNER);
    return root != nullptr ? root : window;
}

// Who a window belongs to, as Windows keeps it.
//
// GW_OWNER answers for nearly everything. Two kinds of window it does not
// answer for are the two whose owner matters most, because they are
// placed against it: a menu, and a combo box's dropped list. Neither has
// an owner — the list is a child of the desktop — and the association
// lives in the thread's input state instead. GUITHREADINFO names the
// window whose menu is up while one is, and the window holding the mouse
// capture while the active window's list is dropped.
//
// That window is often not one anybody sees (an application with its own
// menu bar opens menus from an internal window), so the answer is taken
// up to its root owner, which is.
HWND owner_of(HWND window) {
    if (HWND owner = ::GetWindow(window, GW_OWNER); owner != nullptr) return owner;

    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);
    if (::GetGUIThreadInfo(::GetWindowThreadProcessId(window, nullptr), &gui) == FALSE) {
        return nullptr;
    }
    if (gui.hwndMenuOwner != nullptr && is_windows_menu(window)) {
        return root_owner_of(gui.hwndMenuOwner);
    }
    // A child window with no parent but the desktop: a combo box's
    // dropped list is made this way. A child is a part of something, and
    // the something is whatever its thread has active. The same goes for
    // a window holding the mouse capture while another is active — a
    // window capturing the mouse for itself is a drag, and is nobody's.
    const bool child_of_desktop = (::GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) != 0;
    if ((child_of_desktop || gui.hwndCapture == window) && gui.hwndActive != nullptr) {
        HWND root = root_owner_of(gui.hwndActive);
        if (root != window) {
            RWIN_DEBUG("owner", "{:#x} belongs to active root {:#x} (child={} capture={})",
                       reinterpret_cast<std::uintptr_t>(window),
                       reinterpret_cast<std::uintptr_t>(root), child_of_desktop,
                       gui.hwndCapture == window);
            return root;
        }
    }
    return nullptr;
}

bool dwm_draws_frame(HWND window) {
    BOOL enabled = FALSE;
    if (FAILED(::DwmGetWindowAttribute(window, DWMWA_NCRENDERING_ENABLED, &enabled,
                                       sizeof(enabled)))) {
        return false;
    }
    return enabled != FALSE;
}

// GetWindowBand is exported by user32 and not declared by any header:
// the z-order bands are Windows' own, undocumented, and how the shell
// keeps its own windows above every application's. Resolved once.
DWORD band_of(HWND window) {
    using GetWindowBandFn = BOOL(WINAPI*)(HWND, PDWORD);
    static const auto get_window_band = reinterpret_cast<GetWindowBandFn>(
        reinterpret_cast<void*>(::GetProcAddress(::GetModuleHandleW(L"user32.dll"),
                                                 "GetWindowBand")));
    DWORD band = 0;
    if (get_window_band == nullptr || get_window_band(window, &band) == FALSE) return 0;
    return band;
}

void read_layering(HWND window, GuestWindow& into) {
    COLORREF key = 0;
    BYTE alpha = 255;
    DWORD flags = 0;
    // Fails for a window that never called SetLayeredWindowAttributes,
    // including one drawn with UpdateLayeredWindow, whose alpha is in its
    // pixels already.
    if (::GetLayeredWindowAttributes(window, &key, &alpha, &flags) == FALSE) return;
    into.layered_flags = flags;
    into.layered_alpha = alpha;
    into.color_key = key;
}

// GetWindowRgn answers in the window rectangle's coordinates, and the
// rectangles are moved to the picture's.
void read_region(HWND window, const WindowSurface& surface, std::vector<WindowRect>& into) {
    // More rectangles than this and it is a mask no shape needs — an
    // antialiased edge scanned into rows. Sent whole, such a region is a
    // window list several times the size of every frame; the window is
    // shown as its rectangle instead.
    constexpr std::size_t kMaxRects = 256;

    HRGN region = ::CreateRectRgn(0, 0, 0, 0);
    if (region == nullptr) return;
    const int kind = ::GetWindowRgn(window, region);
    if (kind == ERROR || kind == NULLREGION) {
        ::DeleteObject(region);
        return;
    }
    const DWORD bytes = ::GetRegionData(region, 0, nullptr);
    if (bytes == 0 || bytes > sizeof(RGNDATAHEADER) + kMaxRects * sizeof(RECT)) {
        ::DeleteObject(region);
        return;
    }
    std::vector<std::uint8_t> raw(bytes);
    auto* data = reinterpret_cast<RGNDATA*>(raw.data());
    if (::GetRegionData(region, bytes, data) != 0) {
        const auto* rects = reinterpret_cast<const RECT*>(data->Buffer);
        into.reserve(data->rdh.nCount);
        for (DWORD i = 0; i < data->rdh.nCount; ++i) {
            into.push_back(WindowRect{rects[i].left - surface.inset_x(),
                                      rects[i].top - surface.inset_y(),
                                      rects[i].right - surface.inset_x(),
                                      rects[i].bottom - surface.inset_y()});
        }
    }
    ::DeleteObject(region);
}

// Whether a window of the scope owns ``window``, at any remove: a dialog
// Windows or a broker draws over an application's window in a process of
// its own - the passkey prompt (CredentialUIBroker.exe) over a browser's
// sign-in page - is that application's, and its owner is disabled until
// it is answered.
bool owned_in_scope(HWND window, const LaunchScope& scope) {
    for (HWND owner = ::GetWindow(window, GW_OWNER); owner != nullptr;
         owner = ::GetWindow(owner, GW_OWNER)) {
        if (scope.contains(application_process_of(reinterpret_cast<std::uintptr_t>(owner)))) {
            return true;
        }
    }
    return false;
}

struct Collector {
    std::vector<GuestWindow>* into;
    const LaunchScope* scope;
    HWND foreground;
};

BOOL CALLBACK collect(HWND window, LPARAM param) {
    auto* collector = reinterpret_cast<Collector*>(param);
    if (!worth_showing(window)) return TRUE;

    const std::uint32_t process_id =
        application_process_of(reinterpret_cast<std::uintptr_t>(window));
    if (!collector->scope->contains(process_id) && !owned_in_scope(window, *collector->scope)) {
        return TRUE;
    }

    GuestWindow found;
    found.handle = reinterpret_cast<std::uintptr_t>(window);
    const WindowSurface surface{found.handle};
    if (!surface.valid()) return TRUE;
    found.owner = reinterpret_cast<std::uintptr_t>(owner_of(window));
    found.process_id = process_id;
    found.x = surface.screen_x(0);
    found.y = surface.screen_y(0);
    found.width = surface.width();
    found.height = surface.height();
    found.style = static_cast<std::uint32_t>(::GetWindowLongPtrW(window, GWL_STYLE));
    found.extended_style =
        static_cast<std::uint32_t>(::GetWindowLongPtrW(window, GWL_EXSTYLE));
    found.class_style =
        static_cast<std::uint32_t>(::GetClassLongPtrW(window, GCL_STYLE));
    found.title = text_of(window, window_title);
    found.class_name = text_of(window, window_class);
    found.minimized = ::IsIconic(window) != FALSE;
    found.maximized = ::IsZoomed(window) != FALSE;
    found.dwm_frame = dwm_draws_frame(window);
    if ((found.extended_style & WS_EX_LAYERED) != 0) read_layering(window, found);
    found.band = band_of(window);
    found.foreground = window == collector->foreground;
    read_region(window, surface, found.region);
    collector->into->push_back(std::move(found));
    return TRUE;
}

}  // namespace

std::vector<GuestWindow> WindowEnumerator::windows_in(const LaunchScope& scope) const {
    std::vector<GuestWindow> found;
    Collector collector{&found, &scope, ::GetForegroundWindow()};
    ::EnumWindows(&collect, reinterpret_cast<LPARAM>(&collector));
    return found;
}

}  // namespace rwin::guest
