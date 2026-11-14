#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rwin::host {
class IconBadge {
public:
    IconBadge() = default;
    explicit IconBadge(const std::string& path);

    void stamp(std::span<std::uint8_t> premultiplied_bgra, int side, std::size_t stride) const;

    [[nodiscard]] static int side_on(int icon_side) noexcept;
    [[nodiscard]] static int inset_on(int icon_side) noexcept;

    static constexpr int kSmallestSide = 6;
    static constexpr std::uint32_t kLargestPicture = 1024;

private:
    [[nodiscard]] std::vector<std::uint8_t> scaled(int side) const;

    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::vector<std::uint8_t> picture_;
};

}
