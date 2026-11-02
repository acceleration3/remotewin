#include "guest/windows/shell_views.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
#include <objbase.h>
#include <servprov.h>
// clang-format on

namespace rwin::guest {

namespace {

constexpr WORD kIsShellManagedWindowOrdinal = 2574;
constexpr const wchar_t* kAsNormalWindow = L"Microsoft.Windows.ShellManagedWindowAsNormalWindow";
constexpr CLSID kImmersiveShell{0xc2f03a33, 0x21f5, 0x47fa, {0xb4, 0xbb, 0x15, 0x63, 0x62, 0xa2, 0xf2, 0x39}};
constexpr IID kViewCollection{0x1841c6d7, 0x4f9d, 0x42c0, {0xaf, 0x41, 0x87, 0x47, 0x53, 0x8f, 0x10, 0xe5}};
constexpr std::size_t kGetViewForHwnd = 6;
constexpr std::size_t kGetShowInSwitchers = 27;
using GetViewForHwnd = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, HWND, IUnknown**);
using GetShowInSwitchers = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, BOOL*);

template <typename Method>
Method slot(IUnknown* object, std::size_t index) {
    void** table = *reinterpret_cast<void***>(object);
    return reinterpret_cast<Method>(table[index]);
}

IUnknown* view_collection() {
    IServiceProvider* shell = nullptr;
    if (FAILED(::CoCreateInstance(kImmersiveShell, nullptr, CLSCTX_LOCAL_SERVER, IID_IServiceProvider,
                                  reinterpret_cast<void**>(&shell)))) {
        return nullptr;
    }
    IUnknown* views = nullptr;
    const HRESULT found = shell->QueryService(kViewCollection, kViewCollection, reinterpret_cast<void**>(&views));
    shell->Release();
    return SUCCEEDED(found) ? views : nullptr;
}

}

ShellViews::ShellViews()
    : is_shell_managed_(reinterpret_cast<IsShellManagedWindow>(reinterpret_cast<void*>(
          ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), MAKEINTRESOURCEA(kIsShellManagedWindowOrdinal))))) {
    if (is_shell_managed_ == nullptr) {
        RWIN_INFO("shell", "user32 has no IsShellManagedWindow: every window may have a taskbar button");
    }
}

ShellViews::~ShellViews() {
    if (views_ != nullptr) views_->Release();
}

bool ShellViews::shell_managed(void* window) const {
    if (is_shell_managed_ == nullptr || is_shell_managed_(window) == 0) return false;
    return ::GetPropW(static_cast<HWND>(window), kAsNormalWindow) == nullptr;
}

bool ShellViews::shown_in_switchers(void* window) const {
    if (views_ == nullptr) views_ = view_collection();
    if (views_ == nullptr) return false;
    IUnknown* view = nullptr;
    const HRESULT found = slot<GetViewForHwnd>(views_, kGetViewForHwnd)(views_, static_cast<HWND>(window), &view);
    if (FAILED(found) || view == nullptr) {
        if (found == RPC_E_DISCONNECTED || found == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE)) {
            views_->Release();
            views_ = nullptr;
        }
        return false;
    }
    BOOL shown = FALSE;
    const HRESULT read = slot<GetShowInSwitchers>(view, kGetShowInSwitchers)(view, &shown);
    view->Release();
    return SUCCEEDED(read) && shown != FALSE;
}

}
