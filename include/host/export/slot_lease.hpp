#pragma once

#include "proto/slot_free.hpp"

#include <QPointer>

namespace rwin::host {

class GuestLink;

// A slot of a guest window's ring, held while this side may read it. Let
// go of - the frame in it replaced and every draw from it done on the GPU,
// or the frame dropped unseen - the slot is the guest's again (SlotFree).
class SlotLease {
public:
    SlotLease(GuestLink& link, SlotFree slot) : link_(&link), slot_(slot) {}
    ~SlotLease();

    SlotLease(const SlotLease&) = delete;
    SlotLease& operator=(const SlotLease&) = delete;
    SlotLease(SlotLease&&) = delete;
    SlotLease& operator=(SlotLease&&) = delete;

private:
    QPointer<GuestLink> link_;
    SlotFree slot_;
};

}  // namespace rwin::host
