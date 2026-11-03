#pragma once

namespace rwin::guest {
class KernelHandle {
public:
    KernelHandle() noexcept = default;
    explicit KernelHandle(void* handle) noexcept;
    ~KernelHandle();

    KernelHandle(KernelHandle&& other) noexcept;
    KernelHandle& operator=(KernelHandle&& other) noexcept;
    KernelHandle(const KernelHandle&) = delete;
    KernelHandle& operator=(const KernelHandle&) = delete;

    [[nodiscard]] void* get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept;
    void reset(void* handle = nullptr) noexcept;
    [[nodiscard]] void** put() noexcept;

private:
    void* handle_ = nullptr;
};

}
