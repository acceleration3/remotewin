#include "guest/capture/picture_room.hpp"

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char* label) {
    if (ok) return;
    std::printf("FAIL: %s\n", label);
    ++failures;
}

using rwin::guest::CaptureStream;
using rwin::guest::room_for;
using rwin::guest::room_holds;

}

int main() {
    const CaptureStream::Extent room = room_for({960, 720});
    check(room == CaptureStream::Extent{1152, 896}, "a picture's room is it rounded up to 128 and 128 more");
    check(room_holds(room, {960, 720}), "the room holds its picture");
    int remade = 0;
    CaptureStream::Extent current = room;
    for (std::uint32_t step = 1; step <= 30; ++step) {
        const CaptureStream::Extent picture{960 + 8 * step, 720 + 6 * step};
        if (room_holds(current, picture)) continue;
        current = room_for(picture);
        ++remade;
    }
    check(remade <= 2, "a drag of 240 by 180 makes the room again twice at most");
    check(room_holds(room, {700, 500}), "a window that shrank a little keeps its room");
    check(!room_holds(room, {400, 300}), "one that shrank a long way is given a smaller one");
    check(!room_holds(room, {1153, 700}), "a picture larger than the room is not held");
    check(room_for({16384, 100}).width == 16384, "no room is wider than a texture can be");
    check(room_for({20000, 100}).width == 20000, "a picture beyond that keeps its own size");
    if (failures == 0) std::printf("picture_room: all passed\n");
    return failures == 0 ? 0 : 1;
}
