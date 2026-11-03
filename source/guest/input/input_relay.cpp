#include "guest/input/input_relay.hpp"

#include "guest/input/input_batch.hpp"
#include "guest/input/input_desktop.hpp"
#include "guest/input/input_pipe_name.hpp"
#include "guest/windows/wide_text.hpp"
#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
#include <sddl.h>
// clang-format on

#include <cstdlib>
#include <thread>

namespace rwin::guest {

namespace {
constexpr const wchar_t* kPipeSecurity = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)S:(ML;;NWNRNX;;;HI)";
constexpr DWORD kPipeBuffer = 4096;
constexpr DWORD kMaxImagePath = 32768;

std::wstring image_of(HANDLE process) {
    std::wstring path(kMaxImagePath, L'\0');
    DWORD size = kMaxImagePath;
    if (::QueryFullProcessImageNameW(process, 0, path.data(), &size) == FALSE) return {};
    path.resize(size);
    return path;
}

class PipeSecurity {
public:
    PipeSecurity() {
        ok_ = ::ConvertStringSecurityDescriptorToSecurityDescriptorW(kPipeSecurity, SDDL_REVISION_1,
                                                                     &descriptor_, nullptr) != FALSE;
    }
    ~PipeSecurity() {
        if (descriptor_ != nullptr) ::LocalFree(descriptor_);
    }

    PipeSecurity(const PipeSecurity&) = delete;
    PipeSecurity& operator=(const PipeSecurity&) = delete;

    [[nodiscard]] bool ok() const noexcept { return ok_; }

    [[nodiscard]] SECURITY_ATTRIBUTES attributes() const noexcept {
        return SECURITY_ATTRIBUTES{sizeof(SECURITY_ATTRIBUTES), descriptor_, FALSE};
    }

private:
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
    bool ok_ = false;
};

}

InputRelay::InputRelay(std::uint32_t session) : session_(session) {
    image_ = image_of(::GetCurrentProcess());
}

KernelHandle InputRelay::create_instance(bool first, std::uint32_t& error) const {
    const PipeSecurity security;
    if (!security.ok()) {
        error = ::GetLastError();
        return {};
    }
    SECURITY_ATTRIBUTES attributes = security.attributes();
    const InputPipeName name{session_};
    const DWORD open_mode = PIPE_ACCESS_INBOUND | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
    KernelHandle pipe{::CreateNamedPipeW(name.c_str(), open_mode,
                                         PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT
                                             | PIPE_REJECT_REMOTE_CLIENTS,
                                         PIPE_UNLIMITED_INSTANCES, 0, kPipeBuffer, 0, &attributes)};
    if (!pipe.valid()) error = ::GetLastError();
    return pipe;
}

bool InputRelay::client_allowed(void* pipe, std::string& why) const {
    ULONG pid = 0;
    if (::GetNamedPipeClientProcessId(pipe, &pid) == FALSE) {
        why = "cannot identify the client (error " + std::to_string(::GetLastError()) + ")";
        return false;
    }
    DWORD session = 0;
    if (::ProcessIdToSessionId(pid, &session) == FALSE || session != session_) {
        why = "pid " + std::to_string(pid) + " is in session " + std::to_string(session) + ", not "
              + std::to_string(session_);
        return false;
    }
    KernelHandle process{::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    if (!process.valid()) {
        why = "cannot open pid " + std::to_string(pid) + " (error " + std::to_string(::GetLastError()) + ")";
        return false;
    }
    const std::wstring image = image_of(process.get());
    if (image.empty() || _wcsicmp(image.c_str(), image_.c_str()) != 0) {
        why = "pid " + std::to_string(pid) + " runs '" + utf8_of(image) + "', not '" + utf8_of(image_) + "'";
        return false;
    }
    return true;
}

void InputRelay::serve(KernelHandle pipe) const {
    std::string why;
    if (!client_allowed(pipe.get(), why)) {
        RWIN_WARN("relay", "dropped a client: {}", why);
        ::DisconnectNamedPipe(pipe.get());
        return;
    }
    RWIN_INFO("relay", "client connected");
    InputDesktop desktop;
    std::byte buffer[InputBatch::kMaxFrameBytes];
    for (;;) {
        DWORD got = 0;
        if (::ReadFile(pipe.get(), buffer, sizeof buffer, &got, nullptr) == FALSE) {
            const DWORD error = ::GetLastError();
            if (error == ERROR_MORE_DATA) {
                RWIN_WARN("relay", "dropped a client that sent an oversized frame");
            } else if (error != ERROR_BROKEN_PIPE) {
                RWIN_WARN("relay", "client read failed (error {})", error);
            }
            break;
        }
        const auto batch = InputBatch::from_frame(std::span<const std::byte>{buffer, got});
        if (!batch) {
            RWIN_WARN("relay", "dropped a client that sent a malformed frame of {} bytes", got);
            break;
        }
        if (!desktop.follow()) RWIN_WARN("relay", "no input desktop to follow; injecting where this thread is");
        (void)injector_.inject(*batch);
    }
    RWIN_INFO("relay", "client gone");
}

int InputRelay::run() {
    DWORD own_session = 0;
    ::ProcessIdToSessionId(::GetCurrentProcessId(), &own_session);
    if (own_session != session_) {
        RWIN_ERROR("relay", "asked to serve session {} from session {}", session_, own_session);
        return EXIT_FAILURE;
    }
    RWIN_INFO("relay", "serving session {} from '{}'", session_, utf8_of(image_));
    bool first = true;
    for (;;) {
        std::uint32_t error = 0;
        KernelHandle pipe = create_instance(first, error);
        if (!pipe.valid()) {
            RWIN_ERROR("relay", "cannot create the pipe (error {}); another relay may own it", error);
            return EXIT_FAILURE;
        }
        first = false;
        if (::ConnectNamedPipe(pipe.get(), nullptr) == FALSE && ::GetLastError() != ERROR_PIPE_CONNECTED) {
            RWIN_WARN("relay", "accept failed (error {})", ::GetLastError());
            continue;
        }
        std::thread{[this, taken = std::move(pipe)]() mutable { serve(std::move(taken)); }}.detach();
    }
}

}
