#pragma once

#include "probe/backdrop_process.hpp"
#include "probe/command_reader.hpp"
#include "probe/cursor_reader.hpp"
#include "probe/dialog_boxes.hpp"
#include "probe/input_journal.hpp"
#include "probe/pointer_actions.hpp"
#include "probe/probe_cursors.hpp"
#include "probe/probe_windows.hpp"
#include "probe/window_actions.hpp"
#include "proto/message_id.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

struct HWND__;

namespace rwin {
class MessageReader;
class MessageWriter;
}

namespace rwin::guest {
class TcpClient;
}

namespace rwin::probe {
class ProbeSession {
public:
    explicit ProbeSession(guest::TcpClient& link);
    ~ProbeSession();

    ProbeSession(const ProbeSession&) = delete;
    ProbeSession& operator=(const ProbeSession&) = delete;
    ProbeSession(ProbeSession&&) = delete;
    ProbeSession& operator=(ProbeSession&&) = delete;

    void run();

private:
    static std::intptr_t __stdcall procedure(HWND__* window, unsigned message,
                                             std::uintptr_t wparam, std::intptr_t lparam);
    void serve();
    void handle(MessageReader& message);
    void create_window(MessageReader& message);
    void observe(MessageReader& message);
    void observe_cursor();
    void capture(MessageReader& message);
    void destroy_window(MessageReader& message);
    void drain_input();
    void set_backdrop(MessageReader& message);
    void act(MessageReader& message);
    void look_for_new_dialog();
    void quit();
    void pump_messages();
    void answer_failure(MessageId id, std::string_view why);
    void answer_done();
    void answer_made(std::uint64_t handle);
    void send(MessageWriter& message);

    guest::TcpClient& link_;
    HWND__* window_ = nullptr;
    std::unique_ptr<CommandReader> reader_;
    InputJournal journal_;
    ProbeWindows windows_;
    DialogBoxes dialogs_{journal_};
    ProbeCursors cursors_;
    PointerActions pointer_{windows_, cursors_};
    WindowActions actions_{windows_, dialogs_, pointer_};
    CursorReader cursor_reader_{pointer_};
    std::unique_ptr<BackdropProcess> backdrop_;
    std::optional<int> awaiting_dialog_;
    bool running_ = true;
};

}
