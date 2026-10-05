#pragma once

#include <cstddef>

class QWidget;

namespace rwin::host {

struct CursorLook;
class PixelCursor;

// The guest's cursor as one window of this desktop wears it.
//
// A cursor Windows ships is worn as this desktop's own of the same meaning
// - drawn by name, sharp at any scale and in the user's theme - and any
// other as the guest drew it, one guest pixel to one screen pixel.
class WornCursor {
public:
    explicit WornCursor(PixelCursor& pixels) noexcept : pixels_(pixels) {}

    // ``look`` at ``step`` of its animation; null is no cursor at all.
    void wear(const CursorLook* look, std::size_t step);
    // As ``widget``'s cursor: none, the arrow until the guest has said which
    // it shows, or the guest's.
    void put_on(QWidget& widget);

    // What is worn; null when nothing is, hidden or not yet said.
    [[nodiscard]] const CursorLook* look() const noexcept { return look_; }

private:
    PixelCursor& pixels_;
    const CursorLook* look_ = nullptr;
    std::size_t step_ = 0;
    bool hidden_ = false;
};

}  // namespace rwin::host
