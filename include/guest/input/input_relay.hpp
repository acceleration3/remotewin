#pragma once

#include "guest/input/input_injector.hpp"
#include "guest/windows/kernel_handle.hpp"

#include <cstdint>
#include <string>

namespace rwin::guest {
class InputRelay {
public:
    explicit InputRelay(std::uint32_t session);

    int run();

private:
    [[nodiscard]] KernelHandle create_instance(bool first, std::uint32_t& error) const;
    [[nodiscard]] bool client_allowed(void* pipe, std::string& why) const;
    void serve(KernelHandle pipe) const;

    std::uint32_t session_;
    std::wstring image_;
    InputInjector injector_;
};

}
