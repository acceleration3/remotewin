// KWinPlacement's caption tag: the window's tag, which the scripts find the
// window by and read back as the window, then one character saying which of
// KWin's lists leave it out - a different one for each, and nothing a
// window's own tag reads as anything else.

#include "host/placement/kwin_placement.hpp"

#include <cstdio>
#include <set>
#include <string>

namespace {

using rwin::host::KWinPlacement;

int failures = 0;

void check(bool ok, const char* label) {
    if (ok) return;
    std::printf("FAIL: %s\n", label);
    ++failures;
}

constexpr std::uint64_t kWindow = 0x000a0c2400000007;

void the_tag_names_the_window() {
    check(KWinPlacement::window_of(KWinPlacement::tag_for(kWindow)) == kWindow, "a tag reads back as its window");
    check(!KWinPlacement::window_of("plain title").has_value(), "a title with no tag names no window");
}

void the_caption_tag_is_the_tag_and_what_it_skips() {
    const std::string tag = KWinPlacement::tag_for(kWindow);
    std::set<std::string> said;
    for (const bool taskbar : {false, true}) {
        for (const bool switcher : {false, true}) {
            const std::string caption = KWinPlacement::caption_tag(kWindow, KWinPlacement::Skips{taskbar, switcher});
            check(caption.starts_with(tag), "the caption tag begins with the window's tag");
            check(caption.size() == tag.size() + 3, "one invisible character more");
            check(KWinPlacement::window_of(caption.substr(0, tag.size())) == kWindow,
                  "the tag within it still reads back as the window");
            said.insert(caption);
        }
    }
    check(said.size() == 4, "each of the four says something else");
}

}  // namespace

int main() {
    the_tag_names_the_window();
    the_caption_tag_is_the_tag_and_what_it_skips();
    if (failures == 0) std::printf("kwin_tag: all passed\n");
    return failures == 0 ? 0 : 1;
}
