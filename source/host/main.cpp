#include "host/export/shared_frame_feed.hpp"
#include "host/link/guest_link.hpp"
#include "host/mirror/mirror_session.hpp"
#include "host/mirror/monitor_session.hpp"

#include "logging/logger.hpp"
#include "proto/wire_hello.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>

#include <cstdio>

namespace {

// The exit status when the guest half that dialled in is from another
// build (WireHello): whoever spawned this can tell the user to replace it
// rather than report a mirror that merely ended.
constexpr int kExitWireMismatch = 3;

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("remotewin"));
    QCoreApplication::setApplicationVersion(
        QStringLiteral("wire %1").arg(rwin::WireHello::kVersion));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Mirror a guest Windows application's windows, or the guest's whole "
                       "monitor.\nExits with status %1 when the guest half is from another "
                       "build.")
            .arg(kExitWireMismatch));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption port_option{QStringList{QStringLiteral("port")},
                                         QStringLiteral("Port the guest dials in to."),
                                         QStringLiteral("port"), QStringLiteral("5140")};
    const QCommandLineOption pid_option{
        QStringList{QStringLiteral("pid")},
        QStringLiteral("Guest process to mirror; 0 mirrors every window."),
        QStringLiteral("pid"), QStringLiteral("0")};
    const QCommandLineOption job_option{
        QStringList{QStringLiteral("job")},
        QStringLiteral("Job Object naming the launch to mirror. Survives a "
                       "launcher stub exiting, which a pid does not."),
        QStringLiteral("name")};
    const QCommandLineOption app_id_option{
        QStringList{QStringLiteral("app-id")},
        QStringLiteral("Desktop identity these windows present as."),
        QStringLiteral("app-id")};
    const QCommandLineOption desktop_option{
        QStringList{QStringLiteral("desktop")},
        QStringLiteral("Mirror the guest's whole primary monitor as one window, rather than "
                       "an application's windows. --pid and --job do not apply.")};
    const QCommandLineOption title_option{
        QStringList{QStringLiteral("title")},
        QStringLiteral("The title of the --desktop window."), QStringLiteral("title"),
        QStringLiteral("Guest desktop")};
    const QCommandLineOption export_option{
        QStringList{QStringLiteral("export-socket")},
        QStringLiteral("The GPU device's export socket (virtio-gpu-av-pci's export-socket=). With it, "
                       "and on the GPU the guest's surfaces are on, frames stay on the GPU; without "
                       "it they come as pixels. Defaults to $RWIN_EXPORT_SOCKET."),
        QStringLiteral("path")};
    parser.addOption(port_option);
    parser.addOption(export_option);
    parser.addOption(pid_option);
    parser.addOption(job_option);
    parser.addOption(app_id_option);
    parser.addOption(desktop_option);
    parser.addOption(title_option);
    parser.process(app);

    // Set before any window is created: the compositor reads it once, when
    // the first surface maps, and it is what groups this application's
    // windows together and apart from whatever spawned us.
    if (parser.isSet(app_id_option)) {
        QGuiApplication::setDesktopFileName(parser.value(app_id_option));
    }

    rwin::Logger::instance().opened("remotewin");
    RWIN_INFO("boot", "wire {}", rwin::WireHello::kVersion);
    rwin::host::GuestLink link;
    // Opened before the guest dials in: it is told at its hello which way
    // its frames are to come.
    rwin::host::SharedFrameFeed frames{link};
    const QString export_socket = parser.isSet(export_option)
                                      ? parser.value(export_option)
                                      : qEnvironmentVariable("RWIN_EXPORT_SOCKET");
    if (!export_socket.isEmpty()) {
        frames.open(export_socket.toStdString());
    } else {
        RWIN_INFO("boot", "no export socket: frames come as pixels");
    }
    const auto port = static_cast<std::uint16_t>(parser.value(port_option).toUShort());
    if (!link.listen(port)) {
        return EXIT_FAILURE;
    }

    // A guest of another build is disconnected as it is refused; the
    // disconnection is not what the exit says.
    bool mismatched = false;
    QObject::connect(&link, &rwin::host::GuestLink::mismatched, &app, [&] {
        mismatched = true;
        QCoreApplication::exit(kExitWireMismatch);
    });
    QObject::connect(&link, &rwin::host::GuestLink::disconnected, &app, [&] {
        if (!mismatched) QCoreApplication::quit();
    });

    if (parser.isSet(desktop_option)) {
        RWIN_INFO("boot", "mirroring the guest's monitor as '{}'",
                  parser.value(title_option).toStdString());
        rwin::host::MonitorSession session{link, frames, parser.value(title_option)};
        QObject::connect(&session, &rwin::host::MonitorSession::finished, &app,
                         &QCoreApplication::quit);
        return app.exec();
    }

    const auto process_id = static_cast<std::uint32_t>(parser.value(pid_option).toUInt());
    const QString job = parser.value(job_option);
    RWIN_INFO("boot", "mirroring pid {} job '{}'{}", process_id, job.toStdString(),
              (process_id == 0 && job.isEmpty()) ? " (the whole desktop)" : "");
    rwin::host::MirrorSession session{link, frames, process_id, job};
    QObject::connect(&session, &rwin::host::MirrorSession::finished, &app,
                     &QCoreApplication::quit);
    return app.exec();
}
