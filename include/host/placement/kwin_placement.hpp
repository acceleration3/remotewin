#pragma once

#include "host/placement/kwin_script.hpp"
#include "host/placement/screen_mapping.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class QTimer;

namespace rwin::host {

// Puts this process's toplevel windows where they are asked to be, and says
// where they are, on KWin - which, as any Wayland compositor, gives a
// client no way to do either itself.
//
// Through KWin's scripting: a script that stays loaded watches the
// process's windows and reports, over D-Bus to this object, every place one
// of them is put by anything but an interactive drag still under way; and
// a script per placement finds its window - now, or as KWin maps it, so a
// new window is never seen anywhere else - sets its frame, and says so. A
// script cannot be sent anything once it runs, so a placement is a script
// of its own; loading one is a D-Bus round trip of a few milliseconds.
//
// A window is known to the scripts by a suffix of its title (tag_for):
// characters that are not drawn, encoding the guest window's handle,
// which KWin keeps. Nothing else a Qt client sets on Wayland names one
// window to KWin: the application id and process are the whole program's.
class KWinPlacement : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.remotewin.Placement")
public:
    explicit KWinPlacement(QObject* parent = nullptr);
    ~KWinPlacement() override;

    KWinPlacement(const KWinPlacement&) = delete;
    KWinPlacement& operator=(const KWinPlacement&) = delete;
    KWinPlacement(KWinPlacement&&) = delete;
    KWinPlacement& operator=(KWinPlacement&&) = delete;

    // On KWin, with its scripting reachable and this object on the bus.
    [[nodiscard]] bool active() const noexcept { return watcher_ != nullptr && watcher_->running(); }

    [[nodiscard]] static QString tag_for(std::uint64_t window);
    [[nodiscard]] static std::optional<std::uint64_t> window_of(const QString& tag);

    // ``frame`` in the desktop's logical units. The size is set with the
    // place - KWin has no move alone - and is the size the window is to
    // be: as KWin maps a window it may not have its final size yet, and
    // a place set with that size is a resize the client then honours.
    void place(std::uint64_t window, QRectF frame);
    // Sends a window to the output of that name, keeping its state - a
    // maximised window is maximised there - now or as KWin maps it; said
    // done as a placement is (window_placed, where its frame then is).
    void send_to_output(std::uint64_t window, const QString& output);

    // The output of that name, as KWin lays it out: its corner and its
    // scale. The first output when none is named so; nothing before KWin
    // has said.
    [[nodiscard]] std::optional<ScreenMapping> output_named(const QString& name) const;

public Q_SLOTS:
    // Called by the scripts, over D-Bus: "<tag> <x> <y>" and
    // "<tag> <x> <y> <width> <height>". One string, because a script's
    // numbers cross D-Bus as whatever type each value happens to fit.
    Q_SCRIPTABLE void placed(const QString& report);
    Q_SCRIPTABLE void moved(const QString& report);
    // "<name> <x> <y> <scale>;..." for every output.
    Q_SCRIPTABLE void outputs(const QString& report);

Q_SIGNALS:
    void window_placed(std::uint64_t window, QPointF frame_corner, qint64 round_trip_ms);
    // KWin did not find the window in time.
    void window_not_placed(std::uint64_t window);
    void window_moved(std::uint64_t window, QRectF frame);

private:
    void run_for(std::uint64_t window, const QString& source);
    struct Output {
        QString name;
        QPointF origin;
        double scale = 1.0;
    };

    struct Pending {
        std::uint64_t window = 0;
        QElapsedTimer since;
        std::unique_ptr<KWinScript> script;
    };

    [[nodiscard]] QString script_name(const QString& role);
    void expire();

    QString service_;
    std::unique_ptr<KWinScript> watcher_;
    std::vector<Pending> pending_;
    std::vector<Output> outputs_;
    QTimer* expiry_;
    std::uint32_t scripts_ = 0;
};

}  // namespace rwin::host
