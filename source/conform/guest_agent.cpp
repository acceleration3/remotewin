#include "conform/guest_agent.hpp"

#include <QJsonDocument>
#include <QJsonValue>

#include <utility>

namespace rwin::conform {

namespace {

constexpr std::uint8_t kControlFrame = 1;
constexpr int kHeaderSize = 5;
constexpr int kConnectTimeoutMs = 5000;
// Starting a process in the guest goes through a service, a token and a
// session; seconds, not milliseconds.
constexpr int kAnswerTimeoutMs = 30000;

}  // namespace

GuestAgent::GuestAgent(QString host, std::uint16_t port) : host_(std::move(host)), port_(port) {}

std::optional<QJsonObject> GuestAgent::request(const QString& command, const QJsonObject& params,
                                               QString& error) {
    if (socket_.state() != QAbstractSocket::ConnectedState) {
        socket_.connectToHost(host_, port_);
        if (!socket_.waitForConnected(kConnectTimeoutMs)) {
            error = QStringLiteral("the guest agent at %1:%2 is not reachable: %3")
                        .arg(host_)
                        .arg(port_)
                        .arg(socket_.errorString());
            return std::nullopt;
        }
    }
    const int id = next_id_++;
    const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("id"), id},
                                                      {QStringLiteral("cmd"), command},
                                                      {QStringLiteral("params"), params}})
                                .toJson(QJsonDocument::Compact);
    QByteArray frame;
    frame.reserve(kHeaderSize + body.size());
    frame.append(static_cast<char>(kControlFrame));
    const auto length = static_cast<quint32>(body.size());
    for (int shift = 24; shift >= 0; shift -= 8) frame.append(static_cast<char>((length >> shift) & 0xFF));
    frame.append(body);
    socket_.write(frame);
    socket_.flush();

    std::optional<QJsonObject> reply = answer_to(id, error);
    if (!reply.has_value()) {
        error = QStringLiteral("%1: %2").arg(command, error);
        return std::nullopt;
    }
    if (reply->contains(QStringLiteral("error"))) {
        error = QStringLiteral("%1: %2").arg(command, reply->value(QStringLiteral("error")).toString());
        return std::nullopt;
    }
    QJsonObject result = reply->value(QStringLiteral("result")).toObject();
    // A handler that ran and refused says so in its result.
    if (!result.value(QStringLiteral("ok")).toBool(true)
        || !result.value(QStringLiteral("error")).toString().isEmpty()) {
        error = QStringLiteral("%1: %2").arg(command, result.value(QStringLiteral("error")).toString());
        return std::nullopt;
    }
    return result;
}

// Events arrive on the same connection and are not what was asked for.
std::optional<QJsonObject> GuestAgent::answer_to(int id, QString& error) {
    for (;;) {
        while (pending_.size() >= kHeaderSize) {
            const auto* head = reinterpret_cast<const unsigned char*>(pending_.constData());
            const quint32 length = (quint32{head[1]} << 24) | (quint32{head[2]} << 16)
                                   | (quint32{head[3]} << 8) | quint32{head[4]};
            if (pending_.size() < kHeaderSize + static_cast<qsizetype>(length)) break;
            const QByteArray body = pending_.mid(kHeaderSize, static_cast<qsizetype>(length));
            pending_.remove(0, kHeaderSize + static_cast<qsizetype>(length));
            const QJsonObject message = QJsonDocument::fromJson(body).object();
            if (message.value(QStringLiteral("id")).toInt(-1) == id) return message;
        }
        if (!socket_.waitForReadyRead(kAnswerTimeoutMs)) {
            error = QStringLiteral("no answer from the guest agent: %1").arg(socket_.errorString());
            return std::nullopt;
        }
        pending_.append(socket_.readAll());
    }
}

std::optional<std::uint32_t> GuestAgent::launch(const QString& executable, const QString& arguments,
                                                QString& error) {
    const std::optional<QJsonObject> result =
        request(QStringLiteral("launch"),
                QJsonObject{{QStringLiteral("app_id"), executable}, {QStringLiteral("args"), arguments}},
                error);
    if (!result.has_value()) return std::nullopt;
    return static_cast<std::uint32_t>(result->value(QStringLiteral("pid")).toInteger());
}

std::optional<std::uint32_t> GuestAgent::start_window_mirror(const MirrorStart& start, QString& error) {
    QJsonObject params{{QStringLiteral("port"), start.port},
                       {QStringLiteral("pid"), static_cast<qint64>(start.process_id)}};
    if (!start.exe.isEmpty()) params.insert(QStringLiteral("exe"), start.exe);
    if (!start.log.isEmpty()) params.insert(QStringLiteral("log_path"), start.log);
    const std::optional<QJsonObject> result = request(QStringLiteral("start_window_mirror"), params, error);
    if (!result.has_value()) return std::nullopt;
    return static_cast<std::uint32_t>(result->value(QStringLiteral("pid")).toInteger());
}

}  // namespace rwin::conform
