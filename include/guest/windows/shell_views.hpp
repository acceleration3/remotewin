#pragma once

struct IUnknown;

namespace rwin::guest {
class ShellViews {
public:
    ShellViews();
    ~ShellViews();

    ShellViews(const ShellViews&) = delete;
    ShellViews& operator=(const ShellViews&) = delete;
    ShellViews(ShellViews&&) = delete;
    ShellViews& operator=(ShellViews&&) = delete;

    [[nodiscard]] bool shell_managed(void* window) const;
    [[nodiscard]] bool shown_in_switchers(void* window) const;

private:
    using IsShellManagedWindow = int(__stdcall*)(void*);

    IsShellManagedWindow is_shell_managed_ = nullptr;
    mutable IUnknown* views_ = nullptr;
};

}
