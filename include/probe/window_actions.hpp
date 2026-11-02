#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace rwin {
struct ProbeAction;
}

namespace rwin::probe {

class DialogBoxes;
class PointerActions;
class ProbeWindows;

class WindowActions {
public:
    WindowActions(ProbeWindows& windows, DialogBoxes& dialogs, PointerActions& pointer);

    [[nodiscard]] std::optional<std::uint64_t> perform(const ProbeAction& action,
                                                       std::string& failure);

private:
    [[nodiscard]] bool set_position(const ProbeAction& action, std::string& failure) const;
    [[nodiscard]] bool show_message_box(const ProbeAction& action, std::string& failure) const;

    ProbeWindows& windows_;
    DialogBoxes& dialogs_;
    PointerActions& pointer_;
};

}
