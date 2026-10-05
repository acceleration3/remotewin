#include "guest/capture/frame_signal.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

FrameSignal::FrameSignal() : event_(::CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}

FrameSignal::~FrameSignal() {
    if (event_ != nullptr) ::CloseHandle(event_);
}

void FrameSignal::raise() const noexcept {
    if (event_ != nullptr) ::SetEvent(event_);
}

}  // namespace rwin::guest
