#pragma once

#include "proto/move_size_kind.hpp"

#include <optional>
#include <set>
#include <string>
#include <unordered_map>

namespace rwin::guest {

class Outbox;
class WindowTracker;

class MoveSizeReport {
public:
    MoveSizeReport(WindowTracker& tracker, Outbox& outbox);

    void started(void* window, long kind);
    void ended(void* window);

private:
    [[nodiscard]] bool ignored(void* window) const;

    WindowTracker& tracker_;
    Outbox& outbox_;
    std::set<std::wstring> ignored_;
    std::unordered_map<void*, MoveSizeKind> moving_;
};

}
