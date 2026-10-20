#pragma once

#include <sys/types.h>

#include <optional>
#include <string>
#include <string_view>

namespace rwin::host {
class BusConnection;
}

namespace rwin::integration {

// The host's own account of its mirror (WindowReport's Describe on the
// session bus), read for the one thing the compositor cannot say: the size
// of the guest picture the mirror shows.
class HostReport {
public:
    struct Picture {
        int width = 0;
        int height = 0;
    };

    HostReport(host::BusConnection& bus, pid_t host);

    // One mirror as the host has it.
    struct Mirror {
        // "toplevel", "popup" or "subsurface".
        std::string role;
        bool shown = false;
        bool mapped = false;
        // Its last picture came on the GPU, not as pixels.
        bool on_gpu = false;
        Picture picture;
    };

    // The first mirror's picture, in guest pixels; nothing before there is
    // one, or while the host does not answer.
    [[nodiscard]] std::optional<Picture> picture();
    // The mirror of the guest window of that title; nothing while there is
    // none.
    [[nodiscard]] std::optional<Mirror> mirror(std::string_view title);

private:
    [[nodiscard]] std::string find_name();
    [[nodiscard]] std::optional<std::string> describe();

    host::BusConnection& bus_;
    pid_t host_;
    std::string name_;
};

}  // namespace rwin::integration
