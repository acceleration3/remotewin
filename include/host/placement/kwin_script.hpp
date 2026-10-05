#pragma once

#include <QString>
#include <QTemporaryFile>

namespace rwin::host {

// A script run inside KWin, through its scripting interface on the session
// bus (org.kde.KWin /Scripting): loaded from a file under a name of its
// own, run, and unloaded when this goes - along with every signal it
// connected to inside KWin.
class KWinScript {
public:
    KWinScript(QString name, const QString& source);
    ~KWinScript();

    KWinScript(const KWinScript&) = delete;
    KWinScript& operator=(const KWinScript&) = delete;
    KWinScript(KWinScript&&) = delete;
    KWinScript& operator=(KWinScript&&) = delete;

    // Whether KWin loaded and ran it; ``error`` says why not.
    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] const QString& error() const noexcept { return error_; }

    // KWin's scripting is on the session bus: this desktop is KWin's.
    [[nodiscard]] static bool reachable();

private:
    [[nodiscard]] bool load_and_run(const QString& source);

    QString name_;
    // KWin reads the file when the script runs, not when it is loaded.
    QTemporaryFile file_;
    bool loaded_ = false;
    bool running_ = false;
    QString error_;
};

}  // namespace rwin::host
