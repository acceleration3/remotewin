#include "host/cursor/emulated_cursor.hpp"
#include "proto/cursor_picture.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

rwin::CursorPicture picture(std::uint64_t id) {
    rwin::CursorPicture picture;
    picture.id = id;
    rwin::CursorPicture::Frame frame;
    frame.width = 1;
    frame.height = 1;
    frame.bgra = {0, 0, 0, 255};
    picture.frames.push_back(frame);
    return picture;
}

using rwin::host::EmulatedCursor;

void the_cursor_shown_longest_ago_goes_first() {
    EmulatedCursor cursors;
    for (std::uint64_t id = 1; id <= EmulatedCursor::kCapacity; ++id) cursors.remember(picture(id));
    check(cursors.size() == EmulatedCursor::kCapacity, "every picture is kept up to the bound");
    check(cursors.show(1) != nullptr, "the first, shown again, is the newest shown");
    cursors.remember(picture(EmulatedCursor::kCapacity + 1));
    check(cursors.size() == EmulatedCursor::kCapacity, "a picture past the bound lets one go");
    check(cursors.find(1) != nullptr, "not the one just shown");
    check(cursors.find(2) == nullptr, "the one shown longest ago");
}

void the_cursor_worn_stays() {
    EmulatedCursor cursors;
    cursors.remember(picture(1));
    const rwin::host::CursorLook* worn = cursors.show(1);
    for (std::uint64_t id = 2; id <= 3 * EmulatedCursor::kCapacity; ++id) cursors.remember(picture(id));
    check(cursors.find(1) == worn && worn != nullptr, "the cursor worn is kept however many arrive after it");
}

// The guest keeps 64 it believes sent; as long as the host keeps more, in
// the same order, a cursor named is one the host has.
void the_bound_is_above_the_guests() { check(EmulatedCursor::kCapacity > 64, "the host keeps more than the guest"); }

}  // namespace

int main() {
    the_cursor_shown_longest_ago_goes_first();
    the_cursor_worn_stays();
    the_bound_is_above_the_guests();
    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("emulated_cursor: all checks passed\n");
    return EXIT_SUCCESS;
}
