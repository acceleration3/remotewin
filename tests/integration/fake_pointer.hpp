#pragma once

#include <cstdint>
#include <string>

struct org_kde_kwin_fake_input;
struct wl_display;
struct wl_registry;

namespace rwin::integration {

// The pointer of a compositor with no input devices of its own - KWin's
// virtual backend - made through KWin's fake input (org_kde_kwin_fake_input):
// while this is connected the seat has a pointer, and it goes where it is
// told. Each call waits for the compositor to have taken it.
class FakePointer {
public:
    explicit FakePointer(const std::string& socket);
    ~FakePointer();

    FakePointer(const FakePointer&) = delete;
    FakePointer& operator=(const FakePointer&) = delete;
    FakePointer(FakePointer&&) = delete;
    FakePointer& operator=(FakePointer&&) = delete;

    [[nodiscard]] bool usable() const noexcept { return input_ != nullptr; }

    // In the compositor's logical units.
    void move_to(double x, double y);
    void press();
    void release();

private:
    static void announced(void* self, wl_registry* registry, std::uint32_t name, const char* interface,
                          std::uint32_t version);
    static void withdrawn(void* self, wl_registry* registry, std::uint32_t name);
    void settle();

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    org_kde_kwin_fake_input* input_ = nullptr;
};

}  // namespace rwin::integration
