#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct wl_surface;
struct xdg_activation_token_v1;

namespace rwin::host {

class Globals;
class Seat;

// Asking the compositor to activate a surface (xdg_activation_v1): with a
// token someone handed over - the launcher's, through LSW, at start or with
// RAISE - or with one asked for here, as the user's latest press, which is
// how focus is handed from one window of this client to another.
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
    // A surface is going: a token on its way for it activates nothing.
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

}  // namespace rwin::host
