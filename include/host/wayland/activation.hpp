#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct wl_surface;
struct xdg_activation_token_v1;

namespace rwin::host {

class Globals;
class Seat;

class Activation {
public:
    Activation(const Globals& globals, Seat& seat);
    ~Activation();

    Activation(const Activation&) = delete;
    Activation& operator=(const Activation&) = delete;
    Activation(Activation&&) = delete;
    Activation& operator=(Activation&&) = delete;

    [[nodiscard]] bool offered() const noexcept;
    void activate(wl_surface* surface, const std::string& token);
    void hand_to(wl_surface* surface);
    void forget(wl_surface* surface) noexcept;

private:
    struct Asked {
        xdg_activation_token_v1* token;
        wl_surface* target;
    };

    static void done(void* self, xdg_activation_token_v1* token, const char* name);

    const Globals& globals_;
    Seat& seat_;
    std::vector<Asked> asked_;
};

}
