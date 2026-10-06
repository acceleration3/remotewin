#include "host/mirror/window_report.hpp"

#include "host/chrome/win32_style.hpp"
#include "host/mirror/mirror_session.hpp"
#include "host/mirror/mirror_window.hpp"
#include "logging/logger.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWidget>
#include <QWindow>

namespace rwin::host {

namespace {

constexpr const char* kPath = "/org/remotewin/Windows";

QString hex(std::uint64_t value) {
    return QStringLiteral("0x%1").arg(value, 0, 16);
}

QJsonArray rect(int x, int y, int width, int height) {
    return QJsonArray{x, y, width, height};
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
        if (const auto* mirror = qobject_cast<const MirrorWindow*>(widget)) return hex(mirror->handle());
    }
    return QJsonValue::Null;
}

QJsonObject guest_of(const GuestWindowInfo& window) {
    const Win32Style style{window};
    return {
        {QStringLiteral("owner"), hex(window.owner)},
        {QStringLiteral("pid"), static_cast<qint64>(window.process_id)},
        {QStringLiteral("rect"), rect(window.x, window.y, static_cast<int>(window.width),
                                       static_cast<int>(window.height))},
        {QStringLiteral("style"), hex(window.style)},
        {QStringLiteral("ex_style"), hex(window.extended_style)},
        {QStringLiteral("title"), window.title},
        {QStringLiteral("class"), window.class_name},
        {QStringLiteral("minimized"), window.minimized},
        {QStringLiteral("maximized"), window.maximized},
        {QStringLiteral("foreground"), window.foreground},
        {QStringLiteral("band"), static_cast<qint64>(window.band)},
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
        {QStringLiteral("popup_of"), state.popup_of == 0 ? QJsonValue::Null : QJsonValue(hex(state.popup_of))},
        {QStringLiteral("attached_to"),
         state.attached_to == 0 ? QJsonValue::Null : QJsonValue(hex(state.attached_to))},
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
        {QStringLiteral("picture"), rect(state.picture.x(), state.picture.y(), state.picture.width(),
                                          state.picture.height())},
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
    QList<std::uint64_t> listed;
    for (const GuestWindowInfo& window : session_.listed()) {
        listed.push_back(window.handle);
        QJsonObject entry{{QStringLiteral("handle"), hex(window.handle)},
                          {QStringLiteral("guest"), guest_of(window)}};
        const MirrorWindow* mirror = session_.find(window.handle);
        const std::optional<MirrorState> state = session_.state_of(window.handle);
        entry.insert(QStringLiteral("mirror"),
                     mirror != nullptr && state ? QJsonValue(mirror_of(*mirror, *state)) : QJsonValue::Null);
        windows.push_back(entry);
    }
    // Mirrors of windows the guest no longer lists: what is left over.
    QJsonArray strays;
    for (QWidget* widget : QApplication::allWidgets()) {
        const auto* mirror = qobject_cast<const MirrorWindow*>(widget);
        if (mirror == nullptr || listed.contains(mirror->handle())) continue;
        QJsonObject stray{{QStringLiteral("handle"), hex(mirror->handle())},
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
