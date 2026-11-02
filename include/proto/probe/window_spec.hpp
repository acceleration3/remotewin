#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace rwin {

class MessageReader;
class MessageWriter;

struct WindowSpec {
    enum class Layering : std::uint8_t { None, Alpha, ColorKey, AlphaAndColorKey };
    enum class Shape : std::uint8_t { None, RoundedRect, Ellipse, Frame };
    enum class ShowState : std::uint8_t { Normal, NoActivate, Maximized, Minimized, Hidden };

    static constexpr std::int32_t kUnset = -1;

    std::uint32_t class_style = 0;
    std::uint32_t style = 0;
    std::uint32_t extended_style = 0;
    Layering layering = Layering::None;
    std::uint8_t alpha = 255;
    std::uint32_t color_key = 0;
    Shape shape = Shape::None;
    std::uint64_t owner = 0;
    ShowState show = ShowState::Normal;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t client_width = 0;
    std::uint32_t client_height = 0;
    std::string title;
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
    [[nodiscard]] static std::optional<WindowSpec> read(MessageReader& in);
};

}
