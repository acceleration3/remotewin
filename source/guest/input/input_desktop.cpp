#include "guest/input/input_desktop.hpp"

#include "guest/windows/wide_text.hpp"
#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {
std::wstring name_of(HDESK desktop) {
    wchar_t name[256] = {};
    DWORD bytes = 0;
    if (::GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof name, &bytes) == FALSE) return {};
    return name;
}

}

InputDesktop::~InputDesktop() {
    if (attached_ != nullptr) ::CloseDesktop(static_cast<HDESK>(attached_));
}

bool InputDesktop::follow() {
    HDESK input = ::OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (input == nullptr) return false;
    const std::wstring name = name_of(input);
    if (!name.empty() && name == name_) {
        ::CloseDesktop(input);
        return true;
    }
    if (::SetThreadDesktop(input) == FALSE) {
        RWIN_WARN("input", "cannot follow the input desktop (error {})", ::GetLastError());
        ::CloseDesktop(input);
        return false;
    }
    if (attached_ != nullptr) ::CloseDesktop(static_cast<HDESK>(attached_));
    attached_ = input;
    name_ = name;
    RWIN_INFO("input", "following the input desktop '{}'", utf8_of(name_));
    return true;
}

}
