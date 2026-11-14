#include "host/placement/kwin_placement.hpp"

#include "host/dbus/bus_connection.hpp"
#include "host/latency/frame_stamps.hpp"
#include "logging/logger.hpp"

#include <systemd/sd-bus.h>

#include <unistd.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <string>
#include <utility>

namespace rwin::host {

namespace {

constexpr const char* kPath = "/remotewin/placement";
constexpr const char* kInterface = "org.remotewin.Placement";
constexpr std::int64_t kPlacementTimeoutNs = 3'000'000'000;
constexpr std::chrono::milliseconds kExpiryCheck{500};
constexpr std::chrono::milliseconds kRewatch{1000};
constexpr int kMostWatchTries = 3;
constexpr std::int64_t kAskHeldNs = 10'000'000'000;
constexpr std::int64_t kNanosecondsPerMillisecond = 1'000'000;

constexpr char16_t kMarker = 0x2063;
constexpr std::array<char16_t, 4> kDigits{0x200B, 0x200C, 0x200D, 0x2060};
constexpr int kDigitCount = 32;

constexpr const char* kWatcher = R"JS(
const service = "{0}";
const marker = String.fromCharCode(0x2063);
const digits = [0x200B, 0x200C, 0x200D, 0x2060];
const tagLength = 33;
function tagOf(w) {{
    const caption = w.captionNormal || "";
    const at = caption.indexOf(marker);
    return at < 0 ? "" : caption.substring(at, at + tagLength);
}}
function present(w) {{
    const caption = w.captionNormal || "";
    const at = caption.indexOf(marker);
    const skips = at < 0 ? -1 : digits.indexOf(caption.charCodeAt(at + tagLength));
    if (skips < 0) return;
    w.skipTaskbar = (skips & 1) !== 0;
    w.skipSwitcher = (skips & 2) !== 0;
}}
function report(w, dragged) {{
    const tag = tagOf(w);
    if (tag === "") return;
    const g = w.frameGeometry;
    const state = w.maximizeMode !== 0 || w.fullScreen;
    callDBus(service, "{1}", "{2}", "moved",
             [tag, g.x, g.y, g.width, g.height, dragged ? 1 : 0, state ? 1 : 0, w.minimized ? 1 : 0].join(" "));
}}
const awaited = {{}};
function carry(order, w) {{
    if (order[0] === "place") {{
        if (w.maximizeMode === 0 && !w.fullScreen) {{
            const was = w.frameGeometry;
            w.frameGeometry = {{x: Number(order[3]), y: Number(order[4]), width: was.width, height: was.height}};
        }}
    }} else {{
        const name = order.slice(3).join(" ");
        const output = workspace.screens.find(function(s) {{ return s.name === name; }});
        if (output && w.output !== output) workspace.sendClientToScreen(w, output);
    }}
    const now = w.frameGeometry;
    callDBus(service, "{1}", "{2}", "placed", [order[1], order[2], now.x, now.y].join(" "));
}}
function meet(w) {{
    const tag = tagOf(w);
    if (tag === "" || !awaited[tag]) return;
    const order = awaited[tag];
    delete awaited[tag];
    carry(order, w);
}}
function take(text) {{
    if (text !== "") {{
        const order = text.split(" ");
        const w = workspace.windowList().find(function(w) {{ return w.pid === {3} && tagOf(w) === order[1]; }});
        if (w) {{
            delete awaited[order[1]];
            carry(order, w);
        }} else {{
            awaited[order[1]] = order;
        }}
    }}
    callDBus(service, "{1}", "{2}", "next", take);
}}
function watch(w) {{
    if (w.pid !== {3}) return;
    w.frameGeometryChanged.connect(function() {{ if (!w.move && !w.resize) report(w, false); }});
    w.interactiveMoveResizeFinished.connect(function() {{ report(w, true); }});
    w.captionChanged.connect(function() {{ present(w); meet(w); report(w, false); }});
    w.maximizedChanged.connect(function() {{ report(w, false); }});
    w.fullScreenChanged.connect(function() {{ report(w, false); }});
    w.minimizedChanged.connect(function() {{ report(w, false); }});
    present(w);
    meet(w);
    report(w, false);
}}
workspace.windowList().forEach(watch);
workspace.windowAdded.connect(watch);
function outputs() {{
    callDBus(service, "{1}", "{2}", "outputs",
             workspace.screens.map(function(s) {{
                 return s.name + " " + s.geometry.x + " " + s.geometry.y + " " + s.geometry.width + " " + s.geometry.height + " " + s.devicePixelRatio;
             }}).join(";"));
}}
workspace.screensChanged.connect(outputs);
outputs();
take("");
)JS";

void append_utf8(std::string& out, char16_t code) {
    out += static_cast<char>(0xE0 | (code >> 12));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code & 0x3F));
}

std::vector<std::string_view> words_of(std::string_view text, char separator) {
    std::vector<std::string_view> words;
    while (!text.empty()) {
        const std::size_t end = text.find(separator);
        if (end != 0) words.push_back(text.substr(0, end));
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return words;
}

std::uint32_t to_serial(std::string_view text) {
    std::uint32_t value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

double to_double(std::string_view text) {
    double value = 0.0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

int string_call(sd_bus_message* call, void* self, void (KWinPlacement::*method)(std::string_view)) {
    const char* report = nullptr;
    if (sd_bus_message_read(call, "s", &report) < 0 || report == nullptr) {
        return sd_bus_reply_method_errorf(call, SD_BUS_ERROR_INVALID_ARGS, "a report is one string");
    }
    (static_cast<KWinPlacement*>(self)->*method)(report);
    return sd_bus_reply_method_return(call, "");
}

}

KWinPlacement::KWinPlacement(EventLoop& loop, BusConnection& bus) : bus_(bus) {
    if (!KWinScript::reachable(bus)) {
        RWIN_INFO("placement", "not on KWin: toplevel windows are placed by the compositor");
        return;
    }
    static const sd_bus_vtable kVtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("placed", "s", "", &KWinPlacement::placed_call, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("moved", "s", "", &KWinPlacement::moved_call, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("outputs", "s", "", &KWinPlacement::outputs_call, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("next", "", "s", &KWinPlacement::next_call, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_VTABLE_END,
    };
    if (sd_bus_add_object_vtable(bus.bus(), &object_, kPath, kInterface, kVtable, this) < 0) {
        RWIN_WARN("placement", "cannot put the placement object on the session bus");
        return;
    }
    service_ = bus.unique_name();
    rewatch_ = loop.timer([this] { watch(); });
    watch();
    expiry_ = loop.timer([this] { expire(); });
    expiry_->start(kExpiryCheck, true);
}

void KWinPlacement::watch() {
    watcher_ = std::make_unique<KWinScript>(
        bus_, std::format("remotewin-{}-watch-{}", ::getpid(), ++scripts_), std::format(kWatcher, service_, kPath, kInterface, ::getpid()),
        [this](bool ran, const std::string&) {
            if (ran) {
                RWIN_INFO("placement", "placing toplevel windows through KWin");
            } else if (++watch_tries_ < kMostWatchTries) {
                rewatch_->start(kRewatch, false);
            }
        });
}

KWinPlacement::~KWinPlacement() {
    if (asking_ != nullptr) sd_bus_message_unref(asking_);
    watcher_.reset();
    if (object_ != nullptr) sd_bus_slot_unref(object_);
}

std::string KWinPlacement::tag_for(std::uint64_t window) {
    std::string tag;
    append_utf8(tag, kMarker);
    for (int digit = kDigitCount - 1; digit >= 0; --digit) append_utf8(tag, kDigits[(window >> (digit * 2)) & 0x3]);
    return tag;
}

std::string KWinPlacement::caption_tag(std::uint64_t window, Skips skips) {
    std::string tag = tag_for(window);
    append_utf8(tag, kDigits[(skips.taskbar ? 1 : 0) | (skips.switcher ? 2 : 0)]);
    return tag;
}

std::optional<std::uint64_t> KWinPlacement::window_of(std::string_view tag) {
    constexpr std::size_t kCharBytes = 3;
    if (tag.size() != (kDigitCount + 1) * kCharBytes) return std::nullopt;
    std::string marker;
    append_utf8(marker, kMarker);
    if (!tag.starts_with(marker)) return std::nullopt;
    std::uint64_t window = 0;
    for (std::size_t at = kCharBytes; at < tag.size(); at += kCharBytes) {
        const std::string_view digit = tag.substr(at, kCharBytes);
        const auto known = std::find_if(kDigits.begin(), kDigits.end(), [&](char16_t code) {
            std::string encoded;
            append_utf8(encoded, code);
            return encoded == digit;
        });
        if (known == kDigits.end()) return std::nullopt;
        window = (window << 2) | static_cast<std::uint64_t>(known - kDigits.begin());
    }
    return window;
}

void KWinPlacement::place(std::uint64_t window, PointF corner) {
    if (!active()) return;
    order(window, std::format("place {} {}", corner.x, corner.y));
}

void KWinPlacement::send_to_output(std::uint64_t window, const std::string& output) {
    if (!active()) return;
    order(window, "send " + output);
}

void KWinPlacement::order(std::uint64_t window, std::string what) {
    const auto known = std::find_if(orders_.begin(), orders_.end(),
                                    [window](const Order& order) { return order.window == window; });
    if (known != orders_.end() && known->handed) {
        known->next = std::move(what);
        return;
    }
    if (known != orders_.end()) {
        known->what = std::move(what);
        known->serial = ++serials_;
        return;
    }
    orders_.push_back(Order{window, ++serials_, now_ns(), std::move(what), std::string(), false});
    hand_over();
}

void KWinPlacement::hand_over() {
    if (asking_ == nullptr) return;
    const auto waiting = std::find_if(orders_.begin(), orders_.end(), [](const Order& order) { return !order.handed; });
    if (waiting == orders_.end()) return;
    const std::size_t verb = waiting->what.find(' ');
    answer(std::format("{} {} {}{}", std::string_view(waiting->what).substr(0, verb), tag_for(waiting->window),
                       waiting->serial, std::string_view(waiting->what).substr(verb)));
    waiting->handed = true;
    waiting->since = now_ns();
}

void KWinPlacement::answer(const std::string& text) {
    sd_bus_message* asking = std::exchange(asking_, nullptr);
    if (sd_bus_reply_method_return(asking, "s", text.c_str()) < 0) {
        RWIN_WARN("placement", "KWin's script could not be answered");
    }
    sd_bus_message_unref(asking);
    bus_.rearm();
}

int KWinPlacement::next_call(sd_bus_message* call, void* self, sd_bus_error* ) {
    auto* placement = static_cast<KWinPlacement*>(self);
    if (placement->asking_ != nullptr) placement->answer(std::string());
    placement->asking_ = sd_bus_message_ref(call);
    placement->asked_at_ = now_ns();
    placement->hand_over();
    return 1;
}

int KWinPlacement::placed_call(sd_bus_message* call, void* self, sd_bus_error* ) {
    return string_call(call, self, &KWinPlacement::placed);
}

int KWinPlacement::moved_call(sd_bus_message* call, void* self, sd_bus_error* ) {
    return string_call(call, self, &KWinPlacement::moved);
}

int KWinPlacement::outputs_call(sd_bus_message* call, void* self, sd_bus_error* ) {
    return string_call(call, self, &KWinPlacement::outputs);
}

void KWinPlacement::placed(std::string_view report) {
    const std::vector<std::string_view> parts = words_of(report, ' ');
    constexpr std::size_t kParts = 4;
    const std::optional<std::uint64_t> window = parts.size() == kParts ? window_of(parts[0]) : std::nullopt;
    if (!window.has_value()) {
        RWIN_WARN("placement", "a placement report that names no window: '{}'", report);
        return;
    }
    const auto order = std::find_if(orders_.begin(), orders_.end(),
                                    [&](const Order& entry) { return entry.window == *window; });
    if (order != orders_.end() && (!order->handed || order->serial != to_serial(parts[1]))) {
        RWIN_DEBUG("placement", "{:#x}: a report of a placement since replaced; not taken", *window);
        return;
    }
    const std::int64_t took = order == orders_.end() ? -1 : (now_ns() - order->since) / kNanosecondsPerMillisecond;
    if (order != orders_.end() && !order->next.empty()) {
        order->what = std::exchange(order->next, std::string());
        order->serial = ++serials_;
        order->since = now_ns();
        order->handed = false;
        hand_over();
        return;
    }
    if (order != orders_.end()) orders_.erase(order);
    const PointF at{to_double(parts[2]), to_double(parts[3])};
    RWIN_DEBUG("placement", "{:#x} placed at {:.2f},{:.2f} in {} ms", *window, at.x, at.y, took);
    window_placed(*window, at, took);
}

void KWinPlacement::moved(std::string_view report) {
    const std::vector<std::string_view> parts = words_of(report, ' ');
    constexpr std::size_t kParts = 8;
    const std::optional<std::uint64_t> window = parts.size() == kParts ? window_of(parts[0]) : std::nullopt;
    if (!window.has_value()) {
        RWIN_WARN("placement", "a move report that names no window: '{}'", report);
        return;
    }
    const Report seen{BoxF{to_double(parts[1]), to_double(parts[2]), to_double(parts[3]), to_double(parts[4])},
                      parts[5] == "1", parts[6] == "1", parts[7] == "1"};
    RWIN_TRACE("placement", "{:#x} is at {:.2f},{:.2f} {:.2f}x{:.2f}{}{}{}", *window, seen.frame.x, seen.frame.y,
               seen.frame.width, seen.frame.height, seen.dragged ? ", dragged" : "",
               seen.by_state ? ", maximised or fullscreen" : "", seen.minimized ? ", minimised" : "");
    window_moved(*window, seen);
}

void KWinPlacement::outputs(std::string_view report) {
    std::vector<OutputLayout::Output> found;
    for (const std::string_view entry : words_of(report, ';')) {
        const std::vector<std::string_view> parts = words_of(entry, ' ');
        constexpr std::size_t kParts = 6;
        if (parts.size() != kParts) continue;
        found.push_back(OutputLayout::Output{std::string(parts[0]), PointF{to_double(parts[1]), to_double(parts[2])},
                                             PointF{to_double(parts[3]), to_double(parts[4])}, to_double(parts[5])});
        RWIN_INFO("placement", "output {} at {},{} {}x{} scale {}", parts[0], parts[1], parts[2], parts[3], parts[4],
                  parts[5]);
    }
    outputs_.replace(std::move(found));
    if (outputs_.known()) RWIN_INFO("placement", "the guest's monitor is output {}", outputs_.guest_output_name());
}

void KWinPlacement::expire() {
    const std::int64_t now = now_ns();
    if (asking_ != nullptr && now - asked_at_ >= kAskHeldNs) answer(std::string());
    for (auto it = orders_.begin(); it != orders_.end();) {
        if (now - it->since < kPlacementTimeoutNs) {
            ++it;
            continue;
        }
        const std::uint64_t window = it->window;
        RWIN_WARN("placement", "{:#x} was not placed: KWin never had it", window);
        it = orders_.erase(it);
        window_not_placed(window);
    }
}

}
