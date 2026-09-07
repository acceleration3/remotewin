#pragma once

#include "rwin/proto/region_fence.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

// The IVSHMEM window-frame ring, described once for both sides.
//
// The manager creates and mmaps the region (POSIX shm handed to QEMU);
// the guest agent maps the same memory through the IVSHMEM PCI BAR. The
// layout used to be two independent lists of magic byte offsets, one per
// side, and they had already diverged: the agent knew about
// ``present_qpc`` and hardcoded ``0x100`` for the slot table, the manager
// knew about ``slot_size``/``region_size`` and the agent did not.
//
// Here the layout is a struct, the offsets come from ``offsetof``, and
// the values every peer must agree on are static_asserted. Editing the
// struct without editing the wire contract is now a compile error.
//
// Normative prose: docs/link-protocol.md.

namespace rwin::proto {

// Region header, at offset 0.
struct FrameRingHeader {
    std::uint32_t magic;           // 0x00  'RWIN' little-endian
    std::uint32_t version;         // 0x04
    std::uint32_t slot_count;      // 0x08
    std::uint32_t slot_size;       // 0x0C  bytes per slot table entry
    std::uint64_t table_offset;    // 0x10  region-relative
    std::uint64_t buffers_offset;  // 0x18  region-relative
    std::uint64_t buffers_size;    // 0x20
    std::uint64_t region_size;     // 0x28
};

// One slot-table entry: the metadata for one mirrored window.
//
// ``buffer_offset`` names a pair of buffers (front + back) each
// ``buffer_size / 2`` bytes. Which half is the readable front is encoded
// in bit 1 of ``seqno``; see the seqlock helpers below.
// ``visible_insets`` is how much of the frame is NOT on the guest's
// screen, one byte per edge, packed left | top<<8 | right<<16 |
// bottom<<24. Zero for almost every window; a maximized one is the
// exception, since Windows grows its frame past the monitor by the
// invisible resize border and the application paints nothing out there.
//
// It rides with the pixels deliberately. The same numbers are on the
// enumeration channel, and taking them from there meant pairing a
// rectangle from one channel with an image from another: for the frame
// or two after a maximize the two disagreed, and the mirror cropped a
// live frame by a stale rectangle — a chopped-off title bar, sometimes,
// depending on which arrived first. Written under the same seqlock as
// the frame it describes, the pair cannot be torn.
//
// One byte per edge is deliberate too: it fits the word that was
// reserved, so the slot stays 64 bytes and a peer that predates the
// field reads the zero that was always there and behaves as before.
struct FrameSlot {
    std::uint64_t hwnd;           // 0x00  0 = empty slot
    std::uint32_t state;          // 0x08  SlotState
    std::uint32_t format;         // 0x0C  SlotFormat
    std::uint32_t width;          // 0x10
    std::uint32_t stride;         // 0x14  bytes per row
    std::uint32_t height;         // 0x18
    std::uint32_t visible_insets; // 0x1C  packed l|t<<8|r<<16|b<<24
    std::uint64_t buffer_offset;  // 0x20  region-relative, front+back pair
    std::uint64_t buffer_size;    // 0x28  total of both halves
    std::uint64_t seqno;          // 0x30  seqlock; see below
    std::uint64_t present_qpc;    // 0x38  agent QPC when the frame was ready
};

// Values of FrameSlot::state. u32 because that is the wire width.
inline constexpr std::uint32_t kSlotStateEmpty    = 0;
inline constexpr std::uint32_t kSlotStateActive   = 1;
inline constexpr std::uint32_t kSlotStateResizing = 2;

// Values of FrameSlot::format.
inline constexpr std::uint32_t kFormatBgra8Premul = 1;

inline constexpr std::uint32_t kFrameRingMagic   = 0x4E495752U;  // 'RWIN' LE
inline constexpr std::uint32_t kFrameRingVersion = 1;
inline constexpr std::uint32_t kSlotCount        = 64;
inline constexpr std::uint64_t kTableOffset      = 0x100ULL;
inline constexpr std::uint64_t kBuffersOffset =
    kTableOffset + (static_cast<std::uint64_t>(kSlotCount) * sizeof(FrameSlot));

// ---- Wire contract ------------------------------------------------------
// Every one of these was a hand-written magic number on each side of the
// VM boundary. They are asserted, not documented, so the two peers cannot
// drift apart silently again.

static_assert(sizeof(FrameSlot) == 64, "slot table entries are 64 bytes on the wire");
static_assert(offsetof(FrameSlot, hwnd)          == 0x00);
static_assert(offsetof(FrameSlot, state)         == 0x08);
static_assert(offsetof(FrameSlot, format)        == 0x0C);
static_assert(offsetof(FrameSlot, width)         == 0x10);
static_assert(offsetof(FrameSlot, stride)        == 0x14);
static_assert(offsetof(FrameSlot, height)        == 0x18);
static_assert(offsetof(FrameSlot, visible_insets) == 0x1C);
static_assert(offsetof(FrameSlot, buffer_offset) == 0x20);
static_assert(offsetof(FrameSlot, buffer_size)   == 0x28);
static_assert(offsetof(FrameSlot, seqno)         == 0x30);
static_assert(offsetof(FrameSlot, present_qpc)   == 0x38);

static_assert(offsetof(FrameRingHeader, magic)          == 0x00);
static_assert(offsetof(FrameRingHeader, version)        == 0x04);
static_assert(offsetof(FrameRingHeader, slot_count)     == 0x08);
static_assert(offsetof(FrameRingHeader, slot_size)      == 0x0C);
static_assert(offsetof(FrameRingHeader, table_offset)   == 0x10);
static_assert(offsetof(FrameRingHeader, buffers_offset) == 0x18);
static_assert(offsetof(FrameRingHeader, buffers_size)   == 0x20);
static_assert(offsetof(FrameRingHeader, region_size)    == 0x28);
static_assert(sizeof(FrameRingHeader) <= kTableOffset,
              "header must fit before the slot table");

// ---- Field offsets ------------------------------------------------------
// Both peers touch the region through byte offsets (see below), so the
// offsets get names — derived from the structs, never typed out twice.

namespace slot_off {
inline constexpr std::size_t hwnd          = offsetof(FrameSlot, hwnd);
inline constexpr std::size_t state         = offsetof(FrameSlot, state);
inline constexpr std::size_t format        = offsetof(FrameSlot, format);
inline constexpr std::size_t width         = offsetof(FrameSlot, width);
inline constexpr std::size_t stride        = offsetof(FrameSlot, stride);
inline constexpr std::size_t height        = offsetof(FrameSlot, height);
inline constexpr std::size_t visible_insets = offsetof(FrameSlot, visible_insets);
inline constexpr std::size_t buffer_offset = offsetof(FrameSlot, buffer_offset);
inline constexpr std::size_t buffer_size   = offsetof(FrameSlot, buffer_size);
inline constexpr std::size_t seqno         = offsetof(FrameSlot, seqno);
inline constexpr std::size_t present_qpc   = offsetof(FrameSlot, present_qpc);
}  // namespace slot_off

namespace hdr_off {
inline constexpr std::size_t magic          = offsetof(FrameRingHeader, magic);
inline constexpr std::size_t version        = offsetof(FrameRingHeader, version);
inline constexpr std::size_t slot_count     = offsetof(FrameRingHeader, slot_count);
inline constexpr std::size_t slot_size      = offsetof(FrameRingHeader, slot_size);
inline constexpr std::size_t table_offset   = offsetof(FrameRingHeader, table_offset);
inline constexpr std::size_t buffers_offset = offsetof(FrameRingHeader, buffers_offset);
inline constexpr std::size_t buffers_size   = offsetof(FrameRingHeader, buffers_size);
inline constexpr std::size_t region_size    = offsetof(FrameRingHeader, region_size);
}  // namespace hdr_off

// ---- Untyped access -----------------------------------------------------
// The region is mapped write-combined on the guest and shared across a VM
// boundary, so both sides touch it through memcpy on a byte pointer rather
// than through the struct type. The structs above exist to pin the layout;
// these helpers are how it is actually read and written.

template <typename T>
[[nodiscard]] inline T load_field(const void* p) noexcept {
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

template <typename T>
inline void store_field(void* p, T v) noexcept {
    std::memcpy(p, &v, sizeof(T));
}

// Byte address of slot ``index`` within a mapped region.
[[nodiscard]] inline std::uint8_t* slot_ptr(void* region_base, std::uint32_t index) noexcept {
    return static_cast<std::uint8_t*>(region_base) + kTableOffset
           + (static_cast<std::uint64_t>(index) * sizeof(FrameSlot));
}

[[nodiscard]] inline const std::uint8_t* slot_ptr(const void* region_base,
                                                  std::uint32_t index) noexcept {
    return static_cast<const std::uint8_t*>(region_base) + kTableOffset
           + (static_cast<std::uint64_t>(index) * sizeof(FrameSlot));
}

// ---- Seqlock ------------------------------------------------------------
// ``seqno`` carries two flags in its low bits:
//   bit 0  set while a write is in progress (readers retry)
//   bit 1  selects which half of the buffer pair is the readable front
// The counter proper lives above them and increases by 4 per commit.

[[nodiscard]] inline std::uint64_t load_seqno_acquire(const void* p) noexcept {
    // Fence AFTER the load, not before: what has to be ordered is this
    // read of the sequence number against the reads of the payload it
    // guards. A fence in front of it orders the wrong pair.
    const std::uint64_t v = load_field<std::uint64_t>(p);
    ::rwin::proto::consume_fence();
    return v;
}

inline void store_seqno_release(void* p, std::uint64_t v) noexcept {
    ::rwin::proto::publish_fence();
    store_field<std::uint64_t>(p, v);
}

[[nodiscard]] inline bool seqno_is_writing(std::uint64_t seqno) noexcept {
    return (seqno & 1ULL) != 0ULL;
}

// True when the front buffer is the SECOND half of the pair.
[[nodiscard]] inline bool seqno_front_is_high_half(std::uint64_t seqno) noexcept {
    return (seqno & 2ULL) != 0ULL;
}

// The seqno a producer commits after writing the other half: counter
// advanced, write bit clear, half-selector flipped.
[[nodiscard]] inline std::uint64_t seqno_next_commit(std::uint64_t seqno) noexcept {
    const std::uint64_t counter = seqno & ~3ULL;
    const std::uint64_t flipped_half = (seqno & 2ULL) ^ 2ULL;
    return counter + 4ULL + flipped_half;
}

// The value written before touching pixels: the committed seqno with the
// in-progress bit set.
[[nodiscard]] inline std::uint64_t seqno_begin_write(std::uint64_t next_commit) noexcept {
    return next_commit | 1ULL;
}

// Offset of the half a given seqno designates, relative to a slot's
// ``buffer_offset``. Pass the seqno being committed to get the WRITE half
// (the selector already points at the freshly written buffer); pass the
// last committed seqno to get the READ half.
[[nodiscard]] inline std::uint64_t half_offset(std::uint64_t seqno,
                                               std::uint64_t buffer_size) noexcept {
    return seqno_front_is_high_half(seqno) ? (buffer_size / 2ULL) : 0ULL;
}

}  // namespace rwin::proto
