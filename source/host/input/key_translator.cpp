#include "host/input/key_translator.hpp"

#include "host/input/scan_code.hpp"

#include <QKeyEvent>
#include <QString>

namespace rwin::host {

namespace {

// X11 keycodes start at 8, below which the protocol reserves; XKB, and
// the Wayland keymaps made from it, kept the offset.
constexpr std::uint32_t kXkbOffset = 8;

// For a key the platform gave no scan code: the keys a character cannot
// express, by name.
std::uint32_t virtual_key_for(int key) {
    switch (key) {
        case Qt::Key_Backspace: return 0x08;
        case Qt::Key_Tab:       return 0x09;
        case Qt::Key_Return:
        case Qt::Key_Enter:     return 0x0D;
        case Qt::Key_Pause:     return 0x13;
        case Qt::Key_Escape:    return 0x1B;
        case Qt::Key_PageUp:    return 0x21;
        case Qt::Key_PageDown:  return 0x22;
        case Qt::Key_End:       return 0x23;
        case Qt::Key_Home:      return 0x24;
        case Qt::Key_Left:      return 0x25;
        case Qt::Key_Up:        return 0x26;
        case Qt::Key_Right:     return 0x27;
        case Qt::Key_Down:      return 0x28;
        case Qt::Key_Insert:    return 0x2D;
        case Qt::Key_Delete:    return 0x2E;
        default:                return 0;
    }
}

}  // namespace

KeyTranslator::KeyTranslator(QStringView platform_name) {
    if (platform_name == u"wayland" || platform_name.startsWith(u"wayland-")
        || platform_name == u"xcb") {
        evdev_offset_ = kXkbOffset;
    }
}

std::uint32_t KeyTranslator::native_scan_code(std::uint32_t evdev_code) const noexcept {
    return evdev_offset_.has_value() ? evdev_code + *evdev_offset_ : 0;
}

std::optional<KeyStroke> KeyTranslator::stroke_for(const QKeyEvent& event, std::uint64_t window,
                                                   bool pressed) const {
    KeyStroke stroke;
    stroke.window = window;
    stroke.pressed = pressed;
    const std::uint32_t native = event.nativeScanCode();
    if (evdev_offset_.has_value() && native > *evdev_offset_) {
        stroke.scan_code = windows_scan_code(native - *evdev_offset_);
        if (stroke.scan_code != 0) return stroke;
    }
    stroke.virtual_key = virtual_key_for(event.key());
    if (stroke.virtual_key != 0) return stroke;
    const QString text = event.text();
    if (text.isEmpty() || text.at(0).unicode() < 0x20) return std::nullopt;
    stroke.character = text.toUcs4().value(0);
    return stroke;
}

}  // namespace rwin::host
