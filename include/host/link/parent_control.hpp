#pragma once

#include <QByteArray>
#include <QFile>
#include <QObject>
#include <QSocketNotifier>
#include <cstdint>

namespace rwin::host {

// File descriptors are borrowed from the spawning application's pipes.
class ParentControl : public QObject {
    Q_OBJECT
public:
    ParentControl(int command_fd, int status_fd, QObject* parent = nullptr);
    void report_listening(std::uint16_t port);
    void report_ready();

Q_SIGNALS:
    void raise_requested();
    void closed();
    void failed();

private:
    void read_commands();
    void report(const QByteArray& status);

    QSocketNotifier commands_;
    QFile status_;
    QByteArray pending_;
};

}  // namespace rwin::host
