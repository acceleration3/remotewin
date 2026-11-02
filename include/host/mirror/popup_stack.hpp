#pragma once

#include <vector>

namespace rwin::host {

class WindowMirror;

class PopupStack {
public:
    void opened(WindowMirror& popup);
    void closing(WindowMirror& popup);
    void closing_under(const WindowMirror& mirror);
    [[nodiscard]] bool open_beside(const WindowMirror& parent) const;

private:
    void close_from(std::size_t first);

    std::vector<WindowMirror*> open_;
};

}
