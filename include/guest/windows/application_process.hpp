#pragma once

#include <cstdint>

namespace rwin::guest {

// The process whose application a window shows.
//
// Its own process, except for a Store application's frame: Windows puts
// the application's CoreWindow inside an ApplicationFrameWindow that
// ApplicationFrameHost.exe owns - one process serving every such
// application - so the application is found through the child.
[[nodiscard]] std::uint32_t application_process_of(std::uint64_t window_handle);

}  // namespace rwin::guest
