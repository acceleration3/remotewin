#include "host/placement/kwin_script.hpp"

#include "logging/logger.hpp"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>

namespace rwin::host {

namespace {

constexpr const char* kService = "org.kde.KWin";
constexpr const char* kScripting = "/Scripting";
constexpr const char* kScriptingInterface = "org.kde.kwin.Scripting";
constexpr const char* kScriptInterface = "org.kde.kwin.Script";

QDBusMessage call(const QString& path, const char* interface, const char* method, const QVariantList& arguments) {
    QDBusMessage message = QDBusMessage::createMethodCall(QLatin1String(kService), path,
                                                          QLatin1String(interface), QLatin1String(method));
    message.setArguments(arguments);
    return QDBusConnection::sessionBus().call(message);
}

}  // namespace

KWinScript::KWinScript(QString name, const QString& source) : name_(std::move(name)) {
    running_ = load_and_run(source);
    if (!running_) RWIN_WARN("placement", "script {} did not run: {}", name_.toStdString(), error_.toStdString());
}

KWinScript::~KWinScript() {
    if (!loaded_) return;
    const QDBusMessage answer = call(QLatin1String(kScripting), kScriptingInterface, "unloadScript", {name_});
    if (answer.type() == QDBusMessage::ErrorMessage) {
        RWIN_WARN("placement", "script {} could not be unloaded: {}", name_.toStdString(),
                  answer.errorMessage().toStdString());
    }
}

bool KWinScript::reachable() {
    const QDBusConnection bus = QDBusConnection::sessionBus();
    return bus.isConnected() && bus.interface() != nullptr
           && bus.interface()->isServiceRegistered(QLatin1String(kService)).value();
}

bool KWinScript::load_and_run(const QString& source) {
    file_.setFileTemplate(QDir::tempPath() + QStringLiteral("/remotewin-XXXXXX.js"));
    if (!file_.open() || file_.write(source.toUtf8()) < 0 || !file_.flush()) {
        error_ = QStringLiteral("cannot write %1: %2").arg(file_.fileName(), file_.errorString());
        return false;
    }
    const QDBusMessage loaded =
        call(QLatin1String(kScripting), kScriptingInterface, "loadScript", {file_.fileName(), name_});
    if (loaded.type() == QDBusMessage::ErrorMessage || loaded.arguments().isEmpty()) {
        error_ = QStringLiteral("loadScript: %1").arg(loaded.errorMessage());
        return false;
    }
    const int id = loaded.arguments().constFirst().toInt();
    loaded_ = true;
    if (id < 0) {
        error_ = QStringLiteral("loadScript refused %1 (already loaded?)").arg(name_);
        return false;
    }
    const QDBusMessage ran = call(QStringLiteral("/Scripting/Script%1").arg(id), kScriptInterface, "run", {});
    if (ran.type() == QDBusMessage::ErrorMessage) {
        error_ = QStringLiteral("run: %1").arg(ran.errorMessage());
        return false;
    }
    return true;
}

}  // namespace rwin::host
