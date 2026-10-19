#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QTcpSocket>

#include <cstdint>
#include <optional>

namespace rwin::conform {

// The one part of the harness that knows who runs the guest: LSW's guest
// agent, reached on its control port, asked to start the two guest-side
// processes a run needs - the probe, and the mirror's guest half beside it.
// remotewin itself knows nothing of LSW; this is the glue, and is all of
// it.
//
// Its wire is LSW's (docs/link-protocol.md): [type:u8 = 1][length:u32 big
// endian][JSON], a request {"id", "cmd", "params"} answered by {"id",
// "result"} or {"id", "error"}, with the agent's own events in between.
// Talked to directly rather than through the LSW manager's MCP, which
// needs the manager running and a session handshake for the same two
// calls.
class GuestAgent {
public:
    GuestAgent(QString host, std::uint16_t port);

    // Starts ``executable`` in the user's desktop session with
    // ``arguments`` after it. Its process id.
    [[nodiscard]] std::optional<std::uint32_t> launch(const QString& executable,
                                                      const QString& arguments, QString& error);

    // The mirror's guest half to start: in the session of ``process_id``,
    // dialling back to ``port``. ``exe`` and ``log`` are guest paths; empty,
    // the agent's own (the guest half installed with it, and its log).
    struct MirrorStart {
        std::uint16_t port = 0;
        std::uint32_t process_id = 0;
        QString exe;
        QString log;
    };

    // Its process id.
    [[nodiscard]] std::optional<std::uint32_t> start_window_mirror(const MirrorStart& start, QString& error);

private:
    [[nodiscard]] std::optional<QJsonObject> request(const QString& command,
                                                     const QJsonObject& params, QString& error);
    [[nodiscard]] std::optional<QJsonObject> answer_to(int id, QString& error);

    QString host_;
    std::uint16_t port_;
    QTcpSocket socket_;
    QByteArray pending_;
    int next_id_ = 1;
};

}  // namespace rwin::conform
