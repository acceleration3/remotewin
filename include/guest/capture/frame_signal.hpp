#pragma once

namespace rwin::guest {
class FrameSignal {
public:
    FrameSignal();
    ~FrameSignal();

    FrameSignal(const FrameSignal&) = delete;
    FrameSignal& operator=(const FrameSignal&) = delete;
    FrameSignal(FrameSignal&&) = delete;
    FrameSignal& operator=(FrameSignal&&) = delete;

    void raise() const noexcept;

    [[nodiscard]] void* event() const noexcept { return event_; }

private:
    void* event_;
};

}
