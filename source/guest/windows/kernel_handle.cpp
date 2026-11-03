#include "guest/windows/kernel_handle.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <utility>

namespace rwin::guest {

KernelHandle::KernelHandle(void* handle) noexcept : handle_(handle) {}

KernelHandle::~KernelHandle() { reset(); }

KernelHandle::KernelHandle(KernelHandle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

KernelHandle& KernelHandle::operator=(KernelHandle&& other) noexcept {
    if (this != &other) reset(std::exchange(other.handle_, nullptr));
    return *this;
}

bool KernelHandle::valid() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

void KernelHandle::reset(void* handle) noexcept {
    if (valid()) ::CloseHandle(handle_);
    handle_ = handle;
}

void** KernelHandle::put() noexcept {
    reset();
    return &handle_;
}

}
