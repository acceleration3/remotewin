#include "host/export/copy_wait.hpp"

namespace rwin::host {

CopyWait::Verdict CopyWait::answered(std::uint64_t copies_run) noexcept {
    if (copies_run >= copies_) return Verdict::Ready;
    return ++asks_ < kMostAsks ? Verdict::AskAgain : Verdict::GiveUp;
}

}  // namespace rwin::host
