#include "guest/windows/window_tracker.hpp"

#include "guest/session/outbox.hpp"
#include "guest/windows/min_max_reader.hpp"
#include "guest/windows/window_directory.hpp"
#include "logging/logger.hpp"
#include "proto/feature_set.hpp"
#include "proto/message_writer.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <cwchar>
#include <iterator>
#include <utility>
#include <vector>

namespace rwin::guest {

namespace {

// RichEdit's own dropdowns, the one kind RAIL deletes on hide rather than
// keeping hidden: they are made again for every drop.
bool rich_edit_dropdown(HWND window) {
    wchar_t name[32] = {};
    if (::GetClassNameW(window, name, 32) <= 0) return false;
    return std::wcscmp(name, L"REComboBox20W") == 0 || std::wcscmp(name, L"REListBox20W") == 0;
}

// The style words as sent, against the window's now, the tool-window bit
// RAIL forces aside.
bool restyled(HWND window, const WindowFields::Style& sent) {
    const auto style = static_cast<std::uint32_t>(::GetWindowLongPtrW(window, GWL_STYLE));
    const auto extended = static_cast<std::uint32_t>(::GetWindowLongPtrW(window, GWL_EXSTYLE));
    constexpr std::uint32_t kForced = WS_EX_TOOLWINDOW;
    return (style & WindowFields::Style::kStyleMask) != sent.style
           || (extended & WindowFields::Style::kExtendedMask & ~kForced) != (sent.extended & ~kForced);
}

BOOL CALLBACK collect(HWND window, LPARAM into) {
    reinterpret_cast<std::vector<HWND>*>(into)->push_back(window);
    return TRUE;
}

std::uint32_t fields_for(const FeatureSet& features) {
    std::uint32_t fields = WindowFields::kAll;
    if (!features.has(Feature::kAppId)) fields &= ~WindowFields::kAppId;
    if (!features.has(Feature::kPopupIntent)) fields &= ~WindowFields::kPopup;
    return fields;
}

}  // namespace

WindowTracker::WindowTracker(WindowDirectory& directory, const WindowReader& reader, Outbox& outbox,
                             const FeatureSet& features)
    : directory_(directory),
      reader_(reader),
      outbox_(outbox),
      fields_(fields_for(features)),
      says_min_max_(features.has(Feature::kMoveSize)) {}

void WindowTracker::sync(SyncBegin::Reason reason) {
    MessageWriter begin{MessageId::SyncBegin};
    SyncBegin{reason}.write(begin);
    outbox_.send(begin.take());
    std::vector<HWND> windows;
    ::EnumWindows(&collect, reinterpret_cast<LPARAM>(&windows));
    sweep();
    std::vector<void*> known;
    known.reserve(records_.size());
    for (const auto& entry : records_) known.push_back(entry.first);
    // Reading a window may admit its owner, which adds to records_.
    for (void* window : known) {
        Record& record = records_.at(window);
        record.sent = fields_of(window, record, reader_.read_app_id(window));
        send_new(record);
    }
    for (HWND window : windows) {
        if (!records_.contains(window) && WindowReader::effectively_visible(window)) static_cast<void>(admit(window));
    }
    outbox_.send(MessageWriter{MessageId::SyncEnd}.take());
    RWIN_INFO("windows", "synchronised: {} window(s) of {} on the desktop", records_.size(), windows.size());
}

void WindowTracker::changed(void* window, Change change) {
    const bool known = records_.contains(window);
    switch (change) {
        case Change::Destroyed:
            if (known) forget(window);
            return;
        case Change::Hidden:
            if (!known) return;
            if (rich_edit_dropdown(static_cast<HWND>(window))) {
                forget(window);
                return;
            }
            refresh(window);
            return;
        case Change::Reparented:
            if (known && !WindowReader::top_level(window)) {
                forget(window);
                return;
            }
            break;
        case Change::Restyled:
        case Change::Shown:
        case Change::Moved:
        case Change::Focused:
            break;
    }
    if (!known) {
        if (WindowReader::effectively_visible(window)) static_cast<void>(admit(window));
        return;
    }
    if (change == Change::Shown) {
        Record& record = records_.at(window);
        const WindowFields now = fields_of(window, record, reader_.read_app_id(window));
        if (const std::uint32_t mask = now.differences(record.sent) & fields_; mask != 0) {
            MessageWriter out{MessageId::WindowChanged};
            record.id.write(out);
            now.write(out, mask);
            outbox_.send(out.take());
            record.sent = now;
        }
        return;
    }
    refresh(window);
}

void WindowTracker::refresh(void* window) {
    const auto found = records_.find(window);
    if (found == records_.end()) return;
    Record& record = found->second;
    const WindowFields now = fields_of(window, record, record.sent.app_id);
    const std::uint32_t mask = now.differences(record.sent) & fields_;
    if (mask == 0) return;
    MessageWriter out{MessageId::WindowChanged};
    record.id.write(out);
    now.write(out, mask);
    outbox_.send(out.take());
    record.sent = now;
}

void WindowTracker::refresh_all() {
    std::vector<void*> windows;
    windows.reserve(records_.size());
    for (const auto& entry : records_) windows.push_back(entry.first);
    for (void* window : windows) refresh(window);
}

void WindowTracker::sweep() {
    std::vector<void*> gone;
    std::vector<void*> changed;
    for (const auto& [window, record] : records_) {
        const auto hwnd = static_cast<HWND>(window);
        if (::IsWindow(hwnd) == FALSE) {
            gone.push_back(window);
        } else if (::GetWindow(hwnd, GW_OWNER) != record.owner || restyled(hwnd, record.sent.style)) {
            changed.push_back(window);
        }
    }
    for (void* window : gone) forget(window);
    for (void* window : changed) refresh(window);
}

std::optional<WindowId> WindowTracker::id_of(void* window) const {
    const auto found = records_.find(window);
    if (found == records_.end()) return std::nullopt;
    return found->second.id;
}

std::optional<WindowId> WindowTracker::admitted(void* window) {
    if (const std::optional<WindowId> known = id_of(window)) return known;
    if (admitting_ >= kMostAdmittedAtOnce || !WindowReader::effectively_visible(window)) return std::nullopt;
    return admit(window);
}

std::optional<WindowId> WindowTracker::admit(void* window) {
    if (!reader_.admissible(window)) return std::nullopt;
    if (records_.size() >= kMostWindows) {
        if (!full_) RWIN_WARN("windows", "{} windows tracked: no more are admitted", kMostWindows);
        full_ = true;
        return std::nullopt;
    }
    ++admitting_;
    const WindowId id{static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(window)), ++serial_};
    directory_.admit(id);
    // Placed before it is read: a window owning itself through a loop finds
    // itself tracked rather than admitting itself again.
    Record& record = records_.insert_or_assign(window, Record{id, {}}).first->second;
    record.sent = fields_of(window, record, reader_.read_app_id(window));
    --admitting_;
    RWIN_DEBUG("windows", "admitted {} '{}' class '{}' pid {}", id, record.sent.title, record.sent.class_name,
               record.sent.app_id.process_id);
    send_new(record);
    return id;
}

void WindowTracker::send_new(const Record& record) {
    MessageWriter out{MessageId::WindowNew};
    record.id.write(out);
    record.sent.write(out, fields_);
    outbox_.send(out.take());
    if (!says_min_max_) return;
    MessageWriter limits{MessageId::MinMax};
    read_min_max(reinterpret_cast<void*>(static_cast<std::uintptr_t>(record.id.hwnd)), record.id).write(limits);
    outbox_.send(limits.take());
}

void WindowTracker::forget(void* window) {
    const auto found = records_.find(window);
    if (found == records_.end()) return;
    const WindowId id = found->second.id;
    records_.erase(found);
    directory_.forget(id);
    full_ = false;
    MessageWriter out{MessageId::WindowGone};
    id.write(out);
    outbox_.send(out.take());
    RWIN_DEBUG("windows", "{} gone", id);
}

WindowFields WindowTracker::fields_of(void* window, Record& record, const WindowFields::AppId& app) {
    record.owner = ::GetWindow(static_cast<HWND>(window), GW_OWNER);
    WindowReader::Reading reading = reader_.read(window, record.id);
    reading.fields.app_id = app;
    reading.fields.owner = owner_id(reading);
    if (reading.anchor != nullptr) {
        if (const std::optional<WindowId> anchor = admitted(reading.anchor)) reading.fields.popup.anchor = *anchor;
    }
    if (reading.fields.popup.anchor.none()) reading.fields.popup = WindowFields::Popup{};
    return std::move(reading.fields);
}

// An owner the host is not told about - one of the shell's - is named by
// its handle alone, which names nothing on the host: the window stands on
// its own there.
WindowId WindowTracker::owner_id(const WindowReader::Reading& reading) {
    if (reading.hidden_owner) return WindowId::hidden_owner();
    if (reading.owner == nullptr) return WindowId{};
    if (const std::optional<WindowId> owner = admitted(reading.owner)) return *owner;
    return WindowId{static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(reading.owner)), 0};
}

}  // namespace rwin::guest
