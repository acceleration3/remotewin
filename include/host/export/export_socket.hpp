#pragma once

#include "host/export/export_answer.hpp"
#include "host/export/unique_fd.hpp"

#include <optional>
#include <string>

namespace rwin::host {

struct ExportRequest;

// A connection to the GPU device's export socket: a SOCK_SEQPACKET unix
// socket QEMU listens on (``export-socket=`` on virtio-gpu-av-pci), where
// one datagram is one message and a dma-buf or a sync_file comes beside an
// answer as SCM_RIGHTS.
//
// Non-blocking. The device never waits to send: a consumer whose socket is
// full when an answer is due is taken to have gone and is cut off, so every
// datagram is read as soon as it is there.
class ExportSocket {
public:
    // What came: an answer and the descriptor beside it, if one did.
    struct Received {
        ExportAnswer answer;
        UniqueFd descriptor;
    };

    // Connected to ``path``; nothing, with ``why`` saying so, when nothing
    // there takes the connection.
    [[nodiscard]] static std::optional<ExportSocket> connect(const std::string& path, std::string& why);

    [[nodiscard]] int descriptor() const noexcept { return socket_.get(); }

    // False when the device has gone, or would not take it now.
    [[nodiscard]] bool send(const ExportRequest& request) const;

    // The next datagram when one is waiting. Nothing when none is, and
    // ``gone`` when there will be no more: the device closed the
    // connection, or sent something that is no answer.
    [[nodiscard]] std::optional<Received> receive(bool& gone) const;

    // Waits up to ``milliseconds`` for a datagram; false when none came.
    [[nodiscard]] bool wait(int milliseconds) const;

private:
    explicit ExportSocket(UniqueFd socket) noexcept : socket_(std::move(socket)) {}

    UniqueFd socket_;
};

}  // namespace rwin::host
