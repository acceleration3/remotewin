#pragma once

#include <cstdint>
#include <memory>
#include <optional>

struct ID3D11Device;

namespace rwin::guest {

// Reads the id the guest's GPU driver gave a shared texture: the name the
// host asks the GPU device's export socket for it by.
//
// The id is in the private data the driver wrote onto the texture's
// allocation, which the kernel hands to every process that opens the
// texture. So it is read by opening our own texture again through D3DKMT,
// the kernel thunks gdi32 exports, on a kernel device of the adapter the
// Direct3D device runs on - no driver call of any project's own. One such
// device serves the whole process.
//
// Nothing here is particular to any driver but the record it looks for:
// on a GPU that is not the virtual one, no texture carries it, every read
// comes back empty, and frames go to the host as pixels.
class SurfaceIdReader {
public:
    explicit SurfaceIdReader(ID3D11Device* device);
    ~SurfaceIdReader();

    SurfaceIdReader(const SurfaceIdReader&) = delete;
    SurfaceIdReader& operator=(const SurfaceIdReader&) = delete;
    SurfaceIdReader(SurfaceIdReader&&) = delete;
    SurfaceIdReader& operator=(SurfaceIdReader&&) = delete;

    [[nodiscard]] bool usable() const noexcept { return kernel_ != nullptr && device_ != 0; }

    // The id of the texture ``shared_handle`` names - a legacy shared handle,
    // IDXGIResource::GetSharedHandle's - or nothing when it carries none.
    [[nodiscard]] std::optional<std::uint64_t> id_of(void* shared_handle) const;

private:
    // gdi32's D3DKMT entry points.
    struct Kernel;

    std::unique_ptr<const Kernel> kernel_;
    // D3DKMT_HANDLEs, which are 32-bit. 0 is none.
    std::uint32_t adapter_ = 0;
    std::uint32_t device_ = 0;
};

}  // namespace rwin::guest
