#include "conform/probe_link.hpp"

#include "conform/wait_until.hpp"
#include "logging/logger.hpp"
#include "proto/message_writer.hpp"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

namespace rwin::conform {

namespace {
// A capture of the whole guest screen is fifteen megabytes through QEMU's
// user networking; everything else is instant.
constexpr std::chrono::milliseconds kAnswerTimeout{30000};
}  // namespace

ProbeLink::ProbeLink(QObject* parent) : QObject(parent), server_(new QTcpServer(this)) {
    connect(server_, &QTcpServer::newConnection, this, &ProbeLink::accept_probe);
}

bool ProbeLink::listen() {
    // IPv4, as for the mirror: QEMU puts a guest's 10.0.2.2 on the host's
    // IPv4 loopback.
    if (!server_->listen(QHostAddress::AnyIPv4, 0)) {
        RWIN_ERROR("probe", "cannot listen: {}", server_->errorString().toStdString());
        return false;
    }
    return true;
}

std::uint16_t ProbeLink::port() const { return server_->serverPort(); }

void ProbeLink::accept_probe() {
    QTcpSocket* incoming = server_->nextPendingConnection();
    if (incoming == nullptr) return;
    if (probe_ != nullptr) {
        RWIN_WARN("probe", "a probe is already connected; refusing another");
        incoming->close();
        incoming->deleteLater();
        return;
    }
    probe_ = incoming;
    probe_->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(probe_, &QTcpSocket::readyRead, this, &ProbeLink::read_available);
    connect(probe_, &QTcpSocket::disconnected, this, [this] {
        RWIN_WARN("probe", "the probe went away");
        probe_->deleteLater();
        probe_ = nullptr;
        Q_EMIT lost();
    });
    RWIN_INFO("probe", "probe dialled in from {}", probe_->peerAddress().toString().toStdString());
}

void ProbeLink::read_available() {
    const QByteArray chunk = probe_->readAll();
    decoder_.feed(reinterpret_cast<const std::uint8_t*>(chunk.constData()),
                  static_cast<std::size_t>(chunk.size()));
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    while (decoder_.next(&payload, &size)) {
        MessageReader message{payload, size};
        if (message.id() == MessageId::ProbeHello) {
            hello_ = ProbeHello::read(message);
            continue;
        }
        // The late answer to a command already given up on: the probe
        // answers every command in order, and this one is not the
        // answer to the command now waiting.
        if (owed_ > 0) {
            --owed_;
            RWIN_WARN("probe", "a late answer (message {}) dropped", static_cast<int>(message.id()));
            continue;
        }
        answer_.assign(payload, payload + size);
        answered_ = true;
    }
    if (decoder_.overflowed()) {
        RWIN_ERROR("probe", "the probe's framing is broken; closing");
        probe_->close();
    }
}

std::optional<ProbeHello> ProbeLink::wait_for_probe(std::chrono::milliseconds timeout) {
    if (!wait_until([this] { return hello_.has_value(); }, timeout)) return std::nullopt;
    return hello_;
}

std::optional<MessageReader> ProbeLink::exchange(std::vector<std::uint8_t> command,
                                                 MessageId answer, QString& error) {
    if (probe_ == nullptr) {
        error = QStringLiteral("no probe is connected");
        return std::nullopt;
    }
    answered_ = false;
    probe_->write(reinterpret_cast<const char*>(command.data()),
                  static_cast<qint64>(command.size()));
    if (!wait_until([this] { return answered_ || probe_ == nullptr; }, kAnswerTimeout)
        || !answered_) {
        if (probe_ != nullptr) ++owed_;
        error = QStringLiteral("the probe did not answer in %1 s").arg(kAnswerTimeout.count() / 1000);
        return std::nullopt;
    }
    MessageReader reader{answer_.data(), answer_.size()};
    if (reader.id() != answer) {
        error = QStringLiteral("the probe answered with message %1, not %2")
                    .arg(static_cast<int>(reader.id()))
                    .arg(static_cast<int>(answer));
        return std::nullopt;
    }
    const bool ok = reader.u8() != 0;
    const std::string why = reader.string();
    if (!reader.ok()) {
        error = QStringLiteral("the probe's answer was cut short");
        return std::nullopt;
    }
    if (!ok) {
        error = QString::fromStdString(why);
        return std::nullopt;
    }
    return reader;
}

std::optional<std::uint64_t> ProbeLink::create_window(const WindowSpec& spec, QString& error) {
    MessageWriter out{MessageId::ProbeCreateWindow};
    spec.write(out);
    std::optional<MessageReader> answer = exchange(out.take(), MessageId::ProbeWindowCreated, error);
    if (!answer.has_value()) return std::nullopt;
    const std::uint64_t handle = answer->u64();
    if (!answer->ok()) {
        error = QStringLiteral("the probe's window handle was cut short");
        return std::nullopt;
    }
    return handle;
}

std::optional<WindowTruth> ProbeLink::observe(std::uint64_t window, QString& error) {
    MessageWriter out{MessageId::ProbeObserve};
    out.put_u64(window);
    std::optional<MessageReader> answer = exchange(out.take(), MessageId::ProbeTruth, error);
    if (!answer.has_value()) return std::nullopt;
    std::optional<WindowTruth> truth = WindowTruth::read(*answer);
    if (!truth.has_value()) error = QStringLiteral("the probe's truth was malformed");
    return truth;
}

std::optional<CursorTruth> ProbeLink::observe_cursor(QString& error) {
    MessageWriter out{MessageId::ProbeObserveCursor};
    std::optional<MessageReader> answer = exchange(out.take(), MessageId::ProbeCursorTruth, error);
    if (!answer.has_value()) return std::nullopt;
    std::optional<CursorTruth> truth = CursorTruth::read(*answer);
    if (!truth.has_value()) error = QStringLiteral("the probe's cursor truth was malformed");
    return truth;
}

std::optional<std::uint64_t> ProbeLink::act(const ProbeAction& action, QString& error) {
    MessageWriter out{MessageId::ProbeAct};
    action.write(out);
    std::optional<MessageReader> answer = exchange(out.take(), MessageId::ProbeActed, error);
    if (!answer.has_value()) return std::nullopt;
    const std::uint64_t made = answer->u64();
    if (!answer->ok()) {
        error = QStringLiteral("the probe's answer to an action was cut short");
        return std::nullopt;
    }
    return made;
}

std::optional<ScreenPixels> ProbeLink::capture(QRect area, QString& error) {
    MessageWriter out{MessageId::ProbeCaptureScreen};
    out.put_i32(area.x());
    out.put_i32(area.y());
    out.put_u32(static_cast<std::uint32_t>(area.width()));
    out.put_u32(static_cast<std::uint32_t>(area.height()));
    std::optional<MessageReader> answer = exchange(out.take(), MessageId::ProbeScreenPixels, error);
    if (!answer.has_value()) return std::nullopt;
    std::optional<ScreenPixels> pixels = ScreenPixels::read(*answer);
    if (!pixels.has_value()) error = QStringLiteral("the probe's screen pixels were malformed");
    return pixels;
}

bool ProbeLink::destroy_window(std::uint64_t window, QString& error) {
    MessageWriter out{MessageId::ProbeDestroyWindow};
    out.put_u64(window);
    return exchange(out.take(), MessageId::ProbeDone, error).has_value();
}

bool ProbeLink::destroy_all(QString& error) {
    MessageWriter out{MessageId::ProbeDestroyAll};
    return exchange(out.take(), MessageId::ProbeDone, error).has_value();
}

std::optional<std::vector<InputRecord>> ProbeLink::drain_input(QString& error) {
    MessageWriter out{MessageId::ProbeDrainInput};
    std::optional<MessageReader> answer = exchange(out.take(), MessageId::ProbeInput, error);
    if (!answer.has_value()) return std::nullopt;
    const std::uint32_t count = answer->u32();
    std::vector<InputRecord> records;
    records.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::optional<InputRecord> record = InputRecord::read(*answer);
        if (!record.has_value()) {
            error = QStringLiteral("the probe's input record %1 of %2 was malformed").arg(i).arg(count);
            return std::nullopt;
        }
        records.push_back(*record);
    }
    return records;
}

bool ProbeLink::set_backdrop(std::uint32_t rgb, QString& error) {
    MessageWriter out{MessageId::ProbeSetBackdrop};
    out.put_u32(rgb);
    return exchange(out.take(), MessageId::ProbeDone, error).has_value();
}

bool ProbeLink::quit(QString& error) {
    MessageWriter out{MessageId::ProbeQuit};
    return exchange(out.take(), MessageId::ProbeDone, error).has_value();
}

}  // namespace rwin::conform
