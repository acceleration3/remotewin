#include "host/link/guest_link.hpp"
#include "host/mirror/mirror_session.hpp"

#include "logging/logger.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>

#include <cstdio>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("remotewin"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Mirror a guest Windows application's windows."));
    parser.addHelpOption();
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
    parser.addOption(port_option);
    parser.addOption(pid_option);
    parser.addOption(job_option);
    parser.addOption(app_id_option);
    parser.process(app);

    // Set before any window is created: the compositor reads it once, when
    // the first surface maps, and it is what groups this application's
    // windows together and apart from whatever spawned us.
    if (parser.isSet(app_id_option)) {
        QGuiApplication::setDesktopFileName(parser.value(app_id_option));
    }

    rwin::Logger::instance().opened("remotewin");
    rwin::host::GuestLink link;
    const auto port = static_cast<std::uint16_t>(parser.value(port_option).toUShort());
    if (!link.listen(port)) {
        return EXIT_FAILURE;
    }

    const auto process_id = static_cast<std::uint32_t>(parser.value(pid_option).toUInt());
    const QString job = parser.value(job_option);
    RWIN_INFO("boot", "mirroring pid {} job '{}'{}", process_id, job.toStdString(),
              (process_id == 0 && job.isEmpty()) ? " (the whole desktop)" : "");
    rwin::host::MirrorSession session{link, process_id, job};
    QObject::connect(&session, &rwin::host::MirrorSession::finished, &app,
                     &QCoreApplication::quit);
    QObject::connect(&link, &rwin::host::GuestLink::disconnected, &app,
                     &QCoreApplication::quit);
    return app.exec();
}
