#pragma once

#include <QColor>
#include <QImage>
#include <QRect>

namespace rwin::host {
class MirrorWindow;
struct MirrorState;
}  // namespace rwin::host

namespace rwin::conform {

// The host's picture of a scenario: each mirror as it paints itself, laid
// where the guest has its window, over the backdrop the guest's windows
// are in front of - so a see-through pixel shows the same colour through
// it on both sides.
//
// Mirrors are laid in the order they are added, later ones over earlier
// ones, which is how the guest stacks them: an owner first, then what it
// owns. Where a toplevel mirror sits on the host desktop is the
// compositor's business and is not part of this; that a popup sits where
// it belongs against its owner is checked as a property instead.
class HostScene {
public:
    // ``area`` is the part of the guest's screen this covers, in its pixels.
    HostScene(QRect area, QColor backdrop);

    // ``where`` is the guest rectangle the window's visible frame covers.
    //
    // A picture the mirror draws one to one is laid one to one from that
    // rectangle's corner, whatever its size: one smaller than the frame -
    // a window whose edge row is outside its region is captured without
    // it - leaves the backdrop showing where the guest shows it too. A
    // fitted picture is resampled to the rectangle; returns false then.
    bool add(host::MirrorWindow& mirror, const host::MirrorState& state, QRect where);

    [[nodiscard]] const QImage& image() const noexcept { return image_; }

private:
    QRect area_;
    QImage image_;
};

}  // namespace rwin::conform
