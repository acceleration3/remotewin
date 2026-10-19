#include "guest/cursor/sent_cursors.hpp"

#include <algorithm>

namespace rwin::guest {

bool SentCursors::shown(std::uint64_t id) {
    const auto found = std::find(ids_.begin(), ids_.end(), id);
    const bool sent = found != ids_.end();
    if (sent) ids_.erase(found);
    if (ids_.size() >= kCapacity) ids_.erase(ids_.begin());
    ids_.push_back(id);
    return sent;
}

}  // namespace rwin::guest
