#pragma once

#include <QObject>
#include <QString>

namespace rwin::host {

class MirrorSession;

// What this mirror holds, for a person or a tool comparing it with the
// guest and the compositor: every window the guest has told of, in its
// z-order, whether it is the application's, and each mirror's own idea of
// the window beside it - the
// kind of window it asked for, its transient parent, the state it believes
// it is in - with what the toolkit says holds the pointer and keyboard.
// On the session bus at /org/remotewin/Windows of this process's own
// connection; tools/window_states.py reads it from every mirror running.
class WindowReport : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.remotewin.Windows")
public:
    explicit WindowReport(const MirrorSession& session, QObject* parent = nullptr);
    ~WindowReport() override;

    WindowReport(const WindowReport&) = delete;
    WindowReport& operator=(const WindowReport&) = delete;
    WindowReport(WindowReport&&) = delete;
    WindowReport& operator=(WindowReport&&) = delete;

public Q_SLOTS:
    // The report, as JSON.
    Q_SCRIPTABLE QString Describe() const;

private:
    const MirrorSession& session_;
    bool registered_ = false;
};

}  // namespace rwin::host
