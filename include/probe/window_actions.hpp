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

// Does to the probe's windows what an application does to its own while
// they live: the Win32 call a ProbeAction names, as that application
// would make it.
//
// A modal dialog and a message box do not return until they end: the
// caller has to be told about the window they made from inside the modal
// loop they run (ProbeWindows::new_dialog), not from here.
class WindowActions {
public:
    WindowActions(ProbeWindows& windows, DialogBoxes& dialogs, PointerActions& pointer);

    // The window it made, 0 when it made none; nothing when Windows
    // refused, with ``failure`` saying why.
    [[nodiscard]] std::optional<std::uint64_t> perform(const ProbeAction& action,
                                                       std::string& failure);

private:
    [[nodiscard]] bool set_position(const ProbeAction& action, std::string& failure) const;
    [[nodiscard]] bool show_message_box(const ProbeAction& action, std::string& failure) const;

    ProbeWindows& windows_;
    DialogBoxes& dialogs_;
    PointerActions& pointer_;
};

}  // namespace rwin::probe
