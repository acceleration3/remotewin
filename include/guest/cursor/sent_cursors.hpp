#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rwin::guest {

// The cursors whose pictures the host has been sent, by id, at most
// kCapacity of them: the one shown least recently is forgotten first, and
// sent again if it comes back.
//
// The host keeps more than this, in the same least-recently-shown order
// (EmulatedCursor), so a cursor this says the host has, it has.
class SentCursors {
public:
    static constexpr std::size_t kCapacity = 64;

    // Whether ``id`` was sent, and kept; as shown now either way.
    bool shown(std::uint64_t id);

private:
    // Least recently shown first.
    std::vector<std::uint64_t> ids_;
};

}  // namespace rwin::guest
