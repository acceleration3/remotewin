#pragma once

#include "rwin/proto/frame_ring.hpp"  // load_field / store_field

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

// A single-producer / single-consumer message ring in shared memory.
//
// The frame ring solved one problem — get pixels across the VM boundary
// without copying them through a socket. This is the general form: an
// ordered byte channel between exactly one writer and one reader, living
// in the same IVSHMEM region. Two of them back to back make a duplex
// channel (see region.hpp), which is what a command stream or an RPC
// channel needs.
//
// Design constraints that are not negotiable here:
//
//   * The two ends are in different address spaces, in different
//     operating systems, and one of them maps the memory write-combined.
//     So: no pointers in the shared structs, no std::atomic objects in
//     the shared structs (their layout is not a wire contract), memcpy
//     for every access, explicit fences for ordering.
//   * Exactly one producer and one consumer per ring. ``write_pos`` is
//     written only by the producer, ``read_pos`` only by the consumer;
//     each reads the other's. That is what makes the ring lock-free
//     without a CAS.
//   * Messages, not bytes. A reader must never see half a message, so
//     every record is length-prefixed and writes are all-or-nothing: a
//     message that does not fit is refused outright rather than
//     partially committed.

namespace rwin::proto {

// Ring control block. Sits at the start of the ring's area; the data
// area follows immediately.
struct ByteRingHeader {
    std::uint64_t write_pos;  // 0x00  producer-owned, monotonic
    std::uint64_t read_pos;   // 0x08  consumer-owned, monotonic
    std::uint32_t capacity;   // 0x10  data-area bytes, power of two
    std::uint32_t flags;      // 0x14  reserved
    std::uint64_t dropped;    // 0x18  messages refused for lack of room
    std::uint64_t reserved[4];// 0x20
};

static_assert(sizeof(ByteRingHeader) == 64, "ring header is one cache line");
static_assert(offsetof(ByteRingHeader, write_pos) == 0x00);
static_assert(offsetof(ByteRingHeader, read_pos) == 0x08);
static_assert(offsetof(ByteRingHeader, capacity) == 0x10);
static_assert(offsetof(ByteRingHeader, dropped) == 0x18);

namespace ring_off {
inline constexpr std::size_t write_pos = offsetof(ByteRingHeader, write_pos);
inline constexpr std::size_t read_pos  = offsetof(ByteRingHeader, read_pos);
inline constexpr std::size_t capacity  = offsetof(ByteRingHeader, capacity);
inline constexpr std::size_t dropped   = offsetof(ByteRingHeader, dropped);
}  // namespace ring_off

// Every record is [len:u32][payload:len], both in native byte order —
// the two peers are always the same architecture, sharing the same
// physical pages. (The socket envelope is big-endian because it is a
// network protocol; this is memory.)
inline constexpr std::size_t kRingRecordHeaderSize = sizeof(std::uint32_t);

// Largest message a ring of ``capacity`` bytes can ever hold. Capped
// well below capacity so a single huge message cannot make the ring
// permanently unusable for anything else.
[[nodiscard]] inline constexpr std::uint32_t ring_max_message(std::uint32_t capacity) {
    return (capacity / 2) - static_cast<std::uint32_t>(kRingRecordHeaderSize);
}

// Largest power of two that fits in ``area_bytes`` after the header.
// Returns 0 if the area cannot hold a usable ring.
[[nodiscard]] inline std::uint32_t ring_capacity_for(std::uint64_t area_bytes) {
    if (area_bytes <= sizeof(ByteRingHeader)) {
        return 0;
    }
    const std::uint64_t usable = area_bytes - sizeof(ByteRingHeader);
    std::uint32_t capacity = 1;
    while ((static_cast<std::uint64_t>(capacity) << 1) <= usable
           && capacity < (1U << 31)) {
        capacity <<= 1;
    }
    return capacity < 64 ? 0 : capacity;
}

// Zero a ring's control block and stamp its capacity. Called once, by
// whichever side creates the region (the manager), before the other side
// maps it.
inline bool ring_init(void* ring_base, std::uint64_t area_bytes) {
    const std::uint32_t capacity = ring_capacity_for(area_bytes);
    if (capacity == 0) {
        return false;
    }
    auto* base = static_cast<std::uint8_t*>(ring_base);
    std::memset(base, 0, sizeof(ByteRingHeader));
    store_field<std::uint32_t>(base + ring_off::capacity, capacity);
    ::rwin::proto::publish_fence();
    return true;
}

// Common half of both endpoints: locating the header and the data area.
class ByteRingEndpoint {
public:
    ByteRingEndpoint() = default;
    explicit ByteRingEndpoint(void* ring_base)
        : base_(static_cast<std::uint8_t*>(ring_base)) {
        if (base_ != nullptr) {
            capacity_ = load_field<std::uint32_t>(base_ + ring_off::capacity);
            // A capacity that is not a power of two means the region was
            // never initialised (or was initialised by something that
            // does not share this contract). Refuse rather than compute
            // wrong indices forever.
            if (capacity_ == 0 || (capacity_ & (capacity_ - 1)) != 0) {
                base_ = nullptr;
                capacity_ = 0;
            }
        }
    }

    [[nodiscard]] bool valid() const noexcept { return base_ != nullptr; }
    [[nodiscard]] std::uint32_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::uint32_t max_message() const noexcept {
        return capacity_ == 0 ? 0 : ring_max_message(capacity_);
    }

protected:
    [[nodiscard]] std::uint8_t* data() const noexcept {
        return base_ + sizeof(ByteRingHeader);
    }
    [[nodiscard]] std::uint64_t mask() const noexcept {
        return static_cast<std::uint64_t>(capacity_) - 1;
    }

    // Copy in/out of the data area, splitting at the wrap point.
    void copy_in(std::uint64_t pos, const std::uint8_t* src, std::size_t len) const {
        const std::uint64_t start = pos & mask();
        const std::size_t first = std::min<std::size_t>(len, capacity_ - start);
        std::memcpy(data() + start, src, first);
        if (first < len) {
            std::memcpy(data(), src + first, len - first);
        }
    }
    void copy_out(std::uint64_t pos, std::uint8_t* dst, std::size_t len) const {
        const std::uint64_t start = pos & mask();
        const std::size_t first = std::min<std::size_t>(len, capacity_ - start);
        std::memcpy(dst, data() + start, first);
        if (first < len) {
            std::memcpy(dst + first, data(), len - first);
        }
    }

    std::uint8_t* base_ = nullptr;
    std::uint32_t capacity_ = 0;
};

// The writing end. One per ring, ever.
class ByteRingProducer : public ByteRingEndpoint {
public:
    using ByteRingEndpoint::ByteRingEndpoint;

    // Append one message. All-or-nothing: false means the ring did not
    // have room and NOTHING was written, so the caller can retry or drop
    // deliberately. A message larger than ``max_message`` can never fit
    // and is refused immediately.
    [[nodiscard]] bool write(const std::uint8_t* payload, std::size_t len) {
        if (!valid() || len > max_message()) {
            note_drop();
            return false;
        }
        const std::size_t total = kRingRecordHeaderSize + len;
        const std::uint64_t write_pos = load_field<std::uint64_t>(base_ + ring_off::write_pos);
        ::rwin::proto::consume_fence();
        const std::uint64_t read_pos = load_field<std::uint64_t>(base_ + ring_off::read_pos);
        if (capacity_ - (write_pos - read_pos) < total) {
            note_drop();
            return false;
        }

        const auto len32 = static_cast<std::uint32_t>(len);
        std::uint8_t header[kRingRecordHeaderSize];
        std::memcpy(header, &len32, sizeof(len32));
        copy_in(write_pos, header, sizeof(header));
        if (len > 0) {
            copy_in(write_pos + kRingRecordHeaderSize, payload, len);
        }

        // Publish only after the payload is in memory: the consumer
        // treats any advance of write_pos as "a whole record is there".
        ::rwin::proto::publish_fence();
        store_field<std::uint64_t>(base_ + ring_off::write_pos, write_pos + total);
        return true;
    }

    // How many messages this producer has refused for lack of room.
    // Worth logging: a channel that silently drops is indistinguishable
    // from one that is idle.
    [[nodiscard]] std::uint64_t dropped() const {
        return valid() ? load_field<std::uint64_t>(base_ + ring_off::dropped) : 0;
    }

private:
    void note_drop() {
        if (valid()) {
            store_field<std::uint64_t>(base_ + ring_off::dropped,
                                       load_field<std::uint64_t>(base_ + ring_off::dropped) + 1);
        }
    }
};

// The reading end. One per ring, ever.
class ByteRingConsumer : public ByteRingEndpoint {
public:
    using ByteRingEndpoint::ByteRingEndpoint;

    [[nodiscard]] bool empty() const {
        if (!valid()) {
            return true;
        }
        const std::uint64_t read_pos = load_field<std::uint64_t>(base_ + ring_off::read_pos);
        ::rwin::proto::consume_fence();
        return load_field<std::uint64_t>(base_ + ring_off::write_pos) == read_pos;
    }

    // Size of the next message, without consuming it. 0 when the ring is
    // empty.
    [[nodiscard]] std::uint32_t peek_size() const {
        if (!valid()) {
            return 0;
        }
        const std::uint64_t read_pos = load_field<std::uint64_t>(base_ + ring_off::read_pos);
        ::rwin::proto::consume_fence();
        const std::uint64_t write_pos = load_field<std::uint64_t>(base_ + ring_off::write_pos);
        if (write_pos - read_pos < kRingRecordHeaderSize) {
            return 0;
        }
        std::uint8_t header[kRingRecordHeaderSize];
        copy_out(read_pos, header, sizeof(header));
        std::uint32_t len = 0;
        std::memcpy(&len, header, sizeof(len));
        return len;
    }

    // Pop one message into ``out``. Returns the number of bytes written,
    // or 0 when the ring is empty. If the message does not fit in
    // ``out_capacity`` it is DROPPED (and 0 returned) rather than
    // truncated or left to block the ring forever — the caller sizes its
    // buffer from ``max_message``.
    [[nodiscard]] std::size_t read(std::uint8_t* out, std::size_t out_capacity) {
        if (!valid()) {
            return 0;
        }
        const std::uint64_t read_pos = load_field<std::uint64_t>(base_ + ring_off::read_pos);
        ::rwin::proto::consume_fence();
        const std::uint64_t write_pos = load_field<std::uint64_t>(base_ + ring_off::write_pos);
        const std::uint64_t available = write_pos - read_pos;
        if (available < kRingRecordHeaderSize) {
            return 0;
        }
        std::uint8_t header[kRingRecordHeaderSize];
        copy_out(read_pos, header, sizeof(header));
        std::uint32_t len = 0;
        std::memcpy(&len, header, sizeof(len));

        // A record claiming more than was published means the two ends
        // have lost sync. There is no honest way to resynchronise a
        // stream of length-prefixed records, and leaving read_pos where
        // it is would wedge the ring forever, so discard everything
        // outstanding and carry on from the producer's position.
        if (available < kRingRecordHeaderSize + len) {
            ::rwin::proto::publish_fence();
            store_field<std::uint64_t>(base_ + ring_off::read_pos, write_pos);
            return 0;
        }
        const std::uint64_t next = read_pos + kRingRecordHeaderSize + len;
        if (len > out_capacity) {
            ::rwin::proto::publish_fence();
            store_field<std::uint64_t>(base_ + ring_off::read_pos, next);
            return 0;
        }
        if (len > 0) {
            copy_out(read_pos + kRingRecordHeaderSize, out, len);
        }
        // Release the space only after the payload has been copied out.
        ::rwin::proto::publish_fence();
        store_field<std::uint64_t>(base_ + ring_off::read_pos, next);
        return len;
    }
};

}  // namespace rwin::proto
