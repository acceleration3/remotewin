#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rwin::host {

class GuestWindow;
class GuestWindows;

// Which of the guest's windows are the application this process mirrors.
//
// The guest says of every window in its session which process made it,
// from which image, and which application the taskbar groups it under
// (its AppUserModelID); it does not choose among them. This does, from
// what the application was launched as: its process, its image, and the
// AppUserModelIDs it is known by. A window belongs when it was made by
// that process or that image, or the taskbar groups it with one that was
// - a Store application's windows, made by a host process of Windows', go
// by their AUMID - or when it is owned by or placed against a window that
// belongs: a dialog of a helper process, a menu.
//
// With nothing to go by, every window belongs: the whole session is
// mirrored.
class MirrorScope {
public:
    struct Launch {
        std::optional<std::uint32_t> process_id;
        // The image's Win32 path in the guest; empty for none.
        std::string image;
        std::vector<std::string> aumids;
        // Whether a window the taskbar groups with the launched process's is
        // the application's too. Not for the conformance harness: its probe
        // starts a second process of its own image, the backdrop, which is
        // not to be mirrored.
        bool same_app_id = true;
    };

    explicit MirrorScope(Launch launch);

    [[nodiscard]] bool everything() const noexcept;

    // Takes on the AppUserModelID of every window made by the launched
    // process or image: the identity the rest of the application's windows
    // share. The ones not had before.
    [[nodiscard]] std::vector<std::string> claim(const GuestWindows& windows);
    [[nodiscard]] bool belongs(WindowId window, const GuestWindows& windows) const;
    [[nodiscard]] const std::vector<std::string>& claimed() const noexcept { return claimed_; }

private:
    [[nodiscard]] bool launched(const GuestWindow& window) const;
    [[nodiscard]] bool belongs_itself(const GuestWindow& window) const;

    Launch launch_;
    // The launched image, as compared_path keeps it.
    std::string image_;
    std::vector<std::string> claimed_;
};

}  // namespace rwin::host
