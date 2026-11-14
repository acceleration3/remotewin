#include "host/cursor/cursor_look.hpp"
#include "host/cursor/cursor_player.hpp"
#include "host/loop/poll_loop.hpp"
#include "proto/cursor_picture.hpp"
#include "proto/guest_cursor.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

using rwin::CursorPicture;
using rwin::GuestCursor;
using rwin::host::CursorLook;
using rwin::host::CursorPlayer;
using rwin::host::PollLoop;

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

constexpr std::uint64_t kCursor = 0x10003;

CursorPicture picture(std::size_t frames) {
    CursorPicture picture;
    picture.id = kCursor;
    for (std::size_t at = 0; at < frames; ++at) {
        CursorPicture::Frame frame;
        frame.width = 1;
        frame.height = 1;
        frame.delay_ms = 16;
        frame.bgra = {0, 0, 0, 255};
        picture.frames.push_back(frame);
    }
    return picture;
}

struct Wearer {
    PollLoop loop;
    CursorPlayer player{loop};
    const CursorLook* look = nullptr;
    std::size_t step = 0;

    Wearer() {
        player.worn.connect([this](const CursorLook* worn, std::size_t at) {
            look = worn;
            step = at;
        });
    }

    void play_until_step(std::size_t wanted) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        const auto watch = loop.timer([&] {
            if (step == wanted || std::chrono::steady_clock::now() > deadline) loop.quit(0);
        });
        watch->start(std::chrono::milliseconds(1), true);
        static_cast<void>(loop.run());
    }

    [[nodiscard]] bool step_is_a_frame() const { return look == nullptr || step < look->frames.size(); }
};

void a_worn_look_replaced_by_a_shorter_one_starts_over() {
    Wearer wearer;
    wearer.player.remember(picture(3));
    wearer.player.show(GuestCursor{kCursor, true});
    wearer.play_until_step(2);
    check(wearer.step == 2 && wearer.step_is_a_frame(), "the animation reached its last picture");
    wearer.player.remember(picture(1));
    check(wearer.look != nullptr && wearer.look->frames.size() == 1, "the look worn is the one that arrived");
    check(wearer.step_is_a_frame(), "the step worn is a picture of the look worn");
    if (wearer.look != nullptr) check(wearer.look->frames[wearer.step].size.width == 1, "and that picture is there");
    check(wearer.player.step() == 0, "the animation started over");
}

void a_look_not_worn_changes_nothing_worn() {
    Wearer wearer;
    wearer.player.remember(picture(3));
    wearer.player.show(GuestCursor{kCursor, true});
    const CursorLook* before = wearer.look;
    CursorPicture other = picture(1);
    other.id = kCursor + 1;
    wearer.look = nullptr;
    wearer.player.remember(other);
    check(wearer.look == nullptr && before != nullptr, "a picture of another cursor is not worn");
    wearer.player.show(GuestCursor{0, false});
    wearer.look = before;
    wearer.player.remember(picture(1));
    check(wearer.look == before, "nor is a picture of a cursor that is hidden");
}

}

int main() {
    a_worn_look_replaced_by_a_shorter_one_starts_over();
    a_look_not_worn_changes_nothing_worn();
    if (failures == 0) std::printf("cursor_player: all passed\n");
    return failures == 0 ? 0 : 1;
}
