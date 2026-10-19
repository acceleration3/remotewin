#pragma once

#include <string>

struct IUnknown;

namespace rwin::guest {

// The taskbar's own answer to which application a window is
// (IApplicationResolver::GetAppIDForWindow, what RAIL's server answers Get
// App ID with, docs/rail-model.md §2.9): an explicit AppUserModelID on the
// window or its process, or the one Windows derives from the executable
// or the shortcut that started it.
//
// The resolver is an undocumented shell interface; its id and the slot of
// GetAppIDForWindow are rdpshell's on Windows 11 24H2. Made on, and used
// from, one single-threaded apartment thread.
class AppIdResolver {
public:
    AppIdResolver();
    ~AppIdResolver();

    AppIdResolver(const AppIdResolver&) = delete;
    AppIdResolver& operator=(const AppIdResolver&) = delete;
    AppIdResolver(AppIdResolver&&) = delete;
    AppIdResolver& operator=(AppIdResolver&&) = delete;

    // UTF-8; empty when the shell has no answer for it.
    [[nodiscard]] std::string app_id_of(void* window) const;

private:
    IUnknown* resolver_ = nullptr;
};

}  // namespace rwin::guest
