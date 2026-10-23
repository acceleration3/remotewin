#pragma once

#include <string_view>

namespace rwin::guest {

// Where a launched program's process tree ends upward (ProcessTree): the
// session's own processes, which start programs for everybody and are no
// part of any one program - the shell that a double-click started it from,
// the brokers that activate a Store application, the service host, and
// LSW's own agent and window half. Walking up from a window's process,
// the first of these is where to stop, and is not in the tree.
class SessionBoundary {
public:
    // ``image_name``: the process's executable file name, without its path.
    // Compared without case.
    [[nodiscard]] bool contains(std::wstring_view image_name) const;
};

}  // namespace rwin::guest
