#pragma once

#include "proto/frame_decoder.hpp"
#include "proto/message_id.hpp"
#include "proto/message_reader.hpp"
#include "proto/probe/cursor_truth.hpp"
#include "proto/probe/input_record.hpp"
#include "proto/probe/probe_action.hpp"
#include "proto/probe/probe_hello.hpp"
#include "proto/probe/screen_pixels.hpp"
#include "proto/probe/window_spec.hpp"
#include "proto/probe/window_truth.hpp"

#include <QObject>
#include <QRect>
#include <QString>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

class QTcpServer;
class QTcpSocket;

namespace rwin::conform {

// The connection to the probe in the guest: the harness asks, the probe
// answers, one question at a time.
//
// The probe dials in, for the reason the mirror's guest half does: the
// guest can always reach the host, and the host can reach a guest port
// only through a rule set before the VM booted. Each call waits for its
// answer with the event loop running, so the mirror carries on while the
// probe works.
class ProbeLink : public QObject {
    Q_OBJECT
public:
    explicit ProbeLink(QObject* parent = nullptr);

    // On a port the system picks.
    [[nodiscard]] bool listen();
    [[nodiscard]] std::uint16_t port() const;

    [[nodiscard]] std::optional<ProbeHello> wait_for_probe(std::chrono::milliseconds timeout);

    // Every call below returns nothing, or false, with ``error`` saying
    // why: the probe refused, or did not answer in time.
    [[nodiscard]] std::optional<std::uint64_t> create_window(const WindowSpec& spec,
                                                             QString& error);
    [[nodiscard]] std::optional<WindowTruth> observe(std::uint64_t window, QString& error);
    // The cursor and the pointer as Windows has them now.
    [[nodiscard]] std::optional<CursorTruth> observe_cursor(QString& error);
    // The window the action made, 0 when it makes none.
    [[nodiscard]] std::optional<std::uint64_t> act(const ProbeAction& action, QString& error);
    // ``area`` in the guest's screen pixels.
    [[nodiscard]] std::optional<ScreenPixels> capture(QRect area, QString& error);
    [[nodiscard]] bool destroy_window(std::uint64_t window, QString& error);
    [[nodiscard]] bool destroy_all(QString& error);
    [[nodiscard]] std::optional<std::vector<InputRecord>> drain_input(QString& error);
    // ``rgb`` is 0x00RRGGBB.
    [[nodiscard]] bool set_backdrop(std::uint32_t rgb, QString& error);
    [[nodiscard]] bool quit(QString& error);

Q_SIGNALS:
    void lost();

private:
    void accept_probe();
    void read_available();
    // Sends a command and waits for the answer of kind ``answer``. On
    // success the reader is past the status, at the answer's own fields;
    // it reads from a buffer the next exchange replaces.
    [[nodiscard]] std::optional<MessageReader> exchange(std::vector<std::uint8_t> command,
                                                        MessageId answer, QString& error);

    QTcpServer* server_;
    QTcpSocket* probe_ = nullptr;
    FrameDecoder decoder_;
    std::optional<ProbeHello> hello_;
    std::vector<std::uint8_t> answer_;
    bool answered_ = false;
    // Answers still to come for commands that timed out, to be dropped
    // when they do rather than taken for the answer to a later one.
    int owed_ = 0;
};

}  // namespace rwin::conform
