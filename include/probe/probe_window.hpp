#pragma once

#include "probe/test_pattern.hpp"
#include "proto/probe/window_spec.hpp"

#include <cstdint>
#include <optional>
#include <string>

// windows.h's HWND and HCURSOR, which are pointers to these, named without
// including it.
struct HWND__;
struct HICON__;

namespace rwin::probe {

class InputJournal;

// One window made to a spec, painting the test pattern and noting every
// input message it is given.
//
// Made, layered, shaped and given its DWM attributes before it is shown,
// as an application that wants those from its first frame does: a layered
// window shown before its attributes are set is not drawn at all, and DWM
// reads some attributes only when a window first appears.
class ProbeWindow {
public:
    ProbeWindow(const WindowSpec& spec, InputJournal& journal);
    ~ProbeWindow();

    ProbeWindow(const ProbeWindow&) = delete;
    ProbeWindow& operator=(const ProbeWindow&) = delete;
    ProbeWindow(ProbeWindow&&) = delete;
    ProbeWindow& operator=(ProbeWindow&&) = delete;

    // False when Windows refused to make it; ``failure`` says why.
    [[nodiscard]] bool made() const noexcept { return window_ != nullptr; }
    [[nodiscard]] const std::string& failure() const noexcept { return failure_; }
    [[nodiscard]] std::uint64_t handle() const noexcept;
    // Gone by Windows' hand rather than ours: closed, or taken down with
    // its owner.
    [[nodiscard]] bool destroyed() const noexcept { return made_ && window_ == nullptr; }

    // Answer WM_SETCURSOR over the client area with this cursor - null is
    // SetCursor(NULL), no cursor at all - rather than the class's.
    void answer_cursor_with(HICON__* cursor) noexcept { answer_cursor_ = cursor; }
    // Put the cursor back at the middle of the client area whenever it
    // moves: mouse look, as a game without raw input does it.
    void recentre(bool on) noexcept { recentre_ = on; }
    // Moves in a row it put back, since it last left one where it went.
    [[nodiscard]] std::uint32_t put_back() const noexcept { return put_back_; }

private:
    // A WNDPROC, spelled in the types windows.h gives it on x64.
    static std::intptr_t __stdcall procedure(HWND__* window, unsigned message,
                                             std::uintptr_t wparam, std::intptr_t lparam);
    std::intptr_t answer(unsigned message, std::uintptr_t wparam, std::intptr_t lparam);
    void apply_layering();
    void apply_shape();
    void apply_dwm_attributes();
    void show();
    void paint();
    void follow_cursor(std::intptr_t lparam);
    void note_raw_input(std::intptr_t lparam);

    WindowSpec spec_;
    InputJournal& journal_;
    TestPattern pattern_;
    HWND__* window_ = nullptr;
    std::optional<HICON__*> answer_cursor_;
    bool recentre_ = false;
    std::uint32_t put_back_ = 0;
    bool made_ = false;
    std::string failure_;
};

}  // namespace rwin::probe
