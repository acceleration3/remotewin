#include "guest/windows/session_boundary.hpp"

#include <array>
#include <cwctype>

namespace rwin::guest {

namespace {

constexpr std::array<std::wstring_view, 12> kBoundary{
    L"explorer.exe",   L"svchost.exe",  L"runtimebroker.exe",  L"sihost.exe",
    L"services.exe",   L"wininit.exe",  L"winlogon.exe",       L"userinit.exe",
    L"dllhost.exe",    L"lsw-agent.exe", L"lsw-guest-agent.exe", L"remotewin-guest.exe",
};

bool same_name(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    }
    return true;
}

}

bool SessionBoundary::contains(std::wstring_view image_name) const {
    for (const std::wstring_view name : kBoundary) {
        if (same_name(name, image_name)) return true;
    }
    return false;
}

}
