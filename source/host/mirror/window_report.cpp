#include "host/mirror/window_report.hpp"

#include "host/chrome/win32_style.hpp"
#include "host/link/guest_window.hpp"
#include "host/link/guest_windows.hpp"
#include "host/mirror/mirror_session.hpp"
#include "host/mirror/mirror_window.hpp"
#include "logging/logger.hpp"
#include "proto/window_fields.hpp"
#include "proto/window_id.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRect>
#include <QWidget>
#include <QWindow>

#include <format>

namespace rwin::host {

namespace {

constexpr const char* kPath = "/org/remotewin/Windows";

QString hex(std::uint64_t value) {
    return QStringLiteral("0x%1").arg(value, 0, 16);
}

// "0x30a2c:7", as the logs name it; null for none.
QJsonValue name_of(WindowId window) {
    if (window.none()) return QJsonValue::Null;
    return QString::fromStdString(std::format("{}", window));
}

QJsonArray rect(QRect area) { return QJsonArray{area.x(), area.y(), area.width(), area.height()}; }

QString show_name(WindowFields::Show show) {
    switch (show) {
        case WindowFields::Show::Hidden: return QStringLiteral("hidden");
        case WindowFields::Show::Minimized: return QStringLiteral("minimized");
        case WindowFields::Show::Maximized: return QStringLiteral("maximized");
        case WindowFields::Show::Shown: return QStringLiteral("shown");
    }
    return QStringLiteral("unknown");
}

QString window_type(Qt::WindowFlags flags) {
    switch (flags & Qt::WindowType_Mask) {
        case Qt::Popup: return QStringLiteral("popup");
        case Qt::ToolTip: return QStringLiteral("tooltip");
        case Qt::Tool: return QStringLiteral("tool");
        case Qt::Dialog: return QStringLiteral("dialog");
        case Qt::Window: return QStringLiteral("window");
        case Qt::Widget: return QStringLiteral("widget");
        default: return QStringLiteral("other");
    }
}

// The guest window a widget mirrors: the mirror itself or the one it is in.
QJsonValue handle_of(const QWidget* widget) {
    for (; widget != nullptr; widget = widget->parentWidget()) {
        if (const auto* mirror = qobject_cast<const MirrorWindow*>(widget)) return name_of(mirror->window());
    }
    return QJsonValue::Null;
}

QJsonObject guest_of(const GuestWindow& window, const MirrorSession& session) {
    const Win32Style style{window};
    const WindowFields& fields = window.fields();
    return {
        {QStringLiteral("owner"), name_of(fields.owner)},
        {QStringLiteral("anchor"), name_of(window.anchor())},
        {QStringLiteral("pid"), static_cast<qint64>(fields.app_id.process_id)},
        {QStringLiteral("aumid"), QString::fromStdString(fields.app_id.aumid)},
        {QStringLiteral("image"), QString::fromStdString(fields.app_id.image)},
        {QStringLiteral("belongs"), session.belongs(window.id())},
        {QStringLiteral("frame"), rect(window.frame())},
        {QStringLiteral("rect"), rect(QRect(window.picture_origin(), window.picture_size()))},
        {QStringLiteral("style"), hex(fields.style.style)},
        {QStringLiteral("ex_style"), hex(fields.style.extended)},
        {QStringLiteral("title"), window.title()},
        {QStringLiteral("class"), window.class_name()},
        {QStringLiteral("show"), show_name(fields.show)},
        {QStringLiteral("hidden"), window.hidden()},
        {QStringLiteral("minimized"), window.minimized()},
        {QStringLiteral("maximized"), window.maximized()},
        {QStringLiteral("active"), session.windows().order().active == window.id()},
        {QStringLiteral("band"), static_cast<qint64>(fields.band)},
        {QStringLiteral("in_taskbar"), style.in_taskbar()},
        {QStringLiteral("owned"), style.owned()},
        {QStringLiteral("stands_alone"), style.stands_alone()},
        {QStringLiteral("activates"), style.activates()},
        {QStringLiteral("topmost"), style.topmost()},
        {QStringLiteral("disabled"), style.disabled()},
        {QStringLiteral("tool_window"), style.tool_window()},
    };
}

QJsonObject mirror_of(const MirrorWindow& mirror, const MirrorState& state) {
    const QWindow* native = mirror.windowHandle();
    const QWindow* transient = native != nullptr ? native->transientParent() : nullptr;
    QJsonValue transient_handle = QJsonValue::Null;
    if (transient != nullptr) {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->windowHandle() == transient) transient_handle = handle_of(widget);
        }
    }
    return {
        {QStringLiteral("kind"), window_type(mirror.windowFlags())},
        {QStringLiteral("popup_of"), name_of(state.popup_of)},
        {QStringLiteral("attached_to"), name_of(state.attached_to)},
        {QStringLiteral("transient_parent"), transient_handle},
        {QStringLiteral("modal"), state.modal},
        {QStringLiteral("stays_on_top"), state.flags.testFlag(Qt::WindowStaysOnTopHint)},
        {QStringLiteral("frameless"), state.flags.testFlag(Qt::FramelessWindowHint)},
        {QStringLiteral("shows_without_activating"), state.shows_without_activating},
        {QStringLiteral("visible"), state.visible},
        {QStringLiteral("minimized"), state.minimized},
        {QStringLiteral("qt_minimized"), mirror.isMinimized()},
        {QStringLiteral("maximized"), state.maximized},
        {QStringLiteral("active"), state.active},
        {QStringLiteral("enabled"), mirror.isEnabled()},
        {QStringLiteral("forwards_input"), state.forwards_input},
        {QStringLiteral("transparent_for_mouse"), mirror.testAttribute(Qt::WA_TransparentForMouseEvents)},
        {QStringLiteral("title"), state.title},
        {QStringLiteral("position"), QJsonArray{state.position.x(), state.position.y()}},
        {QStringLiteral("placed"), QJsonArray{state.placed.x(), state.placed.y()}},
        {QStringLiteral("logical_size"), QJsonArray{state.logical_size.width(), state.logical_size.height()}},
        {QStringLiteral("device_pixel_ratio"), state.device_pixel_ratio},
        {QStringLiteral("guest_size"), QJsonArray{state.guest_size.width(), state.guest_size.height()}},
        {QStringLiteral("picture"), rect(state.picture)},
        {QStringLiteral("frames_presented"), static_cast<qint64>(state.frames_presented)},
    };
}

}  // namespace

WindowReport::WindowReport(const MirrorSession& session, QObject* parent)
    : QObject(parent), session_(session) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QLatin1String(kPath), this, QDBusConnection::ExportScriptableSlots)) {
        RWIN_WARN("report", "cannot put the window report on the session bus: {}",
                  bus.lastError().message().toStdString());
        return;
    }
    registered_ = true;
    RWIN_INFO("report", "window report at {} {}", bus.baseService().toStdString(), kPath);
}

WindowReport::~WindowReport() {
    if (registered_) QDBusConnection::sessionBus().unregisterObject(QLatin1String(kPath));
}

QString WindowReport::Describe() const {
    QJsonArray windows;
    for (const GuestWindow* window : session_.windows().stacked()) {
        QJsonObject entry{{QStringLiteral("handle"), name_of(window->id())},
                          {QStringLiteral("guest"), guest_of(*window, session_)}};
        const MirrorWindow* mirror = session_.find(window->id());
        const std::optional<MirrorState> state = session_.state_of(window->id());
        entry.insert(QStringLiteral("mirror"),
                     mirror != nullptr && state ? QJsonValue(mirror_of(*mirror, *state)) : QJsonValue::Null);
        windows.push_back(entry);
    }
    // Mirrors of windows the guest no longer has: what is left over.
    QJsonArray strays;
    for (QWidget* widget : QApplication::allWidgets()) {
        const auto* mirror = qobject_cast<const MirrorWindow*>(widget);
        if (mirror == nullptr || session_.windows().find(mirror->window()) != nullptr) continue;
        QJsonObject stray{{QStringLiteral("handle"), name_of(mirror->window())},
                          {QStringLiteral("visible"), mirror->isVisible()},
                          {QStringLiteral("kind"), window_type(mirror->windowFlags())}};
        strays.push_back(stray);
    }
    const QJsonObject report{
        {QStringLiteral("host_pid"), QCoreApplication::applicationPid()},
        {QStringLiteral("app_id"), QGuiApplication::desktopFileName()},
        {QStringLiteral("windows"), windows},
        {QStringLiteral("strays"), strays},
        {QStringLiteral("active_popup"), handle_of(QApplication::activePopupWidget())},
        {QStringLiteral("mouse_grabber"), handle_of(QWidget::mouseGrabber())},
        {QStringLiteral("keyboard_grabber"), handle_of(QWidget::keyboardGrabber())},
        {QStringLiteral("active_window"), handle_of(QApplication::activeWindow())},
        {QStringLiteral("focus_widget"), handle_of(QApplication::focusWidget())},
    };
    return QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Indented));
}

}  // namespace rwin::host
