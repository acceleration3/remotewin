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

namespace rwin::host {

namespace {

constexpr const char* kPath = "/remotewin/placement";
constexpr const char* kInterface = "org.remotewin.Placement";
// Long enough for a window to be mapped after it was shown; a placement
// still unanswered then is for a window KWin never had.
constexpr std::int64_t kPlacementTimeoutNs = 3'000'000'000;
constexpr std::chrono::milliseconds kExpiryCheck{500};
constexpr std::chrono::milliseconds kRewatch{1000};
constexpr int kMostWatchTries = 3;
constexpr std::int64_t kNanosecondsPerMillisecond = 1'000'000;

// INVISIBLE SEPARATOR, then the handle's low 32 bits two at a time as one
// of four zero-width characters. KWin keeps them in a caption (measured:
// it removes white space it trims, not these), and nothing draws them.
constexpr char16_t kMarker = 0x2063;
constexpr std::array<char16_t, 4> kDigits{0x200B, 0x200C, 0x200D, 0x2060};
constexpr int kDigitCount = 32;

// Finds this process's windows by their tag, and reports where each is
// put - not during a drag, which is reported once, where it ends - and
// when it is maximised, made fullscreen or minimised. Leaves a window out of
// the taskbar and the switcher as the character after its tag says.
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
function watch(w) {{
    if (w.pid !== {3}) return;
    w.frameGeometryChanged.connect(function() {{ if (!w.move && !w.resize) report(w, false); }});
    w.interactiveMoveResizeFinished.connect(function() {{ report(w, true); }});
    w.captionChanged.connect(function() {{ present(w); report(w, false); }});
    w.maximizedChanged.connect(function() {{ report(w, false); }});
    w.fullScreenChanged.connect(function() {{ report(w, false); }});
    w.minimizedChanged.connect(function() {{ report(w, false); }});
    present(w);
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
)JS";

// Puts one window's frame corner somewhere, now or as KWin maps it, at the
// size it has.
constexpr const char* kPlacer = R"JS(
const tag = "{0}";
function place(w) {{
    if (w.pid !== {1}) return false;
    const caption = w.captionNormal || "";
    if (caption.indexOf(tag) < 0) return false;
    const was = w.frameGeometry;
    w.frameGeometry = {{x: {2}, y: {3}, width: was.width, height: was.height}};
    const now = w.frameGeometry;
    callDBus("{4}", "{5}", "{6}", "placed", tag + " " + now.x + " " + now.y);
    return true;
}}
if (!workspace.windowList().some(place)) {{
    const waiting = function(w) {{ if (place(w)) workspace.windowAdded.disconnect(waiting); }};
    workspace.windowAdded.connect(waiting);
}}
)JS";

// Sends one window to an output, now or as KWin maps it.
constexpr const char* kSender = R"JS(
const tag = "{0}";
function send(w) {{
    if (w.pid !== {1}) return false;
    const caption = w.captionNormal || "";
    if (caption.indexOf(tag) < 0) return false;
    const output = workspace.screens.find(function(s) {{ return s.name === "{2}"; }});
    if (output && w.output !== output) workspace.sendClientToScreen(w, output);
    const now = w.frameGeometry;
    callDBus("{3}", "{4}", "{5}", "placed", tag + " " + now.x + " " + now.y);
    return true;
}}
if (!workspace.windowList().some(send)) {{
    const waiting = function(w) {{ if (send(w)) workspace.windowAdded.disconnect(waiting); }};
    workspace.windowAdded.connect(waiting);
}}
)JS";

void append_utf8(std::string& out, char16_t code) {
    out += static_cast<char>(0xE0 | (code >> 12));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code & 0x3F));
}

// A JavaScript string literal's body that is ``text``, every character
// escaped: the tag's characters, and an output's name, whatever it is.
std::string js_string(std::string_view text) {
    std::string out;
    for (std::size_t at = 0; at < text.size();) {
        const auto lead = static_cast<unsigned char>(text[at]);
        char32_t code = lead;
        std::size_t length = 1;
        if (lead >= 0xE0 && at + 2 < text.size()) {
            code = ((lead & 0x0Fu) << 12) | ((static_cast<unsigned char>(text[at + 1]) & 0x3Fu) << 6)
                   | (static_cast<unsigned char>(text[at + 2]) & 0x3Fu);
            length = 3;
        } else if (lead >= 0xC0 && at + 1 < text.size()) {
            code = ((lead & 0x1Fu) << 6) | (static_cast<unsigned char>(text[at + 1]) & 0x3Fu);
            length = 2;
        }
        out += std::format("\\u{:04x}", static_cast<unsigned>(code));
        at += length;
    }
    return out;
}

std::string number(double value) { return std::format("{}", value); }

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

}  // namespace

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

// KWin now and then answers a script's run with a file it cannot open -
// seen right after other scripts were unloaded - and the same script run
// again a moment later runs.
void KWinPlacement::watch() {
    watcher_ = std::make_unique<KWinScript>(
        bus_, script_name("watch"), std::format(kWatcher, service_, kPath, kInterface, ::getpid()),
        [this](bool ran, const std::string&) {
            if (ran) {
                RWIN_INFO("placement", "placing toplevel windows through KWin");
            } else if (++watch_tries_ < kMostWatchTries) {
                rewatch_->start(kRewatch, false);
            }
        });
}

KWinPlacement::~KWinPlacement() {
    pending_.clear();
    watcher_.reset();
    if (object_ != nullptr) sd_bus_slot_unref(object_);
}

std::string KWinPlacement::script_name(std::string_view role) {
    return std::format("remotewin-{}-{}-{}", ::getpid(), role, ++scripts_);
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
    run_for(window, std::format(kPlacer, js_string(tag_for(window)), ::getpid(), number(corner.x), number(corner.y),
                                service_, kPath, kInterface));
}

void KWinPlacement::send_to_output(std::uint64_t window, const std::string& output) {
    if (!active()) return;
    run_for(window, std::format(kSender, js_string(tag_for(window)), ::getpid(), js_string(output), service_, kPath,
                                kInterface));
}

// One script per window at a time: a newer one replaces what an older one
// was still waiting to do.
void KWinPlacement::run_for(std::uint64_t window, const std::string& source) {
    std::erase_if(pending_, [window](const Pending& pending) { return pending.window == window; });
    pending_.push_back(Pending{window, now_ns(), nullptr, false});
    pending_.back().script = std::make_unique<KWinScript>(bus_, script_name("place"), source,
                                                          [this, window](bool ran, const std::string&) {
                                                              if (ran) return;
                                                              for (Pending& pending : pending_) {
                                                                  if (pending.window == window) pending.failed = true;
                                                              }
                                                          });
}

int KWinPlacement::placed_call(sd_bus_message* call, void* self, sd_bus_error* /*error*/) {
    return string_call(call, self, &KWinPlacement::placed);
}

int KWinPlacement::moved_call(sd_bus_message* call, void* self, sd_bus_error* /*error*/) {
    return string_call(call, self, &KWinPlacement::moved);
}

int KWinPlacement::outputs_call(sd_bus_message* call, void* self, sd_bus_error* /*error*/) {
    return string_call(call, self, &KWinPlacement::outputs);
}

void KWinPlacement::placed(std::string_view report) {
    const std::vector<std::string_view> parts = words_of(report, ' ');
    const std::optional<std::uint64_t> window = parts.size() == 3 ? window_of(parts[0]) : std::nullopt;
    if (!window.has_value()) {
        RWIN_WARN("placement", "a placement report that names no window: '{}'", report);
        return;
    }
    const auto pending = std::find_if(pending_.begin(), pending_.end(),
                                      [&](const Pending& entry) { return entry.window == *window; });
    const std::int64_t took = pending == pending_.end() ? -1 : (now_ns() - pending->since) / kNanosecondsPerMillisecond;
    if (pending != pending_.end()) pending_.erase(pending);
    const PointF at{to_double(parts[1]), to_double(parts[2])};
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

// KWin has each output's scale itself, fraction and all.
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
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (!it->failed && now - it->since < kPlacementTimeoutNs) {
            ++it;
            continue;
        }
        const std::uint64_t window = it->window;
        if (!it->failed) RWIN_WARN("placement", "{:#x} was not placed: KWin never had it", window);
        it = pending_.erase(it);
        window_not_placed(window);
    }
}

}  // namespace rwin::host
