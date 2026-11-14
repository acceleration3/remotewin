#pragma once

#include "proto/window_fields.hpp"

#include <string>

namespace rwin::host {

class GuestApplication {
public:
    explicit GuestApplication(const WindowFields::AppId& app);

    [[nodiscard]] static std::string folded(std::string text);

    [[nodiscard]] bool known() const noexcept { return !identity_.empty(); }
    [[nodiscard]] const std::string& identity() const noexcept { return identity_; }
    [[nodiscard]] std::string host_app_id() const;

private:
    [[nodiscard]] std::string readable() const;

    std::string identity_;
};

}
