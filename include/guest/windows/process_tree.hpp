#pragma once

#include "guest/windows/session_boundary.hpp"
#include "proto/window_fields.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace rwin::guest {

// Which launched program a process belongs to: the tree it is in, as the
// processes it descends from.
//
// A program's tree is rooted at the topmost process that is no part of the
// session itself (SessionBoundary) - the launched program, whatever started
// it - and holds that process and every process it ever started, however
// long ago and whether or not their parents are still running: Steam's one
// window is made by a helper its launcher started, and the launcher may be
// gone. So each process this has seen is remembered as (parent, creation
// time, whether it is a boundary), learned while it was alive, and a walk
// up goes through the memory where Windows no longer has the parent. A
// parent pid is believed only when a process of that pid was alive when the
// child was created: one created after its child is a reused pid, and one
// last seen alive before it is an earlier holder of the pid - either ends
// the walk. A process whose image cannot be read is taken for the session's
// own (a service's, which the guest half cannot look into) and bounds the
// tree.
//
// Learning: a process is read when a window of it, or a descendant, asks;
// and everything alive is read, at most once a second, when asked about, so
// a parent that dies before its child's window appears was seen before it
// did. When the memory is full, processes that are dead and that nothing
// alive descends from are forgotten, oldest first. Not thread safe: the
// window watch's thread uses it alone.
class ProcessTree {
public:
    // More ancestors than this are not followed; a tree is not deeper.
    static constexpr std::size_t kMostDepth = 12;

    // ``pid`` first, then its parents nearest first, up to and including the
    // tree's root: just ``pid`` for a process that is its own root, and
    // nothing for a process this cannot read.
    [[nodiscard]] std::vector<WindowFields::ProcessRef> tree_of(std::uint32_t pid);

private:
    struct Record {
        std::uint64_t created = 0;
        std::uint32_t parent = 0;
        bool boundary = false;
        // The last time (FILETIME ticks) it was seen alive.
        std::uint64_t last_seen = 0;
    };
    using Key = std::pair<std::uint32_t, std::uint64_t>;

    // The record of the live process ``pid``, made if it is new and noted
    // alive now; nothing when it cannot be read. A copy: the memory is
    // pruned as it fills.
    std::optional<Record> learn(std::uint32_t pid);
    // The process of ``pid`` that was alive when a child was created at
    // ``child_created``, as last known.
    [[nodiscard]] std::optional<Record> remembered(std::uint32_t pid, std::uint64_t child_created) const;
    void learn_everything_alive();
    void keep(std::uint32_t pid, const Record& record);
    // Dead processes nothing alive descends from, forgotten.
    void prune();

    SessionBoundary boundary_;
    // Each pid's processes, oldest first.
    std::map<std::uint32_t, std::vector<Record>> records_;
    std::size_t kept_ = 0;
    std::uint64_t learned_all_at_ = 0;
    // When (FILETIME ticks) the last read of everything alive began.
    std::uint64_t last_snapshot_ = 0;
};

}  // namespace rwin::guest
