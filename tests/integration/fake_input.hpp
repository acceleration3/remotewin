#pragma once

#include <cstdint>
#include <string>

struct org_kde_kwin_fake_input;
struct wl_display;
struct wl_registry;

namespace rwin::integration {
class FakeInput {
public:
    explicit FakeInput(const std::string& socket);
    ~FakeInput();

    FakeInput(const FakeInput&) = delete;
    FakeInput& operator=(const FakeInput&) = delete;
    FakeInput(FakeInput&&) = delete;
    FakeInput& operator=(FakeInput&&) = delete;

    [[nodiscard]] bool usable() const noexcept { return input_ != nullptr; }

    void move_to(double x, double y);
    void press_button();
    void release_button();
    void press_key(std::uint32_t evdev_code);
    void release_key(std::uint32_t evdev_code);

private:
    static void announced(void* self, wl_registry* registry, std::uint32_t name, const char* interface,
                          std::uint32_t version);
    static void withdrawn(void* self, wl_registry* registry, std::uint32_t name);
    void settle();

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    org_kde_kwin_fake_input* input_ = nullptr;
};

}
