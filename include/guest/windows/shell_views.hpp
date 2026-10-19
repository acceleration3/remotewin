#pragma once

struct IUnknown;

namespace rwin::guest {

// What the shell's own window manager says of a window it manages - an
// immersive window, Start, search, the notification centre - which
// Windows' taskbar asks before it gives one a button (rdpshell's
// CHookWindow::UpdateTaskbarIconAllowed, docs/rail-model.md §2.3).
//
// A window is shell-managed when user32's IsShellManagedWindow (exported
// by ordinal only) says so and it lacks the
// Microsoft.Windows.ShellManagedWindowAsNormalWindow property. Such a
// window is in the switchers only if its application view says so. The
// view collection is reached as rdpshell reaches it, through
// CLSID_ImmersiveShell, with the vtable slots it calls on this build.
// Made on, and used from, one single-threaded apartment thread.
class ShellViews {
public:
    ShellViews();
    ~ShellViews();

    ShellViews(const ShellViews&) = delete;
    ShellViews& operator=(const ShellViews&) = delete;
    ShellViews(ShellViews&&) = delete;
    ShellViews& operator=(ShellViews&&) = delete;

    [[nodiscard]] bool shell_managed(void* window) const;
    // For a shell-managed window: whether its view is shown in Alt+Tab and
    // the taskbar. False when it has no view.
    [[nodiscard]] bool shown_in_switchers(void* window) const;

private:
    using IsShellManagedWindow = int(__stdcall*)(void*);

    IsShellManagedWindow is_shell_managed_ = nullptr;
    // IApplicationViewCollection, asked for lazily: most windows are never
    // shell-managed, and Explorer may restart under us.
    mutable IUnknown* views_ = nullptr;
};

}  // namespace rwin::guest
