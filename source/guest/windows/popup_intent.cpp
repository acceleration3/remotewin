#include "guest/windows/popup_intent.hpp"

#include "guest/windows/window_surface.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <array>
#include <cstdlib>
#include <cwchar>
#include <vector>

namespace rwin::guest {

namespace {

constexpr ULONG_PTR kMenuClassAtom = 32768;
constexpr UINT kGetMenu = 0x01E1;  // MN_GETHMENU
// A menu's window answers MN_GETHMENU at once; one that does not in this
// long is hung, and the popup is placed where Windows put it.
constexpr UINT kAnswerMs = 100;
// TrackPopupMenu puts a corner of the menu at the point it is given: a
// pointer within this of a corner is the point the menu was opened at.
constexpr std::int32_t kNearCorner = 4;
constexpr std::size_t kClassChars = 64;

bool class_is(HWND window, const wchar_t* name) {
    std::array<wchar_t, kClassChars> found{};
    return ::GetClassNameW(window, found.data(), static_cast<int>(found.size())) > 0
           && std::wcscmp(found.data(), name) == 0;
}

bool is_menu(HWND window) { return ::GetClassLongPtrW(window, GCW_ATOM) == kMenuClassAtom; }

HWND root_of(HWND window) {
    HWND root = ::GetAncestor(window, GA_ROOT);
    return root != nullptr ? root : window;
}

HMENU menu_of(HWND menu_window) {
    DWORD_PTR menu = 0;
    if (::SendMessageTimeoutW(menu_window, kGetMenu, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, kAnswerMs, &menu) == 0) {
        return nullptr;
    }
    return reinterpret_cast<HMENU>(menu);
}

GUITHREADINFO input_state_of(HWND window) {
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);
    if (::GetGUIThreadInfo(::GetWindowThreadProcessId(window, nullptr), &gui) == FALSE) gui = GUITHREADINFO{};
    return gui;
}

// The window a menu opens from, as one that is on screen. A menu can be
// tracked for a window nobody sees - the shell's context menu, which
// WizTree's list shows, is tracked for a hidden ItemPropMenuCallbackHelper
// - and then it opens from the window its thread has active, the one that
// was clicked. Against the hidden one it would open from nothing and stand
// alone, behind the window it belongs to.
HWND shown_owner_of_menu(HWND owner, const GUITHREADINFO& input) {
    const HWND root = root_of(owner);
    if (::IsWindowVisible(root) != FALSE) return root;
    return input.hwndActive != nullptr ? root_of(input.hwndActive) : nullptr;
}

bool mirrored(HWND window) { return (::GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0; }

BOOL CALLBACK collect_menus(HWND window, LPARAM into) {
    if (is_menu(window) && ::IsWindowVisible(window) != FALSE) {
        reinterpret_cast<std::vector<HWND>*>(into)->push_back(window);
    }
    return TRUE;
}

}  // namespace

PopupIntent::PopupIntent(void* window, const Rect& frame, std::optional<Point> opened_at) : frame_(frame) {
    const auto hwnd = static_cast<HWND>(window);
    if (is_menu(hwnd)) {
        read_menu(window, opened_at);
    } else if (class_is(hwnd, L"ComboLBox")) {
        read_combo_list(window);
    } else if (class_is(hwnd, L"tooltips_class32")) {
        read_tooltip(window, opened_at);
    } else if (::GetWindow(hwnd, GW_OWNER) == nullptr) {
        read_placed_against(window);
    }
}

void PopupIntent::read_menu(void* window, const std::optional<Point>& opened_at) {
    const auto hwnd = static_cast<HWND>(window);
    const HMENU menu = menu_of(hwnd);
    const GUITHREADINFO input = input_state_of(hwnd);
    HWND owner = input.hwndMenuOwner;
    if (menu != nullptr && owner != nullptr && read_menu_bar_item(menu, owner)) return;
    if (menu != nullptr && read_parent_item(window, menu)) return;
    if (owner == nullptr) return;
    owner = shown_owner_of_menu(owner, input);
    if (owner == nullptr) return;
    const auto at_corner = [&](std::int32_t x, std::int32_t y) {
        return std::abs(opened_at->x - x) <= kNearCorner && std::abs(opened_at->y - y) <= kNearCorner;
    };
    const std::int32_t right = frame_.x + static_cast<std::int32_t>(frame_.width);
    const std::int32_t bottom = frame_.y + static_cast<std::int32_t>(frame_.height);
    if (opened_at.has_value()
        && (at_corner(frame_.x, frame_.y) || at_corner(right, frame_.y) || at_corner(frame_.x, bottom)
            || at_corner(right, bottom))) {
        point_at(Kind::Context, owner, *opened_at);
        using Popup = WindowFields::Popup;
        direct(Edge::TopLeft, Edge::BottomRight, Popup::kFlipX | Popup::kFlipY | Popup::kSlideX | Popup::kSlideY,
               mirrored(owner));
        return;
    }
    open(Kind::Exact, owner, frame_.x, frame_.y, right, bottom);
}

// GetMenuBarInfo numbers the bar's items from 1; 0 is the bar itself.
bool PopupIntent::read_menu_bar_item(void* menu, void* owner) {
    const auto bar_owner = static_cast<HWND>(owner);
    const HMENU bar = ::GetMenu(bar_owner);
    if (bar == nullptr || ::IsMenu(bar) == FALSE) return false;
    const int count = ::GetMenuItemCount(bar);
    for (int item = 0; item < count; ++item) {
        if (::GetSubMenu(bar, item) != static_cast<HMENU>(menu)) continue;
        MENUBARINFO info{};
        info.cbSize = sizeof(info);
        if (::GetMenuBarInfo(bar_owner, OBJID_MENU, item + 1, &info) == FALSE) return false;
        open(Kind::MenuBar, root_of(bar_owner), info.rcBar.left, info.rcBar.top, info.rcBar.right, info.rcBar.bottom);
        using Popup = WindowFields::Popup;
        direct(Edge::BottomLeft, Edge::BottomRight, Popup::kFlipY | Popup::kSlideX | Popup::kSlideY,
               mirrored(bar_owner));
        return anchor_ != nullptr;
    }
    return false;
}

// The menu it opens out of is another menu window of the same thread,
// whose menu has this one as an item's submenu.
bool PopupIntent::read_parent_item(void* window, void* menu) {
    const auto hwnd = static_cast<HWND>(window);
    std::vector<HWND> menus;
    ::EnumThreadWindows(::GetWindowThreadProcessId(hwnd, nullptr), &collect_menus, reinterpret_cast<LPARAM>(&menus));
    for (HWND candidate : menus) {
        if (candidate == hwnd) continue;
        const HMENU parent = menu_of(candidate);
        if (parent == nullptr) continue;
        const int count = ::GetMenuItemCount(parent);
        for (int item = 0; item < count; ++item) {
            if (::GetSubMenu(parent, item) != static_cast<HMENU>(menu)) continue;
            RECT rect{};
            if (::GetMenuItemRect(nullptr, parent, static_cast<UINT>(item), &rect) == FALSE) return false;
            open(Kind::Submenu, candidate, rect.left, rect.top, rect.right, rect.bottom);
            using Popup = WindowFields::Popup;
            direct(Edge::TopRight, Edge::BottomRight, Popup::kFlipX | Popup::kSlideX | Popup::kSlideY,
                   mirrored(candidate));
            return anchor_ != nullptr;
        }
    }
    return false;
}

// The list's combo box has the focus while it is dropped (or its edit
// field does), and owns the capture.
void PopupIntent::read_combo_list(void* window) {
    const auto hwnd = static_cast<HWND>(window);
    const GUITHREADINFO gui = input_state_of(hwnd);
    for (HWND candidate : {gui.hwndFocus, ::GetParent(gui.hwndFocus), gui.hwndCapture, ::GetParent(gui.hwndCapture)}) {
        if (candidate == nullptr) continue;
        COMBOBOXINFO info{};
        info.cbSize = sizeof(info);
        if (::GetComboBoxInfo(candidate, &info) == FALSE || info.hwndList != hwnd) continue;
        RECT box{};
        if (::GetWindowRect(candidate, &box) == FALSE) break;
        open(Kind::Combo, root_of(candidate), box.left, box.top, box.right, box.bottom);
        using Popup = WindowFields::Popup;
        direct(Edge::BottomLeft, Edge::BottomRight, Popup::kFlipY | Popup::kSlideX | Popup::kSlideY, false);
        return;
    }
    read_placed_against(window);
}

void PopupIntent::read_tooltip(void* window, const std::optional<Point>& opened_at) {
    const auto hwnd = static_cast<HWND>(window);
    HWND against = ::GetWindow(hwnd, GW_OWNER);
    if (against == nullptr && opened_at.has_value()) against = ::WindowFromPoint(POINT{opened_at->x, opened_at->y});
    if (against == nullptr || !opened_at.has_value()) {
        read_placed_against(window);
        return;
    }
    against = root_of(against);
    point_at(Kind::Tooltip, against, *opened_at);
    using Popup = WindowFields::Popup;
    direct(Edge::TopLeft, Edge::BottomRight, Popup::kFlipY | Popup::kSlideX | Popup::kSlideY, false);
}

// A window nothing owns that Windows places against another: the
// association lives in the thread's input state. GUITHREADINFO names the
// window whose menu is up while one is, and the active window while a
// child of the desktop or the capture's window is up. That window is often
// one nobody sees (an application draws its menu bar in an internal
// window), so it is taken up to its root owner.
void PopupIntent::read_placed_against(void* window) {
    const auto hwnd = static_cast<HWND>(window);
    if (::GetWindow(hwnd, GW_OWNER) != nullptr) return;
    const GUITHREADINFO gui = input_state_of(hwnd);
    HWND against = nullptr;
    if (gui.hwndMenuOwner != nullptr && is_menu(hwnd)) {
        against = ::GetAncestor(gui.hwndMenuOwner, GA_ROOTOWNER);
    } else if (((::GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) != 0 || gui.hwndCapture == hwnd)
               && gui.hwndActive != nullptr) {
        against = ::GetAncestor(gui.hwndActive, GA_ROOTOWNER);
    }
    if (against == nullptr || against == hwnd) return;
    open(Kind::Exact, against, frame_.x, frame_.y, frame_.x + static_cast<std::int32_t>(frame_.width),
         frame_.y + static_cast<std::int32_t>(frame_.height));
}

void PopupIntent::open(Kind kind, void* anchor, std::int32_t left, std::int32_t top, std::int32_t right,
                       std::int32_t bottom) {
    const WindowSurface against{static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(anchor))};
    if (!against.valid() || right < left || bottom < top) return;
    anchor_ = anchor;
    popup_.kind = kind;
    popup_.placed = Rect{frame_.x - against.screen_x(0), frame_.y - against.screen_y(0), frame_.width, frame_.height};
    popup_.opens_from = Rect{left - against.screen_x(0), top - against.screen_y(0),
                             static_cast<std::uint32_t>(right - left), static_cast<std::uint32_t>(bottom - top)};
}

void PopupIntent::point_at(Kind kind, void* anchor, Point point) { open(kind, anchor, point.x, point.y, point.x, point.y); }

// A right-to-left window's menus open leftwards from the right edge.
void PopupIntent::direct(Edge edge, Edge gravity, std::uint8_t adjust, bool rtl) {
    const auto mirror = [rtl](Edge side) {
        if (!rtl) return side;
        switch (side) {
            case Edge::TopLeft: return Edge::TopRight;
            case Edge::TopRight: return Edge::TopLeft;
            case Edge::BottomLeft: return Edge::BottomRight;
            case Edge::BottomRight: return Edge::BottomLeft;
            case Edge::Left: return Edge::Right;
            case Edge::Right: return Edge::Left;
            default: return side;
        }
    };
    popup_.edge = mirror(edge);
    popup_.gravity = mirror(gravity);
    popup_.adjust = adjust;
}

}  // namespace rwin::guest
