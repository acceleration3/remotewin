#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace rwin {

class MessageReader;
class MessageWriter;

// A window the conformance probe is asked to make: the Win32 words it is
// created with, how it is layered and shaped, how it is first shown, and
// the DWM attributes set on it before it is.
//
// The words go to CreateWindowEx as given. Windows normalises some of them
// - an overlapped window gains WS_CLIPSIBLINGS, a captioned one WS_BORDER -
// and what it made of them comes back as the window's truth, never assumed
// from this.
struct WindowSpec {
    enum class Layering : std::uint8_t { None, Alpha, ColorKey, AlphaAndColorKey };
    // Frame is a rectangle with a rectangular hole: a region of more than
    // one piece, where everything else is one.
    enum class Shape : std::uint8_t { None, RoundedRect, Ellipse, Frame };
    enum class ShowState : std::uint8_t { Normal, NoActivate, Maximized, Minimized, Hidden };

    // A DWM attribute left as Windows has it.
    static constexpr std::int32_t kUnset = -1;

    std::uint32_t class_style = 0;
    std::uint32_t style = 0;
    std::uint32_t extended_style = 0;
    Layering layering = Layering::None;
    std::uint8_t alpha = 255;
    // A COLORREF, 0x00BBGGRR. The probe paints a patch of it, so there is
    // something for the key to remove.
    std::uint32_t color_key = 0;
    Shape shape = Shape::None;
    // The probe window that owns this one; 0 for none.
    std::uint64_t owner = 0;
    ShowState show = ShowState::Normal;
    // Physical pixels: where the window rectangle goes, and the client
    // area it is grown around.
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t client_width = 0;
    std::uint32_t client_height = 0;
    std::string title;
    // DWMWA_WINDOW_CORNER_PREFERENCE, DWMWA_USE_IMMERSIVE_DARK_MODE and
    // DWMWA_NCRENDERING_POLICY, as the values DWM takes for them.
    std::int32_t corner_preference = kUnset;
    std::int32_t dark_mode = kUnset;
    std::int32_t nc_rendering_policy = kUnset;

    [[nodiscard]] bool alpha_blended() const noexcept {
        return layering == Layering::Alpha || layering == Layering::AlphaAndColorKey;
    }
    [[nodiscard]] bool color_keyed() const noexcept {
        return layering == Layering::ColorKey || layering == Layering::AlphaAndColorKey;
    }

    [[nodiscard]] bool operator==(const WindowSpec&) const = default;

    void write(MessageWriter& out) const;
    // Nothing when the message is short or names a value no enum has.
    [[nodiscard]] static std::optional<WindowSpec> read(MessageReader& in);
};

}  // namespace rwin
