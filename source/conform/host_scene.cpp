#include "conform/host_scene.hpp"

#include "host/mirror/mirror_state.hpp"
#include "host/mirror/mirror_window.hpp"

#include <QPainter>
#include <QPixmap>

namespace rwin::conform {

HostScene::HostScene(QRect area, QColor backdrop)
    : area_(area), image_(area.size(), QImage::Format_ARGB32_Premultiplied) {
    image_.fill(backdrop);
}

// What the mirror hands the compositor - its own pixels, and the picture
// drawn from the GPU over them - before the compositor puts it on anything.
bool HostScene::add(host::MirrorWindow& mirror, const host::MirrorState& state, QRect where) {
    QImage painted = mirror.grab_shown();
    // Device pixels from here on: at its own ratio the painter below would
    // scale it back to logical units.
    painted.setDevicePixelRatio(1.0);
    QImage picture = painted.copy(state.picture);
    const bool one_to_one = state.fit == 1.0;
    if (!one_to_one) {
        picture = picture.scaled(where.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    QPainter painter{&image_};
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.drawImage(where.topLeft() - area_.topLeft(), picture);
    return one_to_one;
}

}  // namespace rwin::conform
