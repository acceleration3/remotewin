#pragma once

#include "proto/window_id.hpp"

#include <string>

namespace rwin::host {

class GuestWindow;
class GuestWindows;
class MirrorScope;

class HostAppIds {
public:
    HostAppIds(std::string launched, const MirrorScope& scope);

    [[nodiscard]] const std::string& launched() const noexcept { return launched_; }
    [[nodiscard]] std::string of(WindowId window, const GuestWindows& windows) const;

private:
    [[nodiscard]] std::string of_application(const GuestWindow& window) const;

    std::string launched_;
    const MirrorScope& scope_;
};

}
