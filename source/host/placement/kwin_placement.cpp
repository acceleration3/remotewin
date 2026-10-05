#include "host/placement/kwin_placement.hpp"

#include "logging/logger.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <array>

namespace rwin::host {

namespace {

constexpr const char* kPath = "/remotewin/placement";
// Long enough for a window to be mapped after it was shown; a placement
// still unanswered then is for a window KWin never had.
constexpr qint64 kPlacementTimeoutMs = 3000;
constexpr int kExpiryCheckMs = 500;

// INVISIBLE SEPARATOR, then the handle's low 32 bits two at a time as one
// of four zero-width characters. KWin keeps them in a caption (measured:
// it removes white space it trims, not these), and nothing draws them.
constexpr char16_t kMarker = 0x2063;
constexpr std::array<char16_t, 4> kDigits{0x200B, 0x200C, 0x200D, 0x2060};
constexpr int kDigitCount = 16;

// Finds this process's windows by their tag, and reports where each is
// put - not during a drag, which is reported once, where it ends.
constexpr const char* kWatcher = R"JS(
const service = "%1";
const marker = String.fromCharCode(0x2063);
function tagOf(w) {
    const caption = w.captionNormal || "";
    const at = caption.indexOf(marker);
    return at < 0 ? "" : caption.substring(at);
}
function report(w) {
    const tag = tagOf(w);
    if (tag === "") return;
    const g = w.frameGeometry;
    callDBus(service, "%2", "org.remotewin.Placement", "moved",
             tag + " " + g.x + " " + g.y + " " + g.width + " " + g.height);
}
function watch(w) {
    if (w.pid !== %3) return;
    w.frameGeometryChanged.connect(function() { if (!w.move && !w.resize) report(w); });
    w.interactiveMoveResizeFinished.connect(function() { report(w); });
    w.captionChanged.connect(function() { report(w); });
    report(w);
}
workspace.windowList().forEach(watch);
workspace.windowAdded.connect(watch);
function outputs() {
    callDBus(service, "%2", "org.remotewin.Placement", "outputs",
             workspace.screens.map(function(s) {
                 return s.name + " " + s.geometry.x + " " + s.geometry.y + " " + s.devicePixelRatio;
             }).join(";"));
}
workspace.screensChanged.connect(outputs);
outputs();
)JS";

// Puts one window's frame corner somewhere, now or as KWin maps it.
constexpr const char* kPlacer = R"JS(
const tag = "%1";
function place(w) {
    if (w.pid !== %2) return false;
    const caption = w.captionNormal || "";
    if (caption.indexOf(tag) < 0) return false;
    w.frameGeometry = {x: %3, y: %4, width: %5, height: %6};
    const now = w.frameGeometry;
    callDBus("%7", "%8", "org.remotewin.Placement", "placed", tag + " " + now.x + " " + now.y);
    return true;
}
if (!workspace.windowList().some(place)) {
    const waiting = function(w) { if (place(w)) workspace.windowAdded.disconnect(waiting); };
    workspace.windowAdded.connect(waiting);
}
)JS";

// Sends one window to an output, now or as KWin maps it.
constexpr const char* kSender = R"JS(
const tag = "%1";
function send(w) {
    if (w.pid !== %2) return false;
    const caption = w.captionNormal || "";
    if (caption.indexOf(tag) < 0) return false;
    const output = workspace.screens.find(function(s) { return s.name === "%3"; });
    if (output && w.output !== output) workspace.sendClientToScreen(w, output);
    const now = w.frameGeometry;
    callDBus("%4", "%5", "org.remotewin.Placement", "placed", tag + " " + now.x + " " + now.y);
    return true;
}
if (!workspace.windowList().some(send)) {
    const waiting = function(w) { if (send(w)) workspace.windowAdded.disconnect(waiting); };
    workspace.windowAdded.connect(waiting);
}
)JS";

QString js_string(const QString& text) {
    QString out;
    for (const QChar c : text) out += QStringLiteral("\\u%1").arg(static_cast<unsigned>(c.unicode()), 4, 16, QLatin1Char('0'));
    return out;
}

QString number(double value) { return QString::number(value, 'g', 17); }

}  // namespace

KWinPlacement::KWinPlacement(QObject* parent) : QObject(parent), expiry_(new QTimer(this)) {
    if (!KWinScript::reachable()) {
        RWIN_INFO("placement", "not on KWin: toplevel windows are placed by the compositor");
        return;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QLatin1String(kPath), this, QDBusConnection::ExportScriptableSlots)) {
        RWIN_WARN("placement", "cannot put the placement object on the session bus: {}",
                  bus.lastError().message().toStdString());
        return;
    }
    service_ = bus.baseService();
    watcher_ = std::make_unique<KWinScript>(
        script_name(QStringLiteral("watch")),
        QString::fromLatin1(kWatcher).arg(service_, QLatin1String(kPath)).arg(QCoreApplication::applicationPid()));
    connect(expiry_, &QTimer::timeout, this, &KWinPlacement::expire);
    expiry_->start(kExpiryCheckMs);
    RWIN_INFO("placement", "placing toplevel windows through KWin ({})", active() ? "watching" : "not watching");
}

KWinPlacement::~KWinPlacement() {
    if (!service_.isEmpty()) QDBusConnection::sessionBus().unregisterObject(QLatin1String(kPath));
}

QString KWinPlacement::script_name(const QString& role) {
    return QStringLiteral("remotewin-%1-%2-%3").arg(QCoreApplication::applicationPid()).arg(role).arg(++scripts_);
}

QString KWinPlacement::tag_for(std::uint64_t window) {
    QString tag(1, QChar(kMarker));
    for (int digit = kDigitCount - 1; digit >= 0; --digit) {
        tag += QChar(kDigits[(window >> (digit * 2)) & 0x3]);
    }
    return tag;
}

std::optional<std::uint64_t> KWinPlacement::window_of(const QString& tag) {
    if (tag.size() != kDigitCount + 1 || tag.front() != QChar(kMarker)) return std::nullopt;
    std::uint64_t window = 0;
    for (qsizetype at = 1; at < tag.size(); ++at) {
        const auto digit = std::find(kDigits.begin(), kDigits.end(), tag[at].unicode());
        if (digit == kDigits.end()) return std::nullopt;
        window = (window << 2) | static_cast<std::uint64_t>(digit - kDigits.begin());
    }
    return window;
}

void KWinPlacement::place(std::uint64_t window, QRectF frame) {
    if (!active()) return;
    run_for(window, QString::fromLatin1(kPlacer)
                        .arg(js_string(tag_for(window)))
                        .arg(QCoreApplication::applicationPid())
                        .arg(number(frame.x()), number(frame.y()), number(frame.width()), number(frame.height()),
                             service_, QLatin1String(kPath)));
}

void KWinPlacement::send_to_output(std::uint64_t window, const QString& output) {
    if (!active()) return;
    run_for(window, QString::fromLatin1(kSender)
                        .arg(js_string(tag_for(window)))
                        .arg(QCoreApplication::applicationPid())
                        .arg(js_string(output), service_, QLatin1String(kPath)));
}

// One script per window at a time: a newer one replaces what an older one
// was still waiting to do.
void KWinPlacement::run_for(std::uint64_t window, const QString& source) {
    std::erase_if(pending_, [window](const Pending& pending) { return pending.window == window; });
    Pending pending;
    pending.window = window;
    pending.since.start();
    pending.script = std::make_unique<KWinScript>(script_name(QStringLiteral("place")), source);
    if (!pending.script->running()) {
        Q_EMIT window_not_placed(window);
        return;
    }
    pending_.push_back(std::move(pending));
}

void KWinPlacement::placed(const QString& report) {
    const QStringList parts = report.split(QLatin1Char(' '));
    const std::optional<std::uint64_t> window = parts.size() == 3 ? window_of(parts[0]) : std::nullopt;
    if (!window.has_value()) {
        RWIN_WARN("placement", "a placement report that names no window: '{}'", report.toStdString());
        return;
    }
    const auto pending = std::find_if(pending_.begin(), pending_.end(),
                                      [&](const Pending& entry) { return entry.window == *window; });
    const qint64 took = pending == pending_.end() ? -1 : pending->since.elapsed();
    if (pending != pending_.end()) pending_.erase(pending);
    const QPointF at(parts[1].toDouble(), parts[2].toDouble());
    RWIN_DEBUG("placement", "{:#x} placed at {:.2f},{:.2f} in {} ms", *window, at.x(), at.y(), took);
    Q_EMIT window_placed(*window, at, took);
}

void KWinPlacement::moved(const QString& report) {
    const QStringList parts = report.split(QLatin1Char(' '));
    const std::optional<std::uint64_t> window = parts.size() == 5 ? window_of(parts[0]) : std::nullopt;
    if (!window.has_value()) {
        RWIN_WARN("placement", "a move report that names no window: '{}'", report.toStdString());
        return;
    }
    const QRectF frame(parts[1].toDouble(), parts[2].toDouble(), parts[3].toDouble(), parts[4].toDouble());
    RWIN_TRACE("placement", "{:#x} is at {:.2f},{:.2f}", *window, frame.x(), frame.y());
    Q_EMIT window_moved(*window, frame);
}

// Qt has no word of an output's scale on Wayland beyond the whole number
// above it (2 for 1.2); KWin has the scale itself.
void KWinPlacement::outputs(const QString& report) {
    outputs_.clear();
    for (const QString& entry : report.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QStringList parts = entry.split(QLatin1Char(' '));
        if (parts.size() != 4) continue;
        outputs_.push_back(Output{parts[0], QPointF(parts[1].toDouble(), parts[2].toDouble()), parts[3].toDouble()});
        RWIN_INFO("placement", "output {} at {},{} scale {}", parts[0].toStdString(), parts[1].toStdString(),
                  parts[2].toStdString(), parts[3].toStdString());
    }
}

std::optional<ScreenMapping> KWinPlacement::output_named(const QString& name) const {
    const auto output = std::find_if(outputs_.begin(), outputs_.end(),
                                     [&](const Output& candidate) { return candidate.name == name; });
    if (output != outputs_.end()) return ScreenMapping{output->origin, output->scale};
    if (!outputs_.empty()) return ScreenMapping{outputs_.front().origin, outputs_.front().scale};
    return std::nullopt;
}

void KWinPlacement::expire() {
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (!it->since.hasExpired(kPlacementTimeoutMs)) {
            ++it;
            continue;
        }
        const std::uint64_t window = it->window;
        RWIN_WARN("placement", "{:#x} was not placed: KWin never had it", window);
        it = pending_.erase(it);
        Q_EMIT window_not_placed(window);
    }
}

}  // namespace rwin::host
