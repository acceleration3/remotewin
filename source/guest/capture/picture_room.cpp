#include "guest/capture/picture_room.hpp"

#include <algorithm>

namespace rwin::guest {

namespace {

std::uint32_t side_for(std::uint32_t picture) noexcept {
    const std::uint32_t rounded = (picture + kRoomStep - 1) / kRoomStep * kRoomStep;
    return std::min(rounded + kRoomStep, std::max(picture, kLargestRoom));
}

bool side_holds(std::uint32_t room, std::uint32_t picture) noexcept {
    return picture <= room && room - picture <= kRoomSlack;
}

}

CaptureStream::Extent room_for(CaptureStream::Extent picture) noexcept {
    return CaptureStream::Extent{side_for(picture.width), side_for(picture.height)};
}

bool room_holds(CaptureStream::Extent room, CaptureStream::Extent picture) noexcept {
    return side_holds(room.width, picture.width) && side_holds(room.height, picture.height);
}

}
