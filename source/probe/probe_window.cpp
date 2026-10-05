#include "probe/probe_window.hpp"

#include "probe/input_journal.hpp"
#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
#include <dwmapi.h>
// clang-format on

#include <algorithm>
#include <cstdio>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rwin::probe {

namespace {

// Newer than mingw's dwmapi.h.
constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmWindowCornerPreference = 33;

struct RegionDeleter {
    void operator()(HRGN__* region) const noexcept { ::DeleteObject(region); }
};
using OwnedRegion = std::unique_ptr<HRGN__, RegionDeleter>;

std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                             static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                          out.data(), length);
    return out;
}

// One class per class style: a class style belongs to the class, and the
// spec names it per window.
std::optional<std::wstring> class_for(std::uint32_t class_style, WNDPROC procedure,
                                      std::string& failure) {
    wchar_t name[32] = {};
    ::swprintf(name, 32, L"rwin-probe-%08x", class_style);
    WNDCLASSEXW described{};
    described.cbSize = sizeof(described);
    described.style = class_style;
    described.lpfnWndProc = procedure;
    described.hInstance = ::GetModuleHandleW(nullptr);
    described.hCursor = ::LoadCursorA(nullptr, IDC_ARROW);
    described.lpszClassName = name;
    if (::RegisterClassExW(&described) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        failure = std::format("RegisterClassEx with class style {:#x} failed: error {}",
                              class_style, ::GetLastError());
        return std::nullopt;
    }
    return std::wstring{name};
}

}  // namespace

ProbeWindow::ProbeWindow(const WindowSpec& spec, InputJournal& journal)
    : spec_(spec), journal_(journal) {
    const std::optional<std::wstring> class_name =
        class_for(spec.class_style, &ProbeWindow::procedure, failure_);
    if (!class_name.has_value()) return;

    RECT outer{0, 0, static_cast<LONG>(spec.client_width), static_cast<LONG>(spec.client_height)};
    ::AdjustWindowRectExForDpi(&outer, spec.style, FALSE, spec.extended_style,
                               ::GetDpiForSystem());
    const std::wstring title = wide(spec.title);
    auto* owner = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(spec.owner));
    // WS_VISIBLE is left to show(): everything below has to be in place
    // before the window is first drawn.
    const DWORD style = spec.style & ~static_cast<DWORD>(WS_VISIBLE);
    window_ = ::CreateWindowExW(spec.extended_style, class_name->c_str(), title.c_str(), style, spec.x,
                                spec.y, outer.right - outer.left, outer.bottom - outer.top,
                                owner, nullptr, ::GetModuleHandleW(nullptr), this);
    if (window_ == nullptr) {
        failure_ = std::format("CreateWindowEx '{}' style={:#x} ex={:#x} failed: error {}",
                               spec.title, spec.style, spec.extended_style, ::GetLastError());
        return;
    }
    made_ = true;
    apply_layering();
    apply_shape();
    apply_dwm_attributes();
    show();
    RWIN_INFO("window", "made {:#x} '{}' style={:#x} ex={:#x} class={:#x}", handle(),
              spec.title, spec.style, spec.extended_style, spec.class_style);
}

ProbeWindow::~ProbeWindow() {
    if (window_ == nullptr) return;
    HWND window = window_;
    ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    window_ = nullptr;
    ::DestroyWindow(window);
}

std::uint64_t ProbeWindow::handle() const noexcept {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(window_));
}

void ProbeWindow::apply_layering() {
    if (spec_.layering == WindowSpec::Layering::None) return;
    DWORD flags = 0;
    if (spec_.alpha_blended()) flags |= LWA_ALPHA;
    if (spec_.color_keyed()) flags |= LWA_COLORKEY;
    if (::SetLayeredWindowAttributes(window_, spec_.color_key, spec_.alpha, flags) == FALSE) {
        RWIN_WARN("window", "SetLayeredWindowAttributes on '{}' failed: error {} (is "
                  "WS_EX_LAYERED in its extended style?)", spec_.title, ::GetLastError());
    }
}

// In the window's own coordinates, which start at the window rectangle's
// corner, invisible border and all.
void ProbeWindow::apply_shape() {
    if (spec_.shape == WindowSpec::Shape::None) return;
    RECT rect{};
    ::GetWindowRect(window_, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    OwnedRegion region;
    switch (spec_.shape) {
        case WindowSpec::Shape::RoundedRect: {
            const int radius = std::min(width, height) / 4;
            region.reset(::CreateRoundRectRgn(0, 0, width + 1, height + 1, radius, radius));
            break;
        }
        case WindowSpec::Shape::Ellipse:
            region.reset(::CreateEllipticRgn(0, 0, width, height));
            break;
        case WindowSpec::Shape::Frame: {
            region.reset(::CreateRectRgn(0, 0, width, height));
            const OwnedRegion hole{
                ::CreateRectRgn(width / 4, height / 4, width * 3 / 4, height * 3 / 4)};
            ::CombineRgn(region.get(), region.get(), hole.get(), RGN_DIFF);
            break;
        }
        case WindowSpec::Shape::None:
            return;
    }
    // The window owns the region once it is set, and frees it itself.
    if (::SetWindowRgn(window_, region.get(), FALSE) != 0) {
        static_cast<void>(region.release());
        return;
    }
    RWIN_WARN("window", "SetWindowRgn on '{}' failed: error {}", spec_.title, ::GetLastError());
}

void ProbeWindow::apply_dwm_attributes() {
    const auto set = [this](DWORD attribute, std::int32_t value, const char* name) {
        if (value == WindowSpec::kUnset) return;
        const DWORD word = static_cast<DWORD>(value);
        const HRESULT applied = ::DwmSetWindowAttribute(window_, attribute, &word, sizeof(word));
        if (FAILED(applied)) {
            RWIN_WARN("window", "{}={} on '{}' failed: {:#010x}", name, value, spec_.title,
                      static_cast<unsigned>(applied));
        }
    };
    set(DWMWA_NCRENDERING_POLICY, spec_.nc_rendering_policy, "DWMWA_NCRENDERING_POLICY");
    set(kDwmUseImmersiveDarkMode, spec_.dark_mode, "DWMWA_USE_IMMERSIVE_DARK_MODE");
    set(kDwmWindowCornerPreference, spec_.corner_preference, "DWMWA_WINDOW_CORNER_PREFERENCE");
}

void ProbeWindow::show() {
    switch (spec_.show) {
        case WindowSpec::ShowState::Normal:
            ::ShowWindow(window_, SW_SHOWNORMAL);
            break;
        case WindowSpec::ShowState::NoActivate:
            ::ShowWindow(window_, SW_SHOWNOACTIVATE);
            break;
        case WindowSpec::ShowState::Maximized:
            ::ShowWindow(window_, SW_SHOWMAXIMIZED);
            break;
        case WindowSpec::ShowState::Minimized:
            ::ShowWindow(window_, SW_SHOWMINNOACTIVE);
            break;
        case WindowSpec::ShowState::Hidden:
            return;
    }
    ::UpdateWindow(window_);
}

void ProbeWindow::paint() {
    PAINTSTRUCT painting{};
    HDC dc = ::BeginPaint(window_, &painting);
    RECT client{};
    ::GetClientRect(window_, &client);
    pattern_.paint(dc, client.right, client.bottom,
                   spec_.color_keyed() ? std::optional<std::uint32_t>{spec_.color_key}
                                       : std::nullopt);
    ::EndPaint(window_, &painting);
}

std::intptr_t __stdcall ProbeWindow::procedure(HWND__* window, unsigned message,
                                               std::uintptr_t wparam, std::intptr_t lparam) {
    if (message == WM_NCCREATE) {
        const auto* creating = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        auto* self = static_cast<ProbeWindow*>(creating->lpCreateParams);
        // Kept before CreateWindowEx returns, so the messages it sends on
        // the way are answered by this window too.
        self->window_ = window;
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<ProbeWindow*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self == nullptr) return ::DefWindowProcW(window, message, wparam, lparam);
    return self->answer(message, wparam, lparam);
}

// SetCursorPos makes a WM_MOUSEMOVE of its own, at the middle; that one
// finds the cursor already there, and is neither put back nor left alone.
void ProbeWindow::follow_cursor(std::intptr_t lparam) {
    RECT client{};
    ::GetClientRect(window_, &client);
    const POINT middle_client{client.right / 2, client.bottom / 2};
    const POINT at{static_cast<std::int16_t>(lparam & 0xFFFF), static_cast<std::int16_t>((lparam >> 16) & 0xFFFF)};
    if (at.x == middle_client.x && at.y == middle_client.y) return;
    if (!recentre_) {
        put_back_ = 0;
        return;
    }
    POINT middle = middle_client;
    ::ClientToScreen(window_, &middle);
    ::SetCursorPos(middle.x, middle.y);
    ++put_back_;
}

// The handle WM_INPUT carries is good only until the message is answered,
// so what it says of a mouse is noted now (InputRecord says how).
void ProbeWindow::note_raw_input(std::intptr_t lparam) {
    RAWINPUT input{};
    UINT size = sizeof(input);
    if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER))
            == static_cast<UINT>(-1)
        || input.header.dwType != RIM_TYPEMOUSE) {
        RWIN_WARN("input", "WM_INPUT on {:#x} that is no mouse's, or unreadable", handle());
        return;
    }
    const RAWMOUSE& mouse = input.data.mouse;
    const std::uint64_t flags = mouse.usFlags | (static_cast<std::uint64_t>(mouse.usButtonFlags) << 16);
    const std::uint64_t motion = static_cast<std::uint32_t>(mouse.lLastX)
                                 | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(mouse.lLastY)) << 32);
    journal_.note(InputRecord{WM_INPUT, reinterpret_cast<std::uintptr_t>(window_), flags, motion, 0});
}

std::intptr_t ProbeWindow::answer(unsigned message, std::uintptr_t wparam,
                                  std::intptr_t lparam) {
    HWND window = window_;
    LRESULT answered = 0;
    switch (message) {
        case WM_SETCURSOR:
            if (answer_cursor_.has_value() && LOWORD(lparam) == HTCLIENT) {
                ::SetCursor(*answer_cursor_);
                answered = TRUE;
            } else {
                answered = ::DefWindowProcW(window, message, wparam, lparam);
            }
            break;
        case WM_MOUSEMOVE:
            follow_cursor(lparam);
            break;
        case WM_INPUT:
            // Noted here, with its motion, rather than below with a handle
            // that is gone by then; DefWindowProc frees it.
            note_raw_input(lparam);
            return ::DefWindowProcW(window, message, wparam, lparam);
        case WM_ERASEBKGND:
            // The pattern covers every pixel; erasing first is a flash.
            answered = 1;
            break;
        case WM_PAINT:
            paint();
            break;
        case WM_SYSCOMMAND:
            // F10 or Alt alone asks for the menu bar, and DefWindowProc
            // would run its modal loop on the probe's one thread until
            // the menu is dismissed - with the harness's socket unread
            // meanwhile. The key messages themselves are already noted.
            answered = (wparam & 0xFFF0) == SC_KEYMENU
                           ? 0
                           : ::DefWindowProcW(window, message, wparam, lparam);
            break;
        case WM_NCDESTROY:
            ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            window_ = nullptr;
            answered = ::DefWindowProcW(window, message, wparam, lparam);
            break;
        default:
            answered = ::DefWindowProcW(window, message, wparam, lparam);
            break;
    }
    journal_.note(InputRecord{message, reinterpret_cast<std::uintptr_t>(window), wparam,
                              static_cast<std::uint64_t>(lparam), answered});
    return answered;
}

}  // namespace rwin::probe
