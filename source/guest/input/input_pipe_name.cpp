#include "guest/input/input_pipe_name.hpp"

namespace rwin::guest {

InputPipeName::InputPipeName(std::uint32_t session)
    : text_(L"\\\\.\\pipe\\remotewin-input-" + std::to_wstring(session)) {}

}
