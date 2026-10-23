#pragma once

#include "host/geometry/box_f.hpp"
#include "host/geometry/point.hpp"
#include "host/geometry/point_f.hpp"
#include "host/placement/screen_mapping.hpp"

#include <optional>
#include <string>
#include <vector>

namespace rwin::host {

// The desktop's outputs as the compositor lays them out, and the guest's one
// monitor on them.
//
// The guest has one monitor, as many pixels as the output that stands for it
// (the manager names it: ``prefer``), and every mirror, on whichever output
// it is, is a window on that monitor. A host position is mapped by the
// output the window is on - its centre decides - so the same spot of any
// output is the same spot of the guest monitor, in device pixels; and the
// guest window is kept fully on the monitor, where Windows neither pushes it
// back nor stops drawing it. A guest position is placed on the output the
// window is on now, and a window moving between outputs is no guest move:
// both directions read the one output the window's centre is on, which only
// the user's drag changes.
class OutputLayout {
public:
    struct Output {
        std::string name;
        // The corner in the desktop's logical layout, and the size in
        // logical units.
        PointF origin;
        PointF size;
        double scale = 1.0;
    };

    // Replaces what the compositor said.
    void replace(std::vector<Output> outputs);
    // The output that stands for the guest's monitor, by name; the first one
    // where there is none of that name (or none was named).
    void prefer(std::string name);

    // False before the compositor has said.
    [[nodiscard]] bool known() const noexcept { return !outputs_.empty(); }
    [[nodiscard]] const Output& guest_output() const;
    [[nodiscard]] const std::string& guest_output_name() const { return guest_output().name; }

    // Where the picture of a window whose frame is ``frame`` is on the
    // guest's monitor, kept whole on it.
    [[nodiscard]] Point guest_point(const BoxF& frame) const;
    // Where the frame's corner is to be for the picture to be at ``guest``,
    // on the output ``on`` is on - the guest's, for a window not seen yet.
    [[nodiscard]] PointF host_point(Point guest, const std::optional<BoxF>& on) const;

private:
    // Where the frame's centre is; the guest's output where on none.
    [[nodiscard]] const Output& output_of(const BoxF& frame) const;
    [[nodiscard]] static ScreenMapping mapping_of(const Output& output) {
        return ScreenMapping{output.origin, output.scale};
    }

    std::vector<Output> outputs_;
    std::string preferred_;
};

}  // namespace rwin::host
