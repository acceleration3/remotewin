#pragma once

#include <vector>

namespace rwin::host {

class WindowMirror;

// The popups shown, in the order they were made.
//
// The compositor stacks each new popup over every earlier one and lets a
// client take down only the topmost (xdg_popup.destroy,
// not_the_topmost_popup) - but the guest takes its windows down in any
// order. So a popup going takes every later one down first; those still
// shown in the guest are made again afterwards, over it.
class PopupStack {
public:
    void opened(WindowMirror& popup);
    // ``popup`` is about to go: every popup made after it goes first.
    void closing(WindowMirror& popup);
    // Every popup ``mirror``'s own surface is a parent of, however far
    // down, goes - and everything made after the first of them.
    void closing_under(const WindowMirror& mirror);

private:
    void close_from(std::size_t first);

    std::vector<WindowMirror*> open_;
};

}  // namespace rwin::host
