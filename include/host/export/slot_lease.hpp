#pragma once

#include "proto/slot_free.hpp"

namespace rwin::host {

class GuestLink;

class SlotLease {
public:
    SlotLease(GuestLink& link, SlotFree slot) : link_(link), slot_(slot) {}
    ~SlotLease();

    SlotLease(const SlotLease&) = delete;
    SlotLease& operator=(const SlotLease&) = delete;
    SlotLease(SlotLease&&) = delete;
    SlotLease& operator=(SlotLease&&) = delete;

private:
    GuestLink& link_;
    SlotFree slot_;
};

}
