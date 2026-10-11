#pragma once

#include <cstdint>
#include <unordered_map>

namespace rwin::guest {

// The process whose application a window shows.
//
// Its own process, except for a Store application's frame: Windows puts
// the application's CoreWindow inside an ApplicationFrameWindow that
// ApplicationFrameHost.exe owns - one process serving every such
// application - so the application is found through the child.
//
// Minimised, a Store application is suspended and its CoreWindow taken
// out of the frame, which is then nobody's but its host's. The frame is
// still the application's window, minimised: it is remembered as the
// application's for as long as it exists, or the mirror of a minimised
// window would close.
class ApplicationProcess {
public:
    [[nodiscard]] std::uint32_t of(std::uint64_t window_handle);

    // Drops the frames that no longer exist, so a handle Windows hands
    // out again is not taken for the frame that had it.
    void forget_closed();

private:
    // Each Store frame's application, as last seen inside it.
    std::unordered_map<std::uint64_t, std::uint32_t> frames_;
};

}  // namespace rwin::guest
