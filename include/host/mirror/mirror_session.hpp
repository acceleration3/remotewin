#pragma once

#include "host/cursor/emulated_cursor.hpp"
#include "host/link/guest_window_info.hpp"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QPointer>

#include <cstdint>

class QTimer;

namespace rwin::host {

class GuestLink;
class MirrorWindow;

// The windows of one guest application, mirrored for as long as it runs.
//
// Scoped to a process because that is the unit a user thinks in: one
// application's windows belong together, share a taskbar identity, and
// end together. When the last one closes there is nothing left to show
// and the session says so.
class MirrorSession : public QObject {
    Q_OBJECT
public:
    MirrorSession(GuestLink& link, std::uint32_t process_id, QString job_name,
                  QObject* parent = nullptr);

Q_SIGNALS:
    // No windows left, and the guest had some. The process is done.
    void finished();

private:
    void refresh(const QList<GuestWindowInfo>& windows);
    void reposition(const GuestWindowInfo& window,
                    const QList<GuestWindowInfo>& all);
    void hand_over_drag(quint64 window);
    void show_cursor(quint64 id);
    void show_frame(quint64 window, const QImage& pixels);
    void forget(quint64 window);

    GuestLink& link_;
    std::uint32_t process_id_;
    QString job_name_;
    QTimer* poll_;
    QHash<quint64, QPointer<MirrorWindow>> mirrors_;
    EmulatedCursor cursor_;
    bool saw_any_ = false;
};

}  // namespace rwin::host
