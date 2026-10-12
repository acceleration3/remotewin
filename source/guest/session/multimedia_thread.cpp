#include "guest/session/multimedia_thread.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
#include <avrt.h>
// clang-format on

namespace rwin::guest {

// "Games": the task MMCSS gives the highest priority short of audio, which
// is for what a player sees and does as it happens.
MultimediaThread::MultimediaThread() {
    DWORD task = 0;
    registration_ = ::AvSetMmThreadCharacteristicsW(L"Games", &task);
    if (registration_ == nullptr) {
        RWIN_DEBUG("session", "this thread keeps ordinary priority: MMCSS said {}", ::GetLastError());
        return;
    }
    if (::AvSetMmThreadPriority(registration_, AVRT_PRIORITY_HIGH) == FALSE) {
        RWIN_DEBUG("session", "this thread keeps MMCSS's own priority: {}", ::GetLastError());
    }
}

MultimediaThread::~MultimediaThread() {
    if (registration_ != nullptr) ::AvRevertMmThreadCharacteristics(registration_);
}

}  // namespace rwin::guest
