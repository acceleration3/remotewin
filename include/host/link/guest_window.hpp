#pragma once

#include "host/geometry/box.hpp"
#include "host/geometry/point.hpp"
#include "host/geometry/size.hpp"
#include "proto/min_max.hpp"
#include "proto/window_fields.hpp"
#include "proto/window_id.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rwin::host {
class GuestWindow {
public:
    struct Margins {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;

        [[nodiscard]] bool operator==(const Margins&) const noexcept = default;
    };

    GuestWindow(WindowId id, WindowFields fields) : id_(id), fields_(std::move(fields)) {}

    [[nodiscard]] WindowId id() const noexcept { return id_; }
    [[nodiscard]] const WindowFields& fields() const noexcept { return fields_; }
    void take(WindowFields fields) { fields_ = std::move(fields); }

    [[nodiscard]] WindowId owner() const noexcept;
    [[nodiscard]] WindowId anchor() const noexcept { return fields_.popup.anchor; }

    [[nodiscard]] bool hidden() const noexcept { return fields_.show == WindowFields::Show::Hidden; }
    [[nodiscard]] bool minimized() const noexcept { return fields_.show == WindowFields::Show::Minimized; }
    [[nodiscard]] bool maximized() const noexcept { return fields_.show == WindowFields::Show::Maximized; }
    [[nodiscard]] bool showable() const noexcept { return !hidden() && !picture_size().empty(); }

    [[nodiscard]] const std::string& title() const noexcept { return fields_.title; }

    [[nodiscard]] Box frame() const noexcept;
    [[nodiscard]] Point picture_origin() const noexcept;
    [[nodiscard]] Point picture_offset() const noexcept;
    [[nodiscard]] Size picture_size() const noexcept;
    [[nodiscard]] Size picture_padding() const noexcept;
    [[nodiscard]] Margins margins() const noexcept;
    [[nodiscard]] std::vector<Box> region() const;
    [[nodiscard]] std::vector<Box> opaque_region() const;

    [[nodiscard]] const std::optional<MinMax>& limits() const noexcept { return limits_; }
    void limit(const MinMax& limits) noexcept { limits_ = limits; }

private:
    WindowId id_;
    WindowFields fields_;
    std::optional<MinMax> limits_;
};

}
