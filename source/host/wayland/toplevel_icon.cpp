#include "host/wayland/toplevel_icon.hpp"

#include "host/wayland/globals.hpp"
#include "host/wayland/toplevel_role.hpp"
#include "proto/window_icon.hpp"

#include <xdg-toplevel-icon-v1-client-protocol.h>

#include <algorithm>

namespace rwin::host {
ToplevelIcon::ToplevelIcon(const Globals& globals, const WindowIcon& icon) {
    if (globals.toplevel_icon() == nullptr) return;
    const int side = std::max(icon.width, icon.height);
    pixels_ = std::make_unique<ShmBuffer>(globals.shm(), Size{side, side});
    const std::span<std::uint8_t> into = pixels_->pixels();
    std::fill(into.begin(), into.end(), std::uint8_t{0});
    const int left = (side - icon.width) / 2;
    const int top = (side - icon.height) / 2;
    for (int row = 0; row < icon.height; ++row) {
        const std::uint8_t* from = icon.bgra.data() + static_cast<std::size_t>(row) * icon.width * 4;
        std::uint8_t* to = into.data() + static_cast<std::size_t>(row + top) * pixels_->stride() + left * 4;
        for (int column = 0; column < icon.width; ++column, from += 4, to += 4) {
            const unsigned alpha = from[3];
            for (int channel = 0; channel < 3; ++channel) {
                to[channel] = static_cast<std::uint8_t>((from[channel] * alpha + 127) / 255);
            }
            to[3] = static_cast<std::uint8_t>(alpha);
        }
    }
    icon_ = xdg_toplevel_icon_manager_v1_create_icon(globals.toplevel_icon());
    xdg_toplevel_icon_v1_add_buffer(icon_, pixels_->buffer(), 1);
}

ToplevelIcon::~ToplevelIcon() {
    if (icon_ != nullptr) xdg_toplevel_icon_v1_destroy(icon_);
}

void ToplevelIcon::put_on(ToplevelRole& toplevel) const {
    if (icon_ != nullptr) toplevel.set_icon(icon_);
}

}
