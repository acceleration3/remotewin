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

class WindowTracker {
public:
    enum class Change { Destroyed, Shown, Hidden, Moved, Focused, Restyled, Reparented };

    static constexpr std::size_t kMostWindows = 256;

    WindowTracker(WindowDirectory& directory, const WindowReader& reader, Outbox& outbox,
                  const FeatureSet& features);

    void sync(SyncBegin::Reason reason);

    void changed(void* window, Change change);
    void refresh(void* window);
    void refresh_all();
    void sweep();

    [[nodiscard]] std::optional<WindowId> id_of(void* window) const;
    [[nodiscard]] std::optional<WindowId> admitted(void* window);

private:
    struct Record {
        WindowId id;
        WindowFields sent;
        void* owner = nullptr;
        std::optional<PopupIntent::Point> opened_at;
    };

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
    std::optional<WindowIcons> icons_;
    std::uint32_t serial_ = 0;
    int admitting_ = 0;
    bool full_ = false;
};

}
