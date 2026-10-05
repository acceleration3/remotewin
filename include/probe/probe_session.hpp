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

// One harness connection: makes the windows it asks for, does to them what
// it asks, tells it what Windows made of them, and hands over what they
// were given as input.
//
// Every command is answered, in order, with a message that starts with
// whether it worked and, if not, why; the harness waits for each answer
// before asking again.
//
// Commands are served from a window of the session's own: the reader
// posts it a message as they arrive, and whichever message loop the
// thread is in - its own, or a modal dialog's Windows is running -
// dispatches it. A command that opens a modal dialog is answered from the
// modal loop it starts, once the dialog is up.
class ProbeSession {
public:
    explicit ProbeSession(guest::TcpClient& link);
    ~ProbeSession();

    ProbeSession(const ProbeSession&) = delete;
    ProbeSession& operator=(const ProbeSession&) = delete;
    ProbeSession(ProbeSession&&) = delete;
    ProbeSession& operator=(ProbeSession&&) = delete;

    // Returns when the harness says quit or goes away.
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
    // A modal call whose window has not been seen yet, and how many more
    // times to look for it.
    std::optional<int> awaiting_dialog_;
    bool running_ = true;
};

}  // namespace rwin::probe
