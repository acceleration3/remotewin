#pragma once

#include <cstdint>
#include <memory>
#include <optional>

struct ID3D11Device;

namespace rwin::guest {
class SurfaceIdReader {
public:
    explicit SurfaceIdReader(ID3D11Device* device);
    ~SurfaceIdReader();

    SurfaceIdReader(const SurfaceIdReader&) = delete;
    SurfaceIdReader& operator=(const SurfaceIdReader&) = delete;
    SurfaceIdReader(SurfaceIdReader&&) = delete;
    SurfaceIdReader& operator=(SurfaceIdReader&&) = delete;

    [[nodiscard]] bool usable() const noexcept { return kernel_ != nullptr && device_ != 0; }

    [[nodiscard]] std::optional<std::uint64_t> id_of(void* shared_handle) const;

private:
    struct Kernel;

    std::unique_ptr<const Kernel> kernel_;
    std::uint32_t adapter_ = 0;
    std::uint32_t device_ = 0;
};

}
