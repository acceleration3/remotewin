#pragma once

#include <string>
#include <utility>
#include <vector>

#include <sys/types.h>

namespace rwin::integration {
class ChildProcess {
public:
    struct Launch {
        std::vector<std::string> argv;
        std::vector<std::pair<std::string, std::string>> env;
        std::string log_path;
        bool piped_stdio = false;
    };

    explicit ChildProcess(const Launch& launch);
    ~ChildProcess();

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&&) = delete;
    ChildProcess& operator=(ChildProcess&&) = delete;

    [[nodiscard]] pid_t pid() const noexcept { return pid_; }
    [[nodiscard]] int output() const noexcept { return output_; }
    [[nodiscard]] bool running();
    int stop();

private:
    pid_t pid_ = -1;
    int input_ = -1;
    int output_ = -1;
    int status_ = -1;
};

}
