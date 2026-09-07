#pragma once

#include <QString>

#include <cstdint>

namespace rwin::host {

// One guest window as it arrived on the wire. These fields always travel
// together, so they are one thing.
struct GuestWindowInfo {
    std::uint64_t handle = 0;
    std::uint64_t owner = 0;
    std::uint32_t process_id = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t style = 0;
    std::uint32_t extended_style = 0;
    QString title;
    QString class_name;
};

}  // namespace rwin::host
