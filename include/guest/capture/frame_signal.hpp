#pragma once

namespace rwin::guest {

// Raised from the compositor's thread whenever a capture has a new frame,
// and waited on by the session beside what its reader hands it: a frame is
// taken as it is composed, not when a timer next looks. The reader raises
// one of its own the same way. A timer on Windows wakes on
// its 15.6 ms tick, so one asked for 16 ms waits 31, and a 60 Hz picture
// arrived as bursts and gaps averaging 53.
class FrameSignal {
public:
    FrameSignal();
    ~FrameSignal();

    FrameSignal(const FrameSignal&) = delete;
    FrameSignal& operator=(const FrameSignal&) = delete;
    FrameSignal(FrameSignal&&) = delete;
    FrameSignal& operator=(FrameSignal&&) = delete;

    void raise() const noexcept;

    // The event, auto-reset: a wait that returns on it consumes it.
    [[nodiscard]] void* event() const noexcept { return event_; }

private:
    void* event_;
};

}  // namespace rwin::guest
