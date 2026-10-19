#include "guest/windows/window_reader.hpp"

#include "guest/windows/app_id_resolver.hpp"
#include "guest/windows/desktop_settings.hpp"
#include "guest/windows/shell_views.hpp"
#include "guest/windows/wide_text.hpp"
#include "guest/windows/window_directory.hpp"
#include "guest/windows/window_surface.hpp"

// clang-format off
#include <windows.h>
#include <dwmapi.h>
// clang-format on

#include <algorithm>
#include <array>
#include <cwchar>
#include <iterator>
#include <string_view>
#include <vector>

namespace rwin::guest {

namespace {

// The Explorer shell's own windows, which a full desktop has and RAIL's
// session never did: the desktop, its wallpaper workers, the taskbars.
constexpr std::array<std::wstring_view, 4> kShellClasses{L"Progman", L"WorkerW", L"Shell_TrayWnd",
                                                         L"Shell_SecondaryTrayWnd"};
// The guest half's own windows, in this and any other guest half's process.
constexpr std::wstring_view kOwnClassPrefix = L"rwin-guest-";
// Office's frame-shadow windows, which RAIL keeps next to their neighbours.
constexpr std::wstring_view kOfficeBorderClass = L"MSO_BORDEREFFECT_WINDOW_CLASS";
constexpr ULONG_PTR kMenuClassAtom = 32768;
constexpr std::wstring_view kMenuClass = L"#32768";
constexpr std::size_t kClassChars = 256;
constexpr std::size_t kTitleChars = 1024;

std::wstring class_of(HWND window) {
    std::array<wchar_t, kClassChars> name{};
    const int length = ::GetClassNameW(window, name.data(), static_cast<int>(name.size()));
    return length > 0 ? std::wstring(name.data(), static_cast<std::size_t>(length)) : std::wstring();
}

bool cloaked(HWND window) {
    DWORD state = 0;
    return SUCCEEDED(::DwmGetWindowAttribute(window, DWMWA_CLOAKED, &state, sizeof(state))) && state != 0;
}

bool dwm_draws_frame(HWND window) {
    BOOL enabled = FALSE;
    return SUCCEEDED(::DwmGetWindowAttribute(window, DWMWA_NCRENDERING_ENABLED, &enabled, sizeof(enabled)))
           && enabled != FALSE;
}

// GetWindowBand is exported by user32 and declared by no header: the
// z-order bands are Windows' own, and how the shell keeps its windows above
// every application's. Resolved once.
std::uint32_t band_of(HWND window) {
    using GetWindowBandFn = BOOL(WINAPI*)(HWND, PDWORD);
    static const auto get_window_band = reinterpret_cast<GetWindowBandFn>(
        reinterpret_cast<void*>(::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetWindowBand")));
    DWORD band = 1;
    if (get_window_band == nullptr || get_window_band(window, &band) == FALSE) return 1;
    return band;
}

WindowFields::Layering layering_of(HWND window) {
    WindowFields::Layering layering;
    COLORREF key = 0;
    BYTE alpha = 255;
    DWORD flags = 0;
    // Fails for a window that never called SetLayeredWindowAttributes,
    // including one drawn with UpdateLayeredWindow, whose alpha is in its
    // pixels already.
    if (::GetLayeredWindowAttributes(window, &key, &alpha, &flags) == FALSE) return layering;
    layering.flags = flags;
    layering.alpha = alpha;
    layering.color_key = key;
    return layering;
}

// GetWindowRgn answers in the window rectangle's coordinates; the
// rectangles go relative to the frame.
std::vector<Rect> region_of(HWND window, std::int32_t inset_x, std::int32_t inset_y) {
    std::vector<Rect> rects;
    HRGN region = ::CreateRectRgn(0, 0, 0, 0);
    if (region == nullptr) return rects;
    const int kind = ::GetWindowRgn(window, region);
    const DWORD bytes = kind == ERROR || kind == NULLREGION ? 0 : ::GetRegionData(region, 0, nullptr);
    if (bytes != 0 && bytes <= sizeof(RGNDATAHEADER) + WindowFields::kMaxRegionRects * sizeof(RECT)) {
        std::vector<std::uint8_t> raw(bytes);
        auto* data = reinterpret_cast<RGNDATA*>(raw.data());
        if (::GetRegionData(region, bytes, data) != 0) {
            const auto* each = reinterpret_cast<const RECT*>(data->Buffer);
            rects.reserve(data->rdh.nCount);
            for (DWORD i = 0; i < data->rdh.nCount; ++i) {
                rects.push_back(Rect{each[i].left - inset_x, each[i].top - inset_y,
                                     static_cast<std::uint32_t>(each[i].right - each[i].left),
                                     static_cast<std::uint32_t>(each[i].bottom - each[i].top)});
            }
        }
    }
    ::DeleteObject(region);
    return rects;
}

HWND root_owner_of(HWND window) {
    HWND root = ::GetAncestor(window, GA_ROOTOWNER);
    return root != nullptr ? root : window;
}

bool owner_hidden(HWND owner) {
    if (::IsWindowVisible(owner) == FALSE) return true;
    RECT rect{};
    if (::GetWindowRect(owner, &rect) != FALSE && rect.left == 0 && rect.top == 0 && rect.right == 0
        && rect.bottom == 0) {
        return true;
    }
    if (::IsIconic(owner) == FALSE) return false;
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    return ::GetWindowPlacement(owner, &placement) != FALSE && ::IsRectEmpty(&placement.rcNormalPosition) != FALSE;
}

// The shell's own helpers - the snap bar shown during a drag, thumbnail
// and DWM listeners, the edge input strips - are tool windows of the
// process that owns the desktop's shell window. File Explorer's windows
// share that process and are no tool windows; its menus are, and stay.
bool shell_tool_window(HWND window, DWORD process, std::wstring_view name) {
    if (name == kMenuClass) return false;
    if ((::GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) == 0) return false;
    DWORD shell = 0;
    ::GetWindowThreadProcessId(::GetShellWindow(), &shell);
    return shell != 0 && process == shell;
}

Rect frame_of_surface(HWND window, const WindowSurface& surface) {
    if (surface.valid()) {
        return Rect{surface.screen_x(0), surface.screen_y(0), surface.width(), surface.height()};
    }
    RECT rect{};
    ::GetWindowRect(window, &rect);
    return Rect{rect.left, rect.top, 0, 0};
}

// A maximised window overhangs its monitor by its border; RAIL cuts the
// overhang away.
Rect clipped_to_monitor(HWND window, Rect frame) {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (::GetMonitorInfoW(::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) == FALSE) return frame;
    const RECT bounds = monitor.rcMonitor;
    const std::int32_t left = std::max<std::int32_t>(frame.x, bounds.left);
    const std::int32_t top = std::max<std::int32_t>(frame.y, bounds.top);
    const std::int32_t right = std::min<std::int32_t>(frame.x + static_cast<std::int32_t>(frame.width), bounds.right);
    const std::int32_t bottom =
        std::min<std::int32_t>(frame.y + static_cast<std::int32_t>(frame.height), bounds.bottom);
    if (right <= left || bottom <= top) return frame;
    return Rect{left, top, static_cast<std::uint32_t>(right - left), static_cast<std::uint32_t>(bottom - top)};
}

Rect client_of(HWND window, const Rect& frame) {
    RECT client{};
    POINT corner{0, 0};
    if (::GetClientRect(window, &client) == FALSE || ::ClientToScreen(window, &corner) == FALSE) return Rect{};
    // A maximised window's client overhangs the frame its monitor clipped.
    const std::int32_t x = std::clamp<std::int32_t>(corner.x - frame.x, 0, static_cast<std::int32_t>(frame.width));
    const std::int32_t y = std::clamp<std::int32_t>(corner.y - frame.y, 0, static_cast<std::int32_t>(frame.height));
    return Rect{x, y, std::min(static_cast<std::uint32_t>(client.right), frame.width - static_cast<std::uint32_t>(x)),
                std::min(static_cast<std::uint32_t>(client.bottom), frame.height - static_cast<std::uint32_t>(y))};
}

std::uint16_t margin(std::int32_t pixels) {
    return static_cast<std::uint16_t>(std::clamp<std::int32_t>(pixels, 0, 0xFFFF));
}

}  // namespace

bool WindowReader::effectively_visible(void* window) {
    const auto hwnd = static_cast<HWND>(window);
    if (::IsWindowVisible(hwnd) != FALSE) return true;
    const ATOM redraw = ::GlobalFindAtomW(L"SysSetRedraw");
    return redraw != 0 && ::GetPropW(hwnd, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(redraw))) != nullptr;
}

bool WindowReader::top_level(void* window) {
    const auto hwnd = static_cast<HWND>(window);
    if (::IsWindow(hwnd) == FALSE) return false;
    if (::GetAncestor(hwnd, GA_PARENT) == ::GetDesktopWindow()) return true;
    return (::GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_POPUP) != 0;
}

bool WindowReader::admissible(void* window) const {
    const auto hwnd = static_cast<HWND>(window);
    if (!top_level(window) || hwnd == ::GetDesktopWindow()) return false;
    DWORD process = 0;
    ::GetWindowThreadProcessId(hwnd, &process);
    if (process == ::GetCurrentProcessId()) return false;
    const std::wstring name = class_of(hwnd);
    if (name.starts_with(kOwnClassPrefix)) return false;
    if (std::find(kShellClasses.begin(), kShellClasses.end(), name) != kShellClasses.end()) return false;
    if (shell_tool_window(hwnd, process, name)) return false;
    return !shell_.shell_managed(window) || shell_.shown_in_switchers(window);
}

WindowReader::Reading WindowReader::read(void* window, WindowId id) const {
    const auto hwnd = static_cast<HWND>(window);
    Reading reading;
    WindowFields& fields = reading.fields;
    const auto style = static_cast<std::uint32_t>(::GetWindowLongPtrW(hwnd, GWL_STYLE));
    const auto extended = static_cast<std::uint32_t>(::GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    const auto class_style = static_cast<std::uint32_t>(::GetClassLongPtrW(hwnd, GCL_STYLE));
    bool refused = false;
    fields.taskbar = taskbar_of(window, extended, refused);
    fields.style.style = style & WindowFields::Style::kStyleMask;
    fields.style.extended = (extended & WindowFields::Style::kExtendedMask) | (refused ? WS_EX_TOOLWINDOW : 0);
    fields.style.class_style = class_style;
    fields.cloaked = cloaked(hwnd);
    fields.show = show_of(window, fields.cloaked);
    std::array<wchar_t, kTitleChars> title{};
    const int length = ::InternalGetWindowText(hwnd, title.data(), static_cast<int>(title.size()));
    fields.title = length > 0 ? utf8_of(std::wstring_view(title.data(), static_cast<std::size_t>(length))) : "";
    const std::wstring name = class_of(hwnd);
    fields.class_name = utf8_of(name);
    fields.z_glue = name == kOfficeBorderClass;
    fields.geometry = geometry_of(window, id);
    if ((extended & WS_EX_LAYERED) != 0) fields.layering = layering_of(hwnd);
    fields.band = band_of(hwnd);
    fields.frame = frame_of(window, style, class_style);
    const UINT dpi = ::GetDpiForWindow(hwnd);
    fields.dpi = static_cast<std::uint16_t>(dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI);
    read_owner(window, reading);
    if (reading.owner == nullptr && !reading.hidden_owner) read_anchor(window, reading);
    return reading;
}

WindowFields::Show WindowReader::show_of(void* window, bool is_cloaked) const {
    if (is_cloaked || !effectively_visible(window)) return WindowFields::Show::Hidden;
    const auto style = ::GetWindowLongPtrW(static_cast<HWND>(window), GWL_STYLE);
    if ((style & WS_MINIMIZE) != 0) return WindowFields::Show::Minimized;
    if ((style & WS_MAXIMIZE) != 0) return WindowFields::Show::Maximized;
    return WindowFields::Show::Shown;
}

WindowFields::Geometry WindowReader::geometry_of(void* window, WindowId id) const {
    const auto hwnd = static_cast<HWND>(window);
    const WindowSurface surface{static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(window))};
    WindowFields::Geometry geometry;
    geometry.frame = frame_of_surface(hwnd, surface);
    const auto style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((style & WS_MAXIMIZE) != 0) geometry.frame = clipped_to_monitor(hwnd, geometry.frame);
    geometry.client = client_of(hwnd, geometry.frame);
    if ((style & WS_THICKFRAME) != 0 && surface.valid()) {
        geometry.margin_left = margin(surface.inset_x());
        geometry.margin_top = margin(surface.inset_y());
        geometry.margin_right = margin(static_cast<std::int32_t>(surface.border_width()) - surface.inset_x());
        geometry.margin_bottom = margin(static_cast<std::int32_t>(surface.border_height()) - surface.inset_y());
    }
    geometry.region = region_of(hwnd, surface.inset_x(), surface.inset_y());
    geometry.picture = directory_.picture_of(id).value_or(Rect{0, 0, geometry.frame.width, geometry.frame.height});
    return geometry;
}

// Windows' own taskbar rule: a button for a visible window nothing owns
// that is no tool window, and for any visible window that asks with
// WS_EX_APPWINDOW - unless the shell refuses it one. RAIL's server reads
// the tool-window bit as winning over WS_EX_APPWINDOW; Explorer's taskbar,
// which is the one on this desktop, gives the button, and leaves out a
// WS_EX_NOACTIVATE window that does not ask ("does not appear on the
// taskbar by default").
std::uint8_t WindowReader::taskbar_of(void* window, std::uint32_t extended, bool& refused) const {
    const auto hwnd = static_cast<HWND>(window);
    refused = shell_.shell_managed(window) && !shell_.shown_in_switchers(window);
    const bool visible = ::IsWindowVisible(hwnd) != FALSE;
    const bool asks = (extended & WS_EX_APPWINDOW) != 0;
    const bool tool = !asks && (extended & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) != 0;
    const bool unowned = ::GetWindow(hwnd, GW_OWNER) == nullptr || asks;
    std::uint8_t bits = 0;
    // Alt+Tab follows the same rule; only the shell's own view can say
    // otherwise, and it is the refusal above.
    if (visible && unowned && !tool && !refused) bits |= WindowFields::kTaskbarButton | WindowFields::kInSwitchers;
    return bits;
}

std::uint8_t WindowReader::frame_of(void* window, std::uint32_t style, std::uint32_t class_style) const {
    const bool dwm = dwm_draws_frame(static_cast<HWND>(window));
    const bool framed = (style & (WS_BORDER | WS_DLGFRAME | WS_THICKFRAME)) != 0;
    const bool shaded = settings_.drop_shadows() && ((class_style & CS_DROPSHADOW) != 0 || (dwm && framed));
    return static_cast<std::uint8_t>((dwm ? WindowFields::kDwmFrame : 0) | (shaded ? WindowFields::kDropShadow : 0));
}

void WindowReader::read_owner(void* window, Reading& into) const {
    HWND owner = ::GetWindow(static_cast<HWND>(window), GW_OWNER);
    if (owner == nullptr) return;
    if ((::GetWindowLongPtrW(owner, GWL_STYLE) & WS_CHILD) != 0) owner = ::GetAncestor(owner, GA_ROOT);
    if (owner == nullptr || owner == ::GetDesktopWindow()) return;
    if (owner_hidden(owner)) {
        into.hidden_owner = true;
        return;
    }
    into.owner = owner;
}

// A menu and a combo box's dropped list are placed against a window that
// does not own them: the association lives in the thread's input state.
// GUITHREADINFO names the window whose menu is up while one is, and the
// window holding the mouse capture while the active window's list is
// dropped. That window is often one nobody sees (an application draws its
// menu bar in an internal window), so it is taken up to its root owner.
void WindowReader::read_anchor(void* window, Reading& into) const {
    const auto hwnd = static_cast<HWND>(window);
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);
    if (::GetGUIThreadInfo(::GetWindowThreadProcessId(hwnd, nullptr), &gui) == FALSE) return;
    HWND anchor = nullptr;
    if (gui.hwndMenuOwner != nullptr && ::GetClassLongPtrW(hwnd, GCW_ATOM) == kMenuClassAtom) {
        anchor = root_owner_of(gui.hwndMenuOwner);
    } else if (((::GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) != 0 || gui.hwndCapture == hwnd)
               && gui.hwndActive != nullptr) {
        anchor = root_owner_of(gui.hwndActive);
    }
    if (anchor == nullptr || anchor == hwnd) return;
    const WindowSurface self{static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(hwnd))};
    const WindowSurface against{static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(anchor))};
    if (!self.valid() || !against.valid()) return;
    into.anchor = anchor;
    into.fields.popup.rect = Rect{self.screen_x(0) - against.screen_x(0), self.screen_y(0) - against.screen_y(0),
                                  self.width(), self.height()};
}

WindowFields::AppId WindowReader::read_app_id(void* window) const {
    WindowFields::AppId app;
    app.aumid = app_ids_.app_id_of(window);
    DWORD process_id = 0;
    ::GetWindowThreadProcessId(static_cast<HWND>(window), &process_id);
    app.process_id = process_id;
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (process == nullptr) return app;
    std::array<wchar_t, MAX_PATH * 2> path{};
    DWORD length = static_cast<DWORD>(path.size());
    if (::QueryFullProcessImageNameW(process, 0, path.data(), &length) != FALSE) {
        app.image = utf8_of(std::wstring_view(path.data(), length));
    }
    ::CloseHandle(process);
    return app;
}

}  // namespace rwin::guest
