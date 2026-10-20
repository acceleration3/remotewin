#pragma once

#include <sys/types.h>

#include <optional>
#include <string>

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

    // The first mirror's picture, in guest pixels; nothing before there is
    // one, or while the host does not answer.
    [[nodiscard]] std::optional<Picture> picture();

private:
    [[nodiscard]] std::string find_name();

    host::BusConnection& bus_;
    pid_t host_;
    std::string name_;
};

}  // namespace rwin::integration
