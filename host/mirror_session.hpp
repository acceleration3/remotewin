#pragma once

#include "host/guest_window_info.hpp"

#include <QHash>
#include <QObject>
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
    MirrorSession(GuestLink& link, std::uint32_t process_id, QObject* parent = nullptr);

Q_SIGNALS:
    // No windows left, and the guest had some. The process is done.
    void finished();

private:
    void refresh(const QList<GuestWindowInfo>& windows);
    void show_frame(quint64 window, const QImage& pixels);
    void forget(quint64 window);

    GuestLink& link_;
    std::uint32_t process_id_;
    QTimer* poll_;
    QHash<quint64, QPointer<MirrorWindow>> mirrors_;
    bool saw_any_ = false;
};

}  // namespace rwin::host
