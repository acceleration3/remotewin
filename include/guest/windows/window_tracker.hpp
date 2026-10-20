#pragma once

#include "guest/windows/window_icons.hpp"
#include "guest/windows/window_reader.hpp"
#include "proto/sync_begin.hpp"
#include "proto/window_fields.hpp"
#include "proto/window_id.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace rwin {
class FeatureSet;
}

namespace rwin::guest {

class Outbox;
class WindowDirectory;

// The windows of the guest's session the host is told about, and what it
// was last told of each (rdpshell's CHookEventProcessor and CHookWindow,
// docs/rail-model.md §2.1-§2.2): a window is admitted the first time an
// event finds it shown, gets a serial, and is said whole (WindowNew); every
// later event recomputes its record and says only what differs
// (WindowChanged); it is gone only when Windows destroys it (WindowGone).
// A hidden window stays, shown as hidden.
//
// Lives on the window thread (WindowWatch), as do its callers.
class WindowTracker {
public:
    // What an event says happened to a top-level window.
    enum class Change { Destroyed, Shown, Hidden, Moved, Focused, Restyled, Reparented };

    // RAIL's: more tracked windows than this is an application gone wrong,
    // not a desktop.
    static constexpr std::size_t kMostWindows = 256;

    // ``features`` decides which fields are said (APPID, POPUP), whether a
    // window's MinMax follows its WindowNew, and whether icons are sent.
    WindowTracker(WindowDirectory& directory, const WindowReader& reader, Outbox& outbox,
                  const FeatureSet& features);

    // Every window of the desktop said afresh, between SyncBegin and
    // SyncEnd: anything the host knew and is not said again is gone.
    void sync(SyncBegin::Reason reason);

    void changed(void* window, Change change);
    // Re-read a tracked window, saying what differs; every one of them.
    void refresh(void* window);
    void refresh_all();
    // Windows destroyed or made children without their event reaching
    // this, and windows given another owner or other styles:
    // SetWindowLongPtr and SetWindowPos(HWND_TOPMOST) raise no event at all.
    void sweep();

    [[nodiscard]] std::optional<WindowId> id_of(void* window) const;
    // ``window``'s id, admitting it first if it is one to admit.
    [[nodiscard]] std::optional<WindowId> admitted(void* window);

private:
    struct Record {
        WindowId id;
        WindowFields sent;
        // GW_OWNER as last read, before RAIL's normalising.
        void* owner = nullptr;
        // Where the pointer was when the window was last seen to be shown:
        // a context menu or a tooltip opened there (PopupIntent).
        std::optional<PopupIntent::Point> opened_at;
    };

    // Admit at most this deep through owners and anchors that are not
    // tracked yet: a chain that loops is no chain.
    static constexpr int kMostAdmittedAtOnce = 8;

    std::optional<WindowId> admit(void* window);
    void forget(void* window);
    [[nodiscard]] WindowFields fields_of(void* window, Record& record, const WindowFields::AppId& app);
    [[nodiscard]] WindowId owner_id(const WindowReader::Reading& reading);
    void send_new(const Record& record);
    void look_at_icon(void* window, const Record& record);

    WindowDirectory& directory_;
    const WindowReader& reader_;
    Outbox& outbox_;
    std::uint32_t fields_;
    bool says_min_max_;
    std::unordered_map<void*, Record> records_;
    // None when the host does not speak icons.
    std::optional<WindowIcons> icons_;
    std::uint32_t serial_ = 0;
    int admitting_ = 0;
    bool full_ = false;
};

}  // namespace rwin::guest
