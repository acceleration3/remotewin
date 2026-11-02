#include "probe/probe_session.hpp"

#include "guest/session/tcp_client.hpp"
#include "logging/logger.hpp"
#include "probe/capture_screen.hpp"
#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"
#include "proto/probe/cursor_truth.hpp"
#include "proto/probe/input_record.hpp"
#include "proto/probe/probe_action.hpp"
#include "proto/probe/probe_hello.hpp"
#include "proto/probe/screen_pixels.hpp"
#include "proto/probe/window_spec.hpp"
#include "proto/probe/window_truth.hpp"

// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on

#include <format>

namespace rwin::probe {

namespace {

constexpr UINT kCommandsArrived = WM_APP + 1;
constexpr const wchar_t* kSessionClass = L"rwin-probe-session";
constexpr UINT_PTR kDialogTimer = 1;
constexpr UINT kDialogLookMs = 10;
constexpr int kDialogLooks = 300;

ProbeHello hello() {
    RECT work{};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    BOOL shadows = FALSE;
    ::SystemParametersInfoW(SPI_GETDROPSHADOW, 0, &shadows, 0);
    return ProbeHello{static_cast<std::uint32_t>(::GetCurrentProcessId()),
                      ::GetDpiForSystem(),
                      static_cast<std::uint32_t>(::GetSystemMetrics(SM_CXSCREEN)),
                      static_cast<std::uint32_t>(::GetSystemMetrics(SM_CYSCREEN)),
                      static_cast<std::uint32_t>(work.right - work.left),
                      static_cast<std::uint32_t>(work.bottom - work.top),
                      shadows != FALSE};
}

HWND make_session_window(ProbeSession* session, WNDPROC procedure) {
    WNDCLASSEXW described{};
    described.cbSize = sizeof(described);
    described.lpfnWndProc = procedure;
    described.hInstance = ::GetModuleHandleW(nullptr);
    described.lpszClassName = kSessionClass;
    ::RegisterClassExW(&described);
    HWND window = ::CreateWindowExW(0, kSessionClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                    ::GetModuleHandleW(nullptr), nullptr);
    if (window != nullptr) {
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(session));
    }
    return window;
}

}

ProbeSession::ProbeSession(guest::TcpClient& link) : link_(link) {
    window_ = make_session_window(this, reinterpret_cast<WNDPROC>(&ProbeSession::procedure));
    if (window_ == nullptr) {
        RWIN_ERROR("session", "no session window: error {}", ::GetLastError());
        running_ = false;
        return;
    }
    reader_ = std::make_unique<CommandReader>(link_, window_, kCommandsArrived);
}

ProbeSession::~ProbeSession() {
    reader_.reset();
    if (window_ != nullptr) ::DestroyWindow(window_);
}

void ProbeSession::run() {
    MessageWriter greeting{MessageId::ProbeHello};
    hello().write(greeting);
    send(greeting);
    MSG message{};
    while (running_ && ::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
}

std::intptr_t __stdcall ProbeSession::procedure(HWND window, unsigned message,
                                                std::uintptr_t wparam, std::intptr_t lparam) {
    auto* self = reinterpret_cast<ProbeSession*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self != nullptr && message == kCommandsArrived) {
        self->serve();
        return 0;
    }
    if (self != nullptr && message == WM_TIMER && wparam == kDialogTimer) {
        self->look_for_new_dialog();
        return 0;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

void ProbeSession::serve() {
    while (running_) {
        std::optional<std::vector<std::uint8_t>> command = reader_->next();
        if (!command.has_value()) break;
        MessageReader message{command->data(), command->size()};
        handle(message);
    }
    if (reader_->finished()) quit();
}

void ProbeSession::pump_messages() {
    MSG message{};
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
}

void ProbeSession::handle(MessageReader& message) {
    switch (message.id()) {
        case MessageId::ProbeCreateWindow:
            create_window(message);
            break;
        case MessageId::ProbeObserve:
            observe(message);
            break;
        case MessageId::ProbeObserveCursor:
            observe_cursor();
            break;
        case MessageId::ProbeCaptureScreen:
            capture(message);
            break;
        case MessageId::ProbeDestroyWindow:
            destroy_window(message);
            break;
        case MessageId::ProbeDestroyAll:
            pointer_.reset();
            windows_.destroy_all();
            pump_messages();
            answer_done();
            break;
        case MessageId::ProbeDrainInput:
            drain_input();
            break;
        case MessageId::ProbeSetBackdrop:
            set_backdrop(message);
            break;
        case MessageId::ProbeAct:
            act(message);
            break;
        case MessageId::ProbeQuit:
            RWIN_INFO("session", "asked to quit");
            answer_done();
            quit();
            break;
        default:
            RWIN_WARN("session", "unknown message id {}", static_cast<int>(message.id()));
            answer_failure(MessageId::ProbeDone, "unknown command");
            break;
    }
}

void ProbeSession::quit() {
    if (!running_) return;
    running_ = false;
    pointer_.reset();
    windows_.destroy_all();
    backdrop_.reset();
    ::PostQuitMessage(0);
}

void ProbeSession::create_window(MessageReader& message) {
    const std::optional<WindowSpec> spec = WindowSpec::read(message);
    if (!spec.has_value()) {
        answer_failure(MessageId::ProbeWindowCreated, "malformed window spec");
        return;
    }
    auto window = std::make_unique<ProbeWindow>(*spec, journal_);
    if (!window->made()) {
        RWIN_WARN("session", "{}", window->failure());
        answer_failure(MessageId::ProbeWindowCreated, window->failure());
        return;
    }
    pump_messages();
    MessageWriter out{MessageId::ProbeWindowCreated};
    out.put_u8(1);
    out.put_string("");
    out.put_u64(window->handle());
    windows_.adopt(std::move(window));
    send(out);
}

void ProbeSession::observe(MessageReader& message) {
    const std::uint64_t handle = message.u64();
    if (!message.ok()) {
        answer_failure(MessageId::ProbeTruth, "malformed observe");
        return;
    }
    const std::optional<WindowTruth> truth = windows_.truth(handle);
    if (!truth.has_value()) {
        answer_failure(MessageId::ProbeTruth, std::format("no probe window {:#x}", handle));
        return;
    }
    MessageWriter out{MessageId::ProbeTruth};
    out.put_u8(1);
    out.put_string("");
    truth->write(out);
    send(out);
}

void ProbeSession::observe_cursor() {
    pump_messages();
    MessageWriter out{MessageId::ProbeCursorTruth};
    out.put_u8(1);
    out.put_string("");
    cursor_reader_.read().write(out);
    send(out);
}

void ProbeSession::capture(MessageReader& message) {
    const std::int32_t left = message.i32();
    const std::int32_t top = message.i32();
    const std::uint32_t width = message.u32();
    const std::uint32_t height = message.u32();
    if (!message.ok()) {
        answer_failure(MessageId::ProbeScreenPixels, "malformed capture");
        return;
    }
    pump_messages();
    const std::optional<ScreenPixels> pixels = capture_screen(left, top, width, height);
    if (!pixels.has_value()) {
        answer_failure(MessageId::ProbeScreenPixels,
                       std::format("the screen at {},{} {}x{} could not be read", left, top,
                                   width, height));
        return;
    }
    MessageWriter out{MessageId::ProbeScreenPixels};
    out.put_u8(1);
    out.put_string("");
    pixels->write(out);
    send(out);
}

void ProbeSession::destroy_window(MessageReader& message) {
    const std::uint64_t handle = message.u64();
    if (!message.ok()) {
        answer_failure(MessageId::ProbeDone, "malformed destroy");
        return;
    }
    if (!windows_.destroy(handle)) {
        answer_failure(MessageId::ProbeDone, std::format("no probe window {:#x}", handle));
        return;
    }
    pump_messages();
    answer_done();
}

void ProbeSession::drain_input() {
    pump_messages();
    const std::vector<InputRecord> records = journal_.drain();
    MessageWriter out{MessageId::ProbeInput};
    out.put_u8(1);
    out.put_string("");
    out.put_u32(static_cast<std::uint32_t>(records.size()));
    for (const InputRecord& record : records) record.write(out);
    send(out);
}

void ProbeSession::set_backdrop(MessageReader& message) {
    const std::uint32_t rgb = message.u32();
    if (!message.ok()) {
        answer_failure(MessageId::ProbeDone, "malformed backdrop");
        return;
    }
    backdrop_.reset();
    backdrop_ = std::make_unique<BackdropProcess>(rgb);
    if (!backdrop_->running()) {
        answer_failure(MessageId::ProbeDone, "the backdrop process did not start");
        return;
    }
    answer_done();
}

void ProbeSession::act(MessageReader& message) {
    const std::optional<ProbeAction> action = ProbeAction::read(message);
    if (!action.has_value()) {
        answer_failure(MessageId::ProbeActed, "malformed action");
        return;
    }
    const bool modal = action->kind == ProbeAction::Kind::OpenModal
                       || action->kind == ProbeAction::Kind::ShowMessageBox;
    if (modal) {
        awaiting_dialog_ = kDialogLooks;
        ::SetTimer(window_, kDialogTimer, kDialogLookMs, nullptr);
    }
    std::string failure;
    const std::optional<std::uint64_t> made = actions_.perform(*action, failure);
    if (modal && !awaiting_dialog_.has_value()) return;
    if (modal) {
        ::KillTimer(window_, kDialogTimer);
        awaiting_dialog_.reset();
        if (failure.empty()) failure = "the dialog ended before it was seen";
    }
    if (!made.has_value() || modal) {
        RWIN_WARN("action", "{}", failure);
        answer_failure(MessageId::ProbeActed, failure);
        return;
    }
    pump_messages();
    answer_made(*made);
}

void ProbeSession::look_for_new_dialog() {
    if (!awaiting_dialog_.has_value()) {
        ::KillTimer(window_, kDialogTimer);
        return;
    }
    const std::uint64_t dialog = windows_.new_dialog();
    if (dialog == 0 && --*awaiting_dialog_ > 0) return;
    ::KillTimer(window_, kDialogTimer);
    awaiting_dialog_.reset();
    if (dialog == 0) {
        answer_failure(MessageId::ProbeActed, "no dialog came up");
        return;
    }
    windows_.follow(dialog);
    answer_made(dialog);
}

void ProbeSession::answer_made(std::uint64_t handle) {
    MessageWriter out{MessageId::ProbeActed};
    out.put_u8(1);
    out.put_string("");
    out.put_u64(handle);
    send(out);
}

void ProbeSession::answer_failure(MessageId id, std::string_view why) {
    MessageWriter out{id};
    out.put_u8(0);
    out.put_string(why);
    send(out);
}

void ProbeSession::answer_done() {
    MessageWriter out{MessageId::ProbeDone};
    out.put_u8(1);
    out.put_string("");
    send(out);
}

void ProbeSession::send(MessageWriter& message) {
    if (!link_.send(message.take())) quit();
}

}
