#include "host/export/slot_lease.hpp"

#include "host/link/guest_link.hpp"

namespace rwin::host {

SlotLease::~SlotLease() {
    if (!link_.isNull()) link_->send_slot_free(slot_);
}

}  // namespace rwin::host
