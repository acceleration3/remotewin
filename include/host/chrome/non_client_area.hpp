#pragma once

#include <Qt>

#include <cstdint>

namespace rwin::host {

// What the guest window said the pointer is over, and what the host does
// about it.
//
// The guest keeps its own title bar — it is in the captured pixels, drawn
// the way that application draws it — but the actions those pixels
// promise belong to this desktop. A press on the guest's close button
// closes the host window; a drag on its caption is a compositor move, so
// the window snaps and tiles like every other. Forwarding the press
// instead would have the guest dragging a window inside a screen nobody
// is looking at.
class NonClientArea {
public:
    NonClientArea() = default;
    explicit NonClientArea(std::int32_t hit_code) noexcept : code_(hit_code) {}

    [[nodiscard]] bool is_client() const noexcept;
    [[nodiscard]] bool is_caption() const noexcept;
    [[nodiscard]] bool is_close() const noexcept;
    [[nodiscard]] bool is_minimize() const noexcept;
    [[nodiscard]] bool is_maximize() const noexcept;

    // The edges a press here should start a resize on, or none.
    [[nodiscard]] Qt::Edges resize_edges() const noexcept;

    // Anything the host acts on itself rather than forwarding.
    [[nodiscard]] bool is_host_action() const noexcept;

    [[nodiscard]] std::int32_t code() const noexcept { return code_; }

private:
    // HTCLIENT. The zone a window reports when there is nothing special
    // about the point, and the one this defaults to so a window that has
    // never answered still takes input.
    std::int32_t code_ = 1;
};

}  // namespace rwin::host
