#pragma once

#include <QCursor>
#include <QHash>

#include <cstdint>

namespace rwin::host {

// The guest's cursor, wearing this desktop's clothes.
//
// The guest sends a shape once and then names it; this keeps the shapes
// so a pointer moving between a text field and a button does not resend
// two bitmaps thirty times a second.
class EmulatedCursor {
public:
    // Straight BGRA, top-down, ``width * 4`` per row.
    void remember(std::uint64_t id, std::uint32_t width, std::uint32_t height,
                  std::uint32_t hotspot_x, std::uint32_t hotspot_y,
                  const std::uint8_t* pixels);

    // Null when the shape has not arrived yet, which happens only if the
    // guest names one before sending it.
    [[nodiscard]] const QCursor* find(std::uint64_t id) const;

private:
    QHash<quint64, QCursor> shapes_;
};

}  // namespace rwin::host
