#include "guest/capture/surface_id_reader.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
// clang-format on

#include <array>
#include <cstring>

namespace rwin::guest {

namespace {

// d3dkmthk.h's, which mingw-w64 does not ship. The layouts are the WDK's;
// only what reading an id needs is declared, and only here.
using KmtHandle = UINT;
using KmtStatus = LONG;

struct KmtOpenAdapterFromLuid {
    LUID adapter_luid;
    KmtHandle adapter;
};
struct KmtCloseAdapter {
    KmtHandle adapter;
};
struct KmtCreateDevice {
    union {
        KmtHandle adapter;
        void* adapter_pointer;
    };
    UINT flags;
    KmtHandle device;
    void* command_buffer;
    UINT command_buffer_size;
    void* allocation_list;
    UINT allocation_list_size;
    void* patch_location_list;
    UINT patch_location_list_size;
};
struct KmtDestroyDevice {
    KmtHandle device;
};
struct KmtQueryResourceInfo {
    KmtHandle device;
    KmtHandle global_share;
    void* private_runtime_data;
    UINT private_runtime_data_size;
    UINT total_private_driver_data_size;
    UINT resource_private_driver_data_size;
    UINT allocations;
};
struct KmtOpenAllocationInfo {
    KmtHandle allocation;
    const void* private_driver_data;
    UINT private_driver_data_size;
};
struct KmtOpenResource {
    KmtHandle device;
    KmtHandle global_share;
    UINT allocations;
    KmtOpenAllocationInfo* open_allocation_info;
    void* private_runtime_data;
    UINT private_runtime_data_size;
    void* resource_private_driver_data;
    UINT resource_private_driver_data_size;
    void* total_private_driver_data;
    UINT total_private_driver_data_size;
    KmtHandle resource;
};
struct KmtDestroyAllocation {
    KmtHandle device;
    KmtHandle resource;
    const KmtHandle* allocation_list;
    UINT allocation_count;
};

using OpenAdapterFromLuidEntry = KmtStatus(WINAPI*)(KmtOpenAdapterFromLuid*);
using CloseAdapterEntry = KmtStatus(WINAPI*)(const KmtCloseAdapter*);
using CreateDeviceEntry = KmtStatus(WINAPI*)(KmtCreateDevice*);
using DestroyDeviceEntry = KmtStatus(WINAPI*)(const KmtDestroyDevice*);
using QueryResourceInfoEntry = KmtStatus(WINAPI*)(KmtQueryResourceInfo*);
using OpenResourceEntry = KmtStatus(WINAPI*)(KmtOpenResource*);
using DestroyAllocationEntry = KmtStatus(WINAPI*)(const KmtDestroyAllocation*);

// The allocation's private data as avgpu writes it (accel-virt's
// docs/external-export.md): its 56-byte allocation descriptor, then a
// 40-byte record - magic "AVS\0", version 1, and the id 8 bytes in.
constexpr UINT kDescriptorBytes = 56;
constexpr UINT kRecordBytes = 40;
constexpr std::uint32_t kRecordMagic = 0x00535641;
constexpr std::uint32_t kRecordVersion = 1;
constexpr std::size_t kIdOffset = kDescriptorBytes + 8;
// Larger than any resource's private data a texture of ours has; the sizes
// are asked for first, and one larger than this is not avgpu's texture.
constexpr std::size_t kPrivateBytes = 4096;


std::optional<LUID> adapter_luid(ID3D11Device* device) {
    IDXGIDevice* dxgi = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi)))) return std::nullopt;
    IDXGIAdapter* adapter = nullptr;
    DXGI_ADAPTER_DESC described{};
    const bool found = SUCCEEDED(dxgi->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&described));
    if (adapter != nullptr) adapter->Release();
    dxgi->Release();
    if (!found) return std::nullopt;
    return described.AdapterLuid;
}

}  // namespace

// Looked up in gdi32.dll by name rather than linked: d3d11.dll exports
// functions of the same names that do nothing and say they succeeded
// (D3DKMTCreateDevice among them, handing back device 0), and an import
// library listed before gdi32's on a link line binds them instead.
struct SurfaceIdReader::Kernel {
    // Nothing when gdi32 lacks any of them.
    static std::unique_ptr<const Kernel> load() {
        const HMODULE gdi32 = ::GetModuleHandleW(L"gdi32.dll");
        if (gdi32 == nullptr) return nullptr;
        auto kernel = std::make_unique<Kernel>();
        const bool found = find(gdi32, "D3DKMTOpenAdapterFromLuid", kernel->open_adapter_from_luid)
                           && find(gdi32, "D3DKMTCloseAdapter", kernel->close_adapter)
                           && find(gdi32, "D3DKMTCreateDevice", kernel->create_device)
                           && find(gdi32, "D3DKMTDestroyDevice", kernel->destroy_device)
                           && find(gdi32, "D3DKMTQueryResourceInfo", kernel->query_resource_info)
                           && find(gdi32, "D3DKMTOpenResource", kernel->open_resource)
                           && find(gdi32, "D3DKMTDestroyAllocation", kernel->destroy_allocation);
        if (!found) return nullptr;
        return kernel;
    }

    template <typename Entry>
    static bool find(HMODULE module, const char* name, Entry& entry) {
        entry = reinterpret_cast<Entry>(reinterpret_cast<void*>(::GetProcAddress(module, name)));
        if (entry == nullptr) RWIN_WARN("shared", "gdi32 has no {}", name);
        return entry != nullptr;
    }

    OpenAdapterFromLuidEntry open_adapter_from_luid = nullptr;
    CloseAdapterEntry close_adapter = nullptr;
    CreateDeviceEntry create_device = nullptr;
    DestroyDeviceEntry destroy_device = nullptr;
    QueryResourceInfoEntry query_resource_info = nullptr;
    OpenResourceEntry open_resource = nullptr;
    DestroyAllocationEntry destroy_allocation = nullptr;
};

namespace {

// The open made to read the id is a second hold of this process's on the
// texture, let go of as soon as it is read.
class OpenedResource {
public:
    OpenedResource(DestroyAllocationEntry destroy, KmtHandle device, KmtHandle resource) noexcept
        : destroy_(destroy), device_(device), resource_(resource) {}
    ~OpenedResource() {
        const KmtDestroyAllocation destroyed{device_, resource_, nullptr, 0};
        destroy_(&destroyed);
    }
    OpenedResource(const OpenedResource&) = delete;
    OpenedResource& operator=(const OpenedResource&) = delete;
    OpenedResource(OpenedResource&&) = delete;
    OpenedResource& operator=(OpenedResource&&) = delete;

private:
    DestroyAllocationEntry destroy_;
    KmtHandle device_;
    KmtHandle resource_;
};

}  // namespace

SurfaceIdReader::SurfaceIdReader(ID3D11Device* device) : kernel_(Kernel::load()) {
    if (kernel_ == nullptr) {
        RWIN_WARN("shared", "gdi32 has no D3DKMT entry points here; frames go as pixels");
        return;
    }
    const std::optional<LUID> luid = device != nullptr ? adapter_luid(device) : std::nullopt;
    if (!luid.has_value()) {
        RWIN_WARN("shared", "the Direct3D device names no adapter; frames go as pixels");
        return;
    }
    KmtOpenAdapterFromLuid opened{};
    opened.adapter_luid = *luid;
    if (const KmtStatus status = kernel_->open_adapter_from_luid(&opened); status != 0) {
        RWIN_WARN("shared", "D3DKMTOpenAdapterFromLuid: {:#010x}; frames go as pixels",
                  static_cast<unsigned>(status));
        return;
    }
    adapter_ = opened.adapter;
    KmtCreateDevice created{};
    created.adapter = adapter_;
    if (const KmtStatus status = kernel_->create_device(&created); status != 0 || created.device == 0) {
        RWIN_WARN("shared", "D3DKMTCreateDevice: {:#010x}, device {:#x}; frames go as pixels",
                  static_cast<unsigned>(status), created.device);
        return;
    }
    device_ = created.device;
}

SurfaceIdReader::~SurfaceIdReader() {
    if (device_ != 0) {
        const KmtDestroyDevice destroyed{device_};
        kernel_->destroy_device(&destroyed);
    }
    if (adapter_ != 0) {
        const KmtCloseAdapter closed{adapter_};
        kernel_->close_adapter(&closed);
    }
}

std::optional<std::uint64_t> SurfaceIdReader::id_of(void* shared_handle) const {
    if (!usable()) return std::nullopt;
    const auto global = static_cast<KmtHandle>(reinterpret_cast<std::uintptr_t>(shared_handle));
    KmtQueryResourceInfo sizes{};
    sizes.device = device_;
    sizes.global_share = global;
    if (const KmtStatus status = kernel_->query_resource_info(&sizes); status != 0) {
        RWIN_WARN("shared", "D3DKMTQueryResourceInfo of {:#x}: {:#010x}", global, static_cast<unsigned>(status));
        return std::nullopt;
    }
    if (sizes.allocations != 1 || sizes.total_private_driver_data_size < kDescriptorBytes + kRecordBytes
        || sizes.total_private_driver_data_size > kPrivateBytes || sizes.private_runtime_data_size > kPrivateBytes
        || sizes.resource_private_driver_data_size > kPrivateBytes) {
        RWIN_WARN("shared", "shared texture {:#x}: {} allocation(s), {} bytes of driver data - not the virtual GPU's",
                  global, sizes.allocations, sizes.total_private_driver_data_size);
        return std::nullopt;
    }
    std::array<std::uint8_t, kPrivateBytes> runtime{};
    std::array<std::uint8_t, kPrivateBytes> resource{};
    std::array<std::uint8_t, kPrivateBytes> allocation_data{};
    KmtOpenAllocationInfo allocation{};
    KmtOpenResource open{};
    open.device = device_;
    open.global_share = global;
    open.allocations = 1;
    open.open_allocation_info = &allocation;
    open.private_runtime_data = runtime.data();
    open.private_runtime_data_size = sizes.private_runtime_data_size;
    open.resource_private_driver_data = resource.data();
    open.resource_private_driver_data_size = sizes.resource_private_driver_data_size;
    open.total_private_driver_data = allocation_data.data();
    open.total_private_driver_data_size = sizes.total_private_driver_data_size;
    if (const KmtStatus status = kernel_->open_resource(&open); status != 0) {
        RWIN_WARN("shared", "D3DKMTOpenResource of {:#x}: {:#010x}", global, static_cast<unsigned>(status));
        return std::nullopt;
    }
    const OpenedResource held{kernel_->destroy_allocation, device_, open.resource};
    const auto* record = static_cast<const std::uint8_t*>(allocation.private_driver_data);
    if (record == nullptr || allocation.private_driver_data_size != kDescriptorBytes + kRecordBytes) return std::nullopt;
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint64_t id = 0;
    std::memcpy(&magic, record + kDescriptorBytes, sizeof magic);
    std::memcpy(&version, record + kDescriptorBytes + sizeof magic, sizeof version);
    std::memcpy(&id, record + kIdOffset, sizeof id);
    if (magic != kRecordMagic || version != kRecordVersion || id == 0) {
        RWIN_WARN("shared", "shared texture {:#x} carries no surface id (magic {:#010x} version {})", global, magic,
                  version);
        return std::nullopt;
    }
    return id;
}

}  // namespace rwin::guest
