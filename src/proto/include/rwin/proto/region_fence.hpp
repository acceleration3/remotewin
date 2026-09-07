#pragma once

#include <atomic>

namespace rwin::proto {

// Fences for data that crosses through the shared region.
//
// The guest maps the IVSHMEM BAR write-combined (see
// ``protocol/src/ivshmem_region.cpp``), and write-combining is the one
// x86 memory type that is genuinely weakly ordered — in BOTH directions.
// Stores sit in the WC buffers and may reach memory late and out of
// order; loads may likewise be reordered against each other.
//
// ``std::atomic_thread_fence`` does not help with that. On x86 a release
// fence compiles to no instruction at all and an acquire fence likewise:
// they constrain the compiler, and the hardware needs no more than that
// for ordinary write-back memory. For WC memory it needs SFENCE and
// LFENCE, which are the only things that drain the write-combining
// buffers and serialise loads.
//
// So a producer that writes a record and then publishes the index
// pointing at it has, on paper, published nothing: the consumer may
// observe the new index while the record's bytes are still in a WC
// buffer, and parse whatever was there before. The failure is
// intermittent by nature — the buffers drain on their own within
// microseconds, so it looks like everything works until a frame lands
// in the wrong micro-moment and the consumer reads a stale texture or a
// half-written command.
//
// Both fences below keep the ``atomic_thread_fence`` as well: it is what
// stops the COMPILER reordering the stores, which the CPU instruction
// says nothing about.
// The instructions are written as inline asm rather than through
// ``__builtin_ia32_sfence`` / ``_mm_sfence``: those are gated on the
// compiler having SSE enabled for the target, which the 32-bit guest
// build does not by default, and the failure is a build error in a
// header every module includes. Both instructions exist on every CPU
// that can run this software.
inline void publish_fence() noexcept {
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    __asm__ __volatile__("sfence" ::: "memory");
#endif
    std::atomic_thread_fence(std::memory_order_release);
}

inline void consume_fence() noexcept {
    std::atomic_thread_fence(std::memory_order_acquire);
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    __asm__ __volatile__("lfence" ::: "memory");
#endif
}

}  // namespace rwin::proto
