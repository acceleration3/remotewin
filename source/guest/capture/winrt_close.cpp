#include "guest/capture/winrt_close.hpp"

#define ____FIReference_1_boolean_INTERFACE_DEFINED__

// clang-format off
#include <windows.h>
#include <windows.foundation.h>
// clang-format on

namespace rwin::guest {

void close_winrt(void* object) {
    if (object == nullptr) return;
    ABI::Windows::Foundation::IClosable* closable = nullptr;
    if (FAILED(static_cast<IUnknown*>(object)->QueryInterface(__uuidof(ABI::Windows::Foundation::IClosable),
                                                              reinterpret_cast<void**>(&closable)))) {
        return;
    }
    closable->Close();
    closable->Release();
}

}
