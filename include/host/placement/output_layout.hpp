#pragma once

#include "host/geometry/box_f.hpp"
#include "host/geometry/point.hpp"
#include "host/geometry/point_f.hpp"
#include "host/placement/screen_mapping.hpp"

#include <optional>
#include <string>
#include <vector>

namespace rwin::host {
class OutputLayout {
public:
    struct Output {
        std::string name;
        PointF origin;
        PointF size;
        double scale = 1.0;
    };

    void replace(std::vector<Output> outputs);
    void prefer(std::string name);

    [[nodiscard]] bool known() const noexcept { return !outputs_.empty(); }
    [[nodiscard]] const Output& guest_output() const;
    [[nodiscard]] const std::string& guest_output_name() const { return guest_output().name; }

    [[nodiscard]] Point guest_point(const BoxF& frame) const;
    [[nodiscard]] PointF host_point(Point guest, const std::optional<BoxF>& on) const;

private:
    [[nodiscard]] const Output& output_of(const BoxF& frame) const;
    [[nodiscard]] static ScreenMapping mapping_of(const Output& output) {
        return ScreenMapping{output.origin, output.scale};
    }

    std::vector<Output> outputs_;
    std::string preferred_;
};

}
