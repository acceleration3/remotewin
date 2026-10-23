#pragma once

#include "proto/window_fields.hpp"
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
// by their AUMID - or when it was made by a process of the launched
// program's process tree, or when it is owned by or placed against a window
// that belongs: a dialog of a helper process, a menu.
//
// The process tree is the launched process's whole family: the topmost
// ancestor that is no part of the session itself (explorer, a service host,
// a broker, LSW's agent) and every process it started, however long ago and
// whether or not its parent is still there. The guest decides who is in it
// (ProcessTree) and says with each window the pids from its process up to
// its tree's root (WindowFields::AppId::tree); a window belongs when the
// launched process is among them, or its tree is the launched process's:
// the root of which the first window of the launched process, or of a
// process it started, names.
//
// With nothing to go by, every window belongs: the whole session is
// mirrored.
class MirrorScope {
public:
    struct Launch {
        std::optional<std::uint32_t> process_id;
        // When that process was created (FILETIME ticks), as the guest's
        // agent read it at the launch: a pid alone may be a later process's.
        std::optional<std::uint64_t> process_created;
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
    //
    // And the root of the launched process's tree, learned the same way,
    // claimed under ``tree_claim`` once: the identity a program that
    // shows no AUMID is known by, so two mirrors of one tree are told apart
    // as two of one AUMID are.
    [[nodiscard]] std::vector<std::string> claim(const GuestWindows& windows);
    [[nodiscard]] bool belongs(WindowId window, const GuestWindows& windows) const;
    [[nodiscard]] const std::vector<std::string>& claimed() const noexcept { return claimed_; }

private:
    [[nodiscard]] bool launched(const GuestWindow& window) const;
    [[nodiscard]] bool belongs_itself(const GuestWindow& window) const;
    [[nodiscard]] bool is_launched(const WindowFields::ProcessRef& process) const;
    [[nodiscard]] bool in_launched_tree(const WindowFields::AppId& app) const;

    Launch launch_;
    // The launched image, as compared_path keeps it.
    std::string image_;
    std::vector<std::string> claimed_;
    // The launched process's tree root, once a window has named it.
    std::optional<WindowFields::ProcessRef> tree_root_;
    bool tree_claimed_ = false;
};

}  // namespace rwin::host
