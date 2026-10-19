#include "conform/guest_agent.hpp"
#include "conform/probe_link.hpp"
#include "conform/run_report.hpp"
#include "conform/scenario_book.hpp"
#include "conform/scenario_runner.hpp"
#include "conform/wait_until.hpp"
#include "host/export/shared_frame_feed.hpp"
#include "host/link/guest_link.hpp"
#include "host/mirror/mirror_scope.hpp"
#include "host/mirror/mirror_session.hpp"
#include "logging/logger.hpp"

#include <QApplication>
#include <QImage>
#include <QRegion>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpServer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace {

using namespace std::chrono_literals;

// QEMU's user networking puts the host here, as seen from the guest.
constexpr const char* kHostFromGuest = "10.0.2.2";
constexpr auto kProbeTimeout = 30s;
// The guest half dials in once its Direct3D device is made, which on a
// virtual GPU has taken most of a minute.
constexpr auto kMirrorTimeout = 90s;

std::uint16_t free_port() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::AnyIPv4, 0)) return 0;
    return probe.serverPort();
}

struct Options {
    QString scenarios;
    QString filter;
    QString out;
    QString agent_host;
    std::uint16_t agent_port = 0;
    QString probe_exe;
    QString probe_log;
    QString guest_exe;
    QString guest_log;
    bool launch = true;
    int tolerance = 0;
};

std::optional<Options> parse(QApplication& app) {
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Make Windows windows in the guest, mirror them, and compare the mirror with the truth."));
    parser.addHelpOption();
    const QCommandLineOption scenarios{QStringLiteral("scenarios"), QStringLiteral("Scenario file."),
                                       QStringLiteral("json"), QStringLiteral(RWIN_CONFORM_SCENARIOS)};
    const QCommandLineOption filter{QStringLiteral("filter"),
                                    QStringLiteral("Run only scenarios whose name matches."),
                                    QStringLiteral("regex"), QStringLiteral(".")};
    const QCommandLineOption out{QStringLiteral("out"), QStringLiteral("Report folder."), QStringLiteral("dir"),
                                 QDir::current().filePath(QStringLiteral("conform-")
                                                          + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")))};
    const QCommandLineOption agent{QStringLiteral("agent"), QStringLiteral("LSW guest agent."),
                                   QStringLiteral("host:port"), QStringLiteral("127.0.0.1:5102")};
    const QCommandLineOption probe_exe{QStringLiteral("probe-exe"), QStringLiteral("The probe, as the guest names it."),
                                       QStringLiteral("path"),
                                       QStringLiteral("C:\\ProgramData\\LSW\\remotewin-probe.exe")};
    const QCommandLineOption probe_log{QStringLiteral("probe-log"), QStringLiteral("Where the probe logs, in the guest."),
                                       QStringLiteral("path"),
                                       QStringLiteral("C:\\ProgramData\\LSW\\remotewin-probe.log")};
    const QCommandLineOption guest_exe{QStringLiteral("guest-exe"),
                                       QStringLiteral("The mirror's guest half, as the guest names it; the "
                                                      "agent's own when not given."),
                                       QStringLiteral("path")};
    const QCommandLineOption guest_log{QStringLiteral("guest-log"),
                                       QStringLiteral("Where the guest half logs, in the guest."),
                                       QStringLiteral("path")};
    const QCommandLineOption no_launch{QStringLiteral("no-launch"),
                                       QStringLiteral("Do not launch the probe; wait for one started by hand "
                                                      "(the port is logged).")};
    const QCommandLineOption tolerance{QStringLiteral("tolerance"),
                                       QStringLiteral("Largest channel difference that still matches."),
                                       QStringLiteral("0-255"), QStringLiteral("8")};
    parser.addOptions({scenarios, filter, out, agent, probe_exe, probe_log, guest_exe, guest_log, no_launch, tolerance});
    parser.process(app);

    Options options;
    options.scenarios = parser.value(scenarios);
    options.filter = parser.value(filter);
    options.out = parser.value(out);
    const QStringList endpoint = parser.value(agent).split(QLatin1Char(':'));
    if (endpoint.size() != 2) {
        RWIN_ERROR("conform", "--agent wants host:port");
        return std::nullopt;
    }
    options.agent_host = endpoint[0];
    options.agent_port = static_cast<std::uint16_t>(endpoint[1].toUShort());
    options.probe_exe = parser.value(probe_exe);
    options.probe_log = parser.value(probe_log);
    options.guest_exe = parser.value(guest_exe);
    options.guest_log = parser.value(guest_log);
    options.launch = !parser.isSet(no_launch);
    options.tolerance = parser.value(tolerance).toInt();
    return options;
}

// The probe, started through the agent unless one is to be started by
// hand, and its hello once it has dialled in.
std::optional<rwin::ProbeHello> bring_up_probe(rwin::conform::GuestAgent& agent,
                                               rwin::conform::ProbeLink& probe,
                                               const Options& options) {
    if (options.launch) {
        const QString arguments = QStringLiteral("--harness %1:%2 --log \"%3\"")
                                      .arg(QLatin1String(kHostFromGuest))
                                      .arg(probe.port())
                                      .arg(options.probe_log);
        QString error;
        if (!agent.launch(options.probe_exe, arguments, error).has_value()) {
            RWIN_ERROR("conform", "the probe did not start: {}", error.toStdString());
            return std::nullopt;
        }
    }
    const std::optional<rwin::ProbeHello> hello = probe.wait_for_probe(kProbeTimeout);
    if (!hello.has_value()) {
        RWIN_ERROR("conform", "no probe dialled in within {} s", kProbeTimeout.count());
        return std::nullopt;
    }
    RWIN_INFO("conform", "probe pid {} at {} dpi, screen {}x{}, work area {}x{}, drop shadows {}",
              hello->process_id, hello->dpi, hello->screen_width, hello->screen_height,
              hello->work_area_width, hello->work_area_height, hello->drop_shadows ? "on" : "off");
    return hello;
}

// The mirror's guest half, started in the probe's session, dialled in.
bool bring_up_mirror(rwin::conform::GuestAgent& agent, rwin::host::GuestLink& link,
                     const rwin::conform::GuestAgent::MirrorStart& start) {
    bool connected = false;
    const QMetaObject::Connection watching =
        QObject::connect(&link, &rwin::host::GuestLink::connected, [&] { connected = true; });
    QString error;
    const bool started = agent.start_window_mirror(start, error).has_value();
    if (!started) RWIN_ERROR("conform", "the mirror's guest half did not start: {}", error.toStdString());
    const bool dialled = started && rwin::conform::wait_until([&] { return connected; }, kMirrorTimeout);
    QObject::disconnect(watching);
    if (started && !dialled) RWIN_ERROR("conform", "the mirror's guest half did not dial in");
    return dialled;
}

// The work area with nothing of the probe's on it, only the backdrop: any
// pixel not the backdrop's colour is the shell drawing over every window.
// Grown by two pixels, since a soft edge that matched the backdrop may not
// match a window's colour.
QRegion shell_overlay(rwin::conform::ProbeLink& probe, QRect work_area, QColor backdrop, int tolerance) {
    constexpr int kGrow = 2;
    QString error;
    const std::optional<rwin::ScreenPixels> pixels = probe.capture(work_area, error);
    if (!pixels.has_value()) {
        RWIN_WARN("conform", "the bare screen could not be read: {}", error.toStdString());
        return {};
    }
    const QImage screen(pixels->bgra.data(), static_cast<int>(pixels->width), static_cast<int>(pixels->height),
                        static_cast<int>(pixels->width * 4), QImage::Format_RGB32);
    // Not an exact match: DWM composes the backdrop a level or two off
    // its colour in places.
    QRegion found;
    for (int y = 0; y < screen.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(screen.constScanLine(y));
        int run = -1;
        for (int x = 0; x <= screen.width(); ++x) {
            const bool differs = x < screen.width()
                                 && std::max({std::abs(qRed(row[x]) - backdrop.red()),
                                              std::abs(qGreen(row[x]) - backdrop.green()),
                                              std::abs(qBlue(row[x]) - backdrop.blue())})
                                        > tolerance;
            if (differs && run < 0) run = x;
            if (!differs && run >= 0) {
                found += QRect(run, y, x - run, 1);
                run = -1;
            }
        }
    }
    QRegion grown;
    for (int dy = -kGrow; dy <= kGrow; ++dy) {
        for (int dx = -kGrow; dx <= kGrow; ++dx) grown |= found.translated(dx, dy);
    }
    grown.translate(work_area.topLeft());
    if (!grown.isEmpty()) {
        const QRect box = grown.boundingRect();
        RWIN_INFO("conform", "the guest's shell draws over windows at {},{} {}x{}; not compared", box.x(), box.y(),
                  box.width(), box.height());
    }
    return grown;
}

QJsonObject run_description(const Options& options, const rwin::ProbeHello& hello, QColor backdrop) {
    return QJsonObject{{QStringLiteral("scenarios"), options.scenarios},
                       {QStringLiteral("filter"), options.filter},
                       {QStringLiteral("guest dpi"), static_cast<int>(hello.dpi)},
                       {QStringLiteral("guest screen"),
                        QStringLiteral("%1x%2").arg(hello.screen_width).arg(hello.screen_height)},
                       {QStringLiteral("guest drop shadows"), hello.drop_shadows},
                       {QStringLiteral("backdrop"), backdrop.name()},
                       {QStringLiteral("tolerance"), options.tolerance}};
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("remotewin-conform"));
    rwin::Logger::instance().opened("remotewin-conform");
    const std::optional<Options> options = parse(app);
    if (!options.has_value()) return EXIT_FAILURE;

    QString error;
    const std::optional<rwin::conform::ScenarioBook> book =
        rwin::conform::ScenarioBook::read(options->scenarios, error);
    if (!book.has_value()) {
        RWIN_ERROR("conform", "{}", error.toStdString());
        return EXIT_FAILURE;
    }
    const QRegularExpression filter{options->filter};

    rwin::conform::ProbeLink probe;
    rwin::host::GuestLink link;
    const std::uint16_t mirror_port = free_port();
    if (!probe.listen() || mirror_port == 0 || !link.listen(mirror_port)) return EXIT_FAILURE;
    RWIN_INFO("conform", "probe port {}, mirror port {}", probe.port(), mirror_port);

    rwin::conform::GuestAgent agent{options->agent_host, options->agent_port};
    const std::optional<rwin::ProbeHello> hello = bring_up_probe(agent, probe, *options);
    if (!hello.has_value()) return EXIT_FAILURE;
    const QColor backdrop = book->backdrop();
    if (!probe.set_backdrop(static_cast<std::uint32_t>(backdrop.rgb() & 0xFFFFFF), error)) {
        RWIN_ERROR("conform", "no backdrop: {}", error.toStdString());
        return EXIT_FAILURE;
    }
    // Exactly the probe's process: its backdrop is another, and is not
    // mirrored.
    // Never opened: the harness compares pixels it reads off the mirror,
    // and the frames it judges come as pixels.
    rwin::host::SharedFrameFeed frames{link};
    rwin::host::MirrorSession mirrors{
        link, frames, rwin::host::MirrorScope{rwin::host::MirrorScope::Launch{hello->process_id, {}, {}, false}}};
    if (!bring_up_mirror(agent, link, {mirror_port, hello->process_id, options->guest_exe, options->guest_log})) {
        return EXIT_FAILURE;
    }
    bool lost = false;
    QObject::connect(&link, &rwin::host::GuestLink::disconnected, [&] { lost = true; });
    QObject::connect(&probe, &rwin::conform::ProbeLink::lost, [&] { lost = true; });

    const QRect work_area(0, 0, static_cast<int>(hello->work_area_width),
                          static_cast<int>(hello->work_area_height));
    const QRegion overlay = shell_overlay(probe, work_area, backdrop, options->tolerance);
    QJsonObject description = run_description(*options, *hello, backdrop);
    const QRect overlay_box = overlay.boundingRect();
    description.insert(QStringLiteral("guest shell overlay (not compared)"),
                       overlay.isEmpty() ? QStringLiteral("none")
                                         : QStringLiteral("%1,%2 %3x%4").arg(overlay_box.x()).arg(overlay_box.y())
                                               .arg(overlay_box.width()).arg(overlay_box.height()));
    rwin::conform::RunReport report{options->out, description};
    if (!report.open(error)) {
        RWIN_ERROR("conform", "{}", error.toStdString());
        return EXIT_FAILURE;
    }
    rwin::conform::ScenarioRunner runner{probe, mirrors,
                                         {backdrop, work_area, options->tolerance, hello->drop_shadows, overlay}};
    for (const rwin::conform::Scenario& scenario : book->scenarios()) {
        if (lost) break;
        if (!filter.match(scenario.name).hasMatch()) continue;
        const rwin::conform::ScenarioOutcome outcome = runner.run(scenario);
        RWIN_INFO("conform", "{}: {} {}", scenario.name.toStdString(), outcome.passed() ? "pass" : "FAIL",
                  outcome.failures().join(QStringLiteral(", ")).toStdString());
        if (!report.add(outcome, error)) RWIN_ERROR("conform", "{}", error.toStdString());
    }
    if (lost) RWIN_ERROR("conform", "the probe or the mirror went away mid-run");
    if (!probe.quit(error)) RWIN_WARN("conform", "the probe did not quit: {}", error.toStdString());
    if (!report.finish(error)) {
        RWIN_ERROR("conform", "{}", error.toStdString());
        return EXIT_FAILURE;
    }
    RWIN_INFO("conform", "{} of {} failed; report at {}", report.failed(), report.total(),
              QDir(options->out).filePath(QStringLiteral("report.html")).toStdString());
    return report.failed() == 0 && !lost ? EXIT_SUCCESS : EXIT_FAILURE;
}
