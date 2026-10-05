#include "guest/capture/slot_ledger.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

void every_slot_starts_free_and_is_the_hosts_once_filled() {
    rwin::guest::SlotLedger ledger{4};
    check(ledger.free_slot() == 0u, "a new ring's first slot is free");
    check(ledger.filled(0) == 1, "the first copy into a slot is copy 1");
    check(ledger.free_slot() == 1u, "a filled slot is not offered again");
    check(ledger.filled(1) == 1, "each slot counts its own copies");
    check(ledger.filled(2) == 1, "the third slot too");
    check(!ledger.free_slot().has_value(), "with all three the host's, none is free");
}

// The device counts every copy into a surface from the day it was shared,
// so a slot's count goes on across frees: the n-th copy is "copy n".
void counts_go_on_across_frees() {
    rwin::guest::SlotLedger ledger{1};
    check(ledger.filled(0) == 1, "copy 1");
    check(ledger.freed(1, 0), "the host frees it");
    check(ledger.free_slot() == 0u, "and it is offered again");
    check(ledger.filled(0) == 2, "the next copy into it is copy 2, not 1 again");
}

// A ring replaced by a larger one has slots of the same numbers; a free
// the host sent for the old ring must not hand a slot of the new one back
// while the host still reads it.
void a_free_for_another_ring_changes_nothing() {
    rwin::guest::SlotLedger ledger{8};
    static_cast<void>(ledger.filled(0));
    check(!ledger.freed(7, 0), "a free for the ring this replaced is refused");
    check(ledger.free_slot() == 1u, "and slot 0 stays the host's");
    check(!ledger.freed(8, 2), "a slot the host never had is not freed");
    check(!ledger.freed(8, 9), "nor one past the ring");
    check(ledger.freed(8, 0), "its own ring's free is taken");
    check(!ledger.freed(8, 0), "once");
}

}  // namespace

int main() {
    every_slot_starts_free_and_is_the_hosts_once_filled();
    counts_go_on_across_frees();
    a_free_for_another_ring_changes_nothing();
    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("slot ledger: all checks passed\n");
    return EXIT_SUCCESS;
}
