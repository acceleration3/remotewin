#include "host/mirror/presentation.hpp"

#include "host/mirror/mirror_role.hpp"

namespace rwin::host {

void Presentation::pictured() { begin(); }

void Presentation::mapped(const MirrorRole& role) {
    if (role.kind == MirrorRole::Kind::Toplevel) begin();
}

void Presentation::begin() {
    if (begun_) return;
    begun_ = true;
    began();
}

}
