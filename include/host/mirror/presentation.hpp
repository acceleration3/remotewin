#pragma once

#include "host/loop/signal.hpp"

namespace rwin::host {

struct MirrorRole;

class Presentation {
public:
    void pictured();
    void mapped(const MirrorRole& role);

    [[nodiscard]] bool begun() const noexcept { return begun_; }

    Signal<> began;

private:
    void begin();

    bool begun_ = false;
};

}
