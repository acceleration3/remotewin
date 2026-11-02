#pragma once

#include <sys/types.h>

#include <optional>
#include <string>
#include <string_view>

namespace rwin::host {
class BusConnection;
}

namespace rwin::integration {
class HostReport {
public:
    struct Picture {
        int width = 0;
        int height = 0;
    };

    HostReport(host::BusConnection& bus, pid_t host);

    struct Mirror {
        std::string role;
        bool shown = false;
        bool mapped = false;
        bool on_gpu = false;
        Picture picture;
    };

    [[nodiscard]] std::optional<Picture> picture();
    [[nodiscard]] std::optional<Mirror> mirror(std::string_view title);

private:
    [[nodiscard]] std::string find_name();
    [[nodiscard]] std::optional<std::string> describe();

    host::BusConnection& bus_;
    pid_t host_;
    std::string name_;
};

}
