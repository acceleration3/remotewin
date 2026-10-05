#include "host/link/parent_control.hpp"

#include "logging/logger.hpp"

#include <cerrno>
#include <unistd.h>

namespace rwin::host {

ParentControl::ParentControl(int command_fd, int status_fd, QObject* parent)
    : QObject(parent), commands_(command_fd, QSocketNotifier::Read, this) {
    if (!status_.open(status_fd, QIODevice::WriteOnly, QFileDevice::DontCloseHandle)) {
        RWIN_ERROR("parent", "cannot open status pipe: {}", status_.errorString().toStdString());
    }
    connect(&commands_, &QSocketNotifier::activated, this, &ParentControl::read_commands);
}

void ParentControl::report_listening(std::uint16_t port) {
    report("LISTENING " + QByteArray::number(port) + '\n');
}

void ParentControl::report_ready() {
    report("READY\n");
}

void ParentControl::report(const QByteArray& status) {
    if (status_.write(status) == status.size() && status_.flush())
        return;
    RWIN_ERROR("parent", "cannot write status pipe: {}", status_.errorString().toStdString());
    Q_EMIT failed();
}

void ParentControl::read_commands() {
    char bytes[128];
    const ssize_t count = ::read(static_cast<int>(commands_.socket()), bytes, sizeof(bytes));
    if (count <= 0) {
        if (count < 0 && (errno == EINTR || errno == EAGAIN))
            return;
        commands_.setEnabled(false);
        if (count == 0)
            Q_EMIT closed();
        else
            Q_EMIT failed();
        return;
    }
    pending_.append(bytes, count);
    while (pending_.contains('\n')) {
        const auto line = pending_.first(pending_.indexOf('\n'));
        pending_.remove(0, line.size() + 1);
        if (line == "RAISE")
            Q_EMIT raise_requested();
    }
    if (pending_.size() > 1024) {
        RWIN_ERROR("parent", "command exceeds 1024 bytes");
        commands_.setEnabled(false);
        Q_EMIT failed();
    }
}

}  // namespace rwin::host
