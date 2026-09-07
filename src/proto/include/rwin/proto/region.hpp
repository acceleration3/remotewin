#pragma once

#include "rwin/proto/byte_ring.hpp"
#include "rwin/proto/frame_ring.hpp"

#include <cstddef>
#include <cstdint>

// The shared-memory region, divided into channels.
//
// The region started as one thing: a ring of window-frame slots. That
// was enough while pixels were the only payload, but the missing
// vhost_vsock pushed the RPC channel onto TCP-over-hostfwd, and the
// Vulkan command stream — the point of the whole project — has nowhere
// to go at all. Both want exactly what the frames already have: shared
// pages between host and guest.
//
// So the region now begins with a channel table, and the frame ring is
// one channel among several rather than the whole region:
//
//   0x0000  RegionHeader
//   0x0040  channel table (kMaxChannels x ChannelDescriptor)
//   0x1000  first channel
//           ...
//
// Every channel's contents are addressed RELATIVE TO ITS OWN BASE, so a
// channel can be moved or resized by changing one table entry. Both
// peers locate a channel by kind, never by a hardcoded offset.
//
// Normative prose: docs/link-protocol.md.

namespace rwin::proto {

// What a channel carries. Values are a wire contract; never renumber.
inline constexpr std::uint32_t kChannelFrames = 1;     // window frame slots + pixel buffers
inline constexpr std::uint32_t kChannelRpc = 2;        // duplex control messages
// 3 and 4 were the Vulkan proxy's command stream and its host-memory
// arena. The proxy is the render server's now, in a region of its own,
// and this one carries frames and control traffic. Retired rather than
// reused: the values are a wire contract.

struct ChannelDescriptor {
    std::uint32_t kind;      // 0x00  one of the kChannel* values
    std::uint32_t flags;     // 0x04  reserved
    std::uint64_t offset;    // 0x08  region-relative start
    std::uint64_t size;      // 0x10  bytes
    std::uint64_t reserved;  // 0x18
};

struct RegionHeader {
    std::uint32_t magic;                // 0x00  'RWIN' little-endian
    std::uint32_t version;              // 0x04  kRegionVersion
    std::uint32_t channel_count;        // 0x08  live entries in the table
    std::uint32_t channel_entry_size;   // 0x0C  sizeof(ChannelDescriptor)
    std::uint64_t channel_table_offset; // 0x10  region-relative
    std::uint64_t region_size;          // 0x18
    std::uint64_t reserved[4];          // 0x20
};

// Region format version. Bumped from 1 (single implicit frame channel)
// to 2 (channel table) — the magic is deliberately unchanged, so an old
// guest agent mapping a new region reads a version it does not know and
// refuses cleanly instead of interpreting the channel table as a slot
// table.
inline constexpr std::uint32_t kRegionVersion = 3;

inline constexpr std::uint64_t kChannelTableOffset = 0x40;
inline constexpr std::uint32_t kMaxChannels = 8;
// First channel starts on a page boundary, after the table.
inline constexpr std::uint64_t kFirstChannelOffset = 0x1000;

static_assert(sizeof(RegionHeader) == 64);
static_assert(offsetof(RegionHeader, magic) == 0x00);
static_assert(offsetof(RegionHeader, version) == 0x04);
static_assert(offsetof(RegionHeader, channel_count) == 0x08);
static_assert(offsetof(RegionHeader, channel_entry_size) == 0x0C);
static_assert(offsetof(RegionHeader, channel_table_offset) == 0x10);
static_assert(offsetof(RegionHeader, region_size) == 0x18);
static_assert(sizeof(RegionHeader) <= kChannelTableOffset,
              "the header must fit before the channel table");

static_assert(sizeof(ChannelDescriptor) == 32);
static_assert(offsetof(ChannelDescriptor, kind) == 0x00);
static_assert(offsetof(ChannelDescriptor, offset) == 0x08);
static_assert(offsetof(ChannelDescriptor, size) == 0x10);
static_assert(kChannelTableOffset + (kMaxChannels * sizeof(ChannelDescriptor))
                  <= kFirstChannelOffset,
              "the channel table must fit before the first channel");

namespace region_off {
inline constexpr std::size_t magic                = offsetof(RegionHeader, magic);
inline constexpr std::size_t version              = offsetof(RegionHeader, version);
inline constexpr std::size_t channel_count        = offsetof(RegionHeader, channel_count);
inline constexpr std::size_t channel_entry_size   = offsetof(RegionHeader, channel_entry_size);
inline constexpr std::size_t channel_table_offset = offsetof(RegionHeader, channel_table_offset);
inline constexpr std::size_t region_size          = offsetof(RegionHeader, region_size);
}  // namespace region_off

namespace channel_off {
inline constexpr std::size_t kind   = offsetof(ChannelDescriptor, kind);
inline constexpr std::size_t offset = offsetof(ChannelDescriptor, offset);
inline constexpr std::size_t size   = offsetof(ChannelDescriptor, size);
}  // namespace channel_off

// ---- Default sizes ------------------------------------------------------
// Only the manager uses these: it lays the region out and writes the
// table, and the guest reads whatever the table says. They are here so
// the two ends' expectations are at least written down in one file.

inline constexpr std::uint64_t kDefaultRpcChannelBytes = 1ULL << 20;        // 1 MiB

// ---- Doorbell vectors ---------------------------------------------------
// The guest signals the host with an IVSHMEM MSI-X doorbell. Vectors
// 0..kSlotCount-1 belong to frame slots (vector == slot index, as
// before); the channels get their own past the end, so a ring
// notification can never be mistaken for a frame.

inline constexpr std::uint16_t kRpcDoorbellVector =
    static_cast<std::uint16_t>(kSlotCount);
// Total vectors the device must expose. Mirrored by <msi vectors='N'/>
// in domain.xml and by IvshmemServer::kVectorCount.
inline constexpr std::uint16_t kDoorbellVectorCount =
    static_cast<std::uint16_t>(kSlotCount + 1);

// ---- Duplex layout ------------------------------------------------------
// A duplex channel is two independent rings back to back, each getting
// half the channel. Direction names are absolute, not relative to the
// speaker, so both sides read the same way.

[[nodiscard]] inline constexpr std::uint64_t guest_to_host_ring_offset(std::uint64_t) {
    return 0;
}
[[nodiscard]] inline constexpr std::uint64_t host_to_guest_ring_offset(
    std::uint64_t channel_size) {
    return channel_size / 2;
}
[[nodiscard]] inline constexpr std::uint64_t duplex_ring_bytes(std::uint64_t channel_size) {
    return channel_size / 2;
}

// ---- Lookup -------------------------------------------------------------

struct ChannelView {
    std::uint32_t kind = 0;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;

    [[nodiscard]] bool valid() const noexcept { return size != 0; }
};

// Find a channel by kind. Returns an invalid view when the region has no
// such channel — which is the expected answer for an older peer, not an
// error.
[[nodiscard]] inline ChannelView find_channel(const void* region_base,
                                              std::uint32_t kind) {
    ChannelView out;
    if (region_base == nullptr) {
        return out;
    }
    const auto* base = static_cast<const std::uint8_t*>(region_base);
    const auto count = load_field<std::uint32_t>(base + region_off::channel_count);
    const auto entry_size = load_field<std::uint32_t>(base + region_off::channel_entry_size);
    const auto table = load_field<std::uint64_t>(base + region_off::channel_table_offset);
    if (count == 0 || count > kMaxChannels || entry_size != sizeof(ChannelDescriptor)) {
        return out;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* entry = base + table + (static_cast<std::uint64_t>(i) * entry_size);
        if (load_field<std::uint32_t>(entry + channel_off::kind) != kind) {
            continue;
        }
        out.kind = kind;
        out.offset = load_field<std::uint64_t>(entry + channel_off::offset);
        out.size = load_field<std::uint64_t>(entry + channel_off::size);
        return out;
    }
    return out;
}

// Write one channel-table entry. Manager-side only.
inline void write_channel(void* region_base, std::uint32_t index, std::uint32_t kind,
                          std::uint64_t offset, std::uint64_t size) {
    auto* entry = static_cast<std::uint8_t*>(region_base) + kChannelTableOffset
                  + (static_cast<std::uint64_t>(index) * sizeof(ChannelDescriptor));
    store_field<std::uint32_t>(entry + channel_off::kind, kind);
    store_field<std::uint32_t>(entry + offsetof(ChannelDescriptor, flags), 0U);
    store_field<std::uint64_t>(entry + channel_off::offset, offset);
    store_field<std::uint64_t>(entry + channel_off::size, size);
    store_field<std::uint64_t>(entry + offsetof(ChannelDescriptor, reserved), 0ULL);
}

}  // namespace rwin::proto
