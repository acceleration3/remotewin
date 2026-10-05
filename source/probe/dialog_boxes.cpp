#include "probe/dialog_boxes.hpp"

#include "logging/logger.hpp"
#include "probe/input_journal.hpp"
#include "proto/probe/input_record.hpp"
#include "proto/probe/probe_action.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <format>

namespace rwin::probe {

namespace {

constexpr DWORD kDialogStyle = DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
// Dialog units are a quarter of the dialog font's average width and an
// eighth of its height; a template is sized in them.
constexpr int kHorizontalUnits = 4;
constexpr int kVerticalUnits = 8;

HWND as_window(std::uint64_t handle) {
    return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(handle));
}

}  // namespace

struct DialogBoxes::Opening {
    DialogBoxes* self;
    const ProbeAction* action;
};

DialogBoxes::DialogBoxes(InputJournal& journal) : journal_(journal) {}

// DLGTEMPLATE, then the menu, class and title as WORD arrays: no menu, the
// dialog class, the title as UTF-16. No controls follow.
const void* DialogBoxes::template_for(const ProbeAction& action) {
    const DWORD style = kDialogStyle | (action.centred ? DS_CENTER : 0);
    const LONG units = ::GetDialogBaseUnits();
    const int across = LOWORD(units);
    const int down = HIWORD(units);
    template_.clear();
    template_.resize(sizeof(DLGTEMPLATE) / sizeof(std::uint16_t));
    auto* head = reinterpret_cast<DLGTEMPLATE*>(template_.data());
    head->style = style;
    head->dwExtendedStyle = 0;
    head->cdit = 0;
    head->x = 0;
    head->y = 0;
    head->cx = static_cast<short>(::MulDiv(action.width, kHorizontalUnits, across));
    head->cy = static_cast<short>(::MulDiv(action.height, kVerticalUnits, down));
    template_.push_back(0);
    template_.push_back(0);
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, action.title.c_str(), -1, nullptr, 0);
    const std::size_t at = template_.size();
    template_.resize(at + static_cast<std::size_t>(length));
    ::MultiByteToWideChar(CP_UTF8, 0, action.title.c_str(), -1,
                          reinterpret_cast<wchar_t*>(template_.data() + at), length);
    return template_.data();
}

bool DialogBoxes::run_modal(const ProbeAction& action, std::string& failure) {
    Opening opening{this, &action};
    const auto* made = static_cast<const DLGTEMPLATE*>(template_for(action));
    const INT_PTR ended =
        ::DialogBoxIndirectParamW(::GetModuleHandleW(nullptr), made, as_window(action.window),
                                  reinterpret_cast<DLGPROC>(&DialogBoxes::procedure),
                                  reinterpret_cast<LPARAM>(&opening));
    if (ended == -1) {
        failure = std::format("DialogBoxIndirectParam '{}' failed: error {}", action.title,
                              ::GetLastError());
        return false;
    }
    RWIN_INFO("dialog", "modal '{}' ended with {}", action.title, static_cast<long long>(ended));
    return true;
}

HWND DialogBoxes::open_modeless(const ProbeAction& action, std::string& failure) {
    Opening opening{this, &action};
    const auto* made = static_cast<const DLGTEMPLATE*>(template_for(action));
    HWND dialog = ::CreateDialogIndirectParamW(
        ::GetModuleHandleW(nullptr), made, as_window(action.window),
        reinterpret_cast<DLGPROC>(&DialogBoxes::procedure), reinterpret_cast<LPARAM>(&opening));
    if (dialog == nullptr) {
        failure = std::format("CreateDialogIndirectParam '{}' failed: error {}", action.title,
                              ::GetLastError());
        return nullptr;
    }
    modeless_.insert(dialog);
    return dialog;
}

bool DialogBoxes::end(HWND dialog, std::string& failure) {
    const bool modeless = modeless_.erase(dialog) != 0;
    const BOOL ended = modeless ? ::DestroyWindow(dialog) : ::EndDialog(dialog, IDOK);
    if (ended != FALSE) return true;
    failure = std::format("{} {:#x} failed: error {}", modeless ? "DestroyWindow" : "EndDialog",
                          reinterpret_cast<std::uintptr_t>(dialog), ::GetLastError());
    return false;
}

// The template is in dialog units, which DS_CENTER centres; the client
// size asked for is in pixels. The dialog is grown to that size about the
// place Windows chose for it, or put where it was asked to be.
void DialogBoxes::place(HWND dialog, const ProbeAction& action) const {
    RECT outer{0, 0, action.width, action.height};
    const auto style = static_cast<DWORD>(::GetWindowLongPtrW(dialog, GWL_STYLE));
    const auto extended = static_cast<DWORD>(::GetWindowLongPtrW(dialog, GWL_EXSTYLE));
    ::AdjustWindowRectExForDpi(&outer, style, FALSE, extended, ::GetDpiForWindow(dialog));
    const int width = outer.right - outer.left;
    const int height = outer.bottom - outer.top;
    int x = action.x;
    int y = action.y;
    if (action.centred) {
        RECT now{};
        ::GetWindowRect(dialog, &now);
        x = (now.left + now.right - width) / 2;
        y = (now.top + now.bottom - height) / 2;
    }
    ::SetWindowPos(dialog, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

std::intptr_t __stdcall DialogBoxes::procedure(HWND dialog, unsigned message,
                                               std::uintptr_t wparam, std::intptr_t lparam) {
    if (message == WM_INITDIALOG) {
        const auto* opening = reinterpret_cast<const Opening*>(lparam);
        ::SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(opening->self));
        opening->self->place(dialog, *opening->action);
    }
    auto* self = reinterpret_cast<DialogBoxes*>(::GetWindowLongPtrW(dialog, DWLP_USER));
    if (self == nullptr) return FALSE;
    return self->answer(dialog, message, wparam, lparam);
}

std::intptr_t DialogBoxes::answer(HWND dialog, unsigned message, std::uintptr_t wparam,
                                  std::intptr_t lparam) {
    std::intptr_t answered = FALSE;
    switch (message) {
        case WM_INITDIALOG:
            answered = TRUE;
            break;
        case WM_CLOSE: {
            std::string failure;
            if (!end(dialog, failure)) RWIN_WARN("dialog", "{}", failure);
            answered = TRUE;
            break;
        }
        case WM_NCDESTROY:
            modeless_.erase(dialog);
            break;
        default:
            break;
    }
    journal_.note(InputRecord{message, reinterpret_cast<std::uintptr_t>(dialog), wparam,
                              static_cast<std::uint64_t>(lparam), answered});
    return answered;
}

}  // namespace rwin::probe
