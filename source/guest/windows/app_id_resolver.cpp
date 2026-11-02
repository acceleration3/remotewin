#include "guest/windows/app_id_resolver.hpp"

#include "guest/windows/wide_text.hpp"
#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
#include <objbase.h>
// clang-format on

namespace rwin::guest {

namespace {
constexpr CLSID kResolverClass{0x660b90c8, 0x73a9, 0x4b58, {0x8c, 0xae, 0x35, 0x5b, 0x7f, 0x55, 0x34, 0x1b}};
constexpr IID kResolverInterface{0xde25675a, 0x72de, 0x44b4, {0x93, 0x73, 0x05, 0x17, 0x04, 0x50, 0xc1, 0x40}};

constexpr std::size_t kGetAppIdForWindow = 5;
using GetAppIdForWindow = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, HWND, LPWSTR*, BOOL*, BOOL*, BOOL*);

}

AppIdResolver::AppIdResolver() {
    const HRESULT made = ::CoCreateInstance(kResolverClass, nullptr, CLSCTX_INPROC_SERVER, kResolverInterface,
                                            reinterpret_cast<void**>(&resolver_));
    if (FAILED(made)) {
        resolver_ = nullptr;
        RWIN_WARN("appid", "no application resolver ({:#010x}): windows are reported without an AppUserModelID",
                  static_cast<unsigned long>(made));
    }
}

AppIdResolver::~AppIdResolver() {
    if (resolver_ != nullptr) resolver_->Release();
}

std::string AppIdResolver::app_id_of(void* window) const {
    if (resolver_ == nullptr) return {};
    void** table = *reinterpret_cast<void***>(resolver_);
    const auto resolve = reinterpret_cast<GetAppIdForWindow>(table[kGetAppIdForWindow]);
    LPWSTR id = nullptr;
    BOOL pinning_prevented = FALSE;
    BOOL explicit_id = FALSE;
    BOOL shortcut_valid = FALSE;
    if (FAILED(resolve(resolver_, static_cast<HWND>(window), &id, &pinning_prevented, &explicit_id, &shortcut_valid))
        || id == nullptr) {
        return {};
    }
    std::string utf8 = utf8_of(id);
    ::CoTaskMemFree(id);
    return utf8;
}

}
