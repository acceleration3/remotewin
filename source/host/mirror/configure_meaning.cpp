#include "host/mirror/configure_meaning.hpp"

#include <algorithm>

namespace rwin::host {

namespace {

ConfigureMeaning::Kind kind_of(const ToplevelConfigure& configure) noexcept {
    if (configure.fullscreen) return ConfigureMeaning::Kind::Fullscreen;
    if (!configure.maximized) return ConfigureMeaning::Kind::Free;
    if (!configure.tiled) return ConfigureMeaning::Kind::Maximized;
    const bool fills_bounds = !configure.bounds.empty() && configure.size.width >= configure.bounds.width
                              && configure.size.height >= configure.bounds.height;
    return fills_bounds ? ConfigureMeaning::Kind::Maximized : ConfigureMeaning::Kind::Placed;
}

int within(int length, int smallest, int largest) noexcept {
    const int raised = std::max(length, smallest);
    return largest > 0 ? std::min(raised, std::max(largest, smallest)) : raised;
}

}

ConfigureMeaning ConfigureMeaning::of(const ToplevelConfigure& configure, const Guest& guest) noexcept {
    ConfigureMeaning meaning;
    meaning.kind = kind_of(configure);
    if (meaning.held() || configure.size.empty() || !guest.sizable) return meaning;
    if (meaning.kind == Kind::Placed && configure.resizing) return meaning;
    const Size taken{within(configure.size.width, guest.smallest.width, guest.largest.width),
                     within(configure.size.height, guest.smallest.height, guest.largest.height)};
    if (taken != guest.size) meaning.guest_size = taken;
    return meaning;
}

}
