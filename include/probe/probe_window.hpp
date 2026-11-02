#pragma once

#include "probe/test_pattern.hpp"
#include "proto/probe/window_spec.hpp"

#include <cstdint>
#include <optional>
#include <string>

struct HWND__;
struct HICON__;

namespace rwin::probe {

class InputJournal;

class ProbeWindow {
public:
    ProbeWindow(const WindowSpec& spec, InputJournal& journal);
    ~ProbeWindow();

    ProbeWindow(const ProbeWindow&) = delete;
    ProbeWindow& operator=(const ProbeWindow&) = delete;
    ProbeWindow(ProbeWindow&&) = delete;
    ProbeWindow& operator=(ProbeWindow&&) = delete;

    [[nodiscard]] bool made() const noexcept { return window_ != nullptr; }
    [[nodiscard]] const std::string& failure() const noexcept { return failure_; }
    [[nodiscard]] std::uint64_t handle() const noexcept;
    [[nodiscard]] bool destroyed() const noexcept { return made_ && window_ == nullptr; }

    void answer_cursor_with(HICON__* cursor) noexcept { answer_cursor_ = cursor; }
    void recentre(bool on) noexcept { recentre_ = on; }
    [[nodiscard]] std::uint32_t put_back() const noexcept { return put_back_; }

private:
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

}
