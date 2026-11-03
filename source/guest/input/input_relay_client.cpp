#include "guest/input/input_relay_client.hpp"

#include "guest/input/input_batch.hpp"
#include "guest/input/input_pipe_name.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

InputRelayClient::InputRelayClient() {
    DWORD session = 0;
    ::ProcessIdToSessionId(::GetCurrentProcessId(), &session);
    session_ = session;
}

bool InputRelayClient::connect(std::uint32_t& error) {
    const InputPipeName name{session_};
    pipe_.reset(::CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
    if (pipe_.valid()) return true;
    error = ::GetLastError();
    pipe_.reset();
    return false;
}

bool InputRelayClient::write(const InputBatch& batch, std::uint32_t& error) {
    const auto frame = batch.frame();
    DWORD written = 0;
    if (::WriteFile(pipe_.get(), frame.data(), static_cast<DWORD>(frame.size()), &written, nullptr) != FALSE
        && written == frame.size()) {
        return true;
    }
    error = ::GetLastError();
    pipe_.reset();
    return false;
}

}
