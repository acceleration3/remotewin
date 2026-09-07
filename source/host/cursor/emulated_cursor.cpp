#include "host/cursor/emulated_cursor.hpp"

#include "logging/logger.hpp"

#include <QImage>
#include <QPixmap>
#include <QWidget>

namespace rwin::host {

void EmulatedCursor::remember(std::uint64_t id, std::uint32_t width, std::uint32_t height,
                              std::uint32_t hotspot_x, std::uint32_t hotspot_y,
                              const std::uint8_t* pixels) {
    if (width == 0 || height == 0 || pixels == nullptr) return;
    const QImage image(pixels, static_cast<int>(width), static_cast<int>(height),
                       static_cast<int>(width * 4), QImage::Format_ARGB32);
    // Copied: the bytes belong to the decoder and the next read reuses them.
    shapes_.insert(id, QCursor(QPixmap::fromImage(image.copy()),
                               static_cast<int>(hotspot_x),
                               static_cast<int>(hotspot_y)));
    RWIN_DEBUG("cursor", "remembered shape {:#x} ({}x{})", id, width, height);
}

bool EmulatedCursor::apply(std::uint64_t id, QWidget& to) const {
    const auto found = shapes_.constFind(id);
    if (found == shapes_.constEnd()) return false;
    to.setCursor(found.value());
    return true;
}

}  // namespace rwin::host
