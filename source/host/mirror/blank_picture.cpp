#include "host/mirror/blank_picture.hpp"

#include "host/link/guest_window.hpp"
#include "host/mirror/mirror_role.hpp"

namespace rwin::host {

std::optional<Size> blank_picture(const GuestWindow& window, const MirrorRole& role, Size natural,
                                  bool pictured) noexcept {
    if (role.kind != MirrorRole::Kind::Toplevel || !window.minimized() || pictured || natural.empty()) {
        return std::nullopt;
    }
    return natural;
}

}
