#pragma once

#include <string>
#include <utility>
#include <vector>

#include <sys/types.h>

namespace rwin::integration {

// A program the harness starts and owns: ended (SIGTERM, then SIGKILL) and
// reaped when this goes. Its output goes to a file; with ``piped_stdio`` its
// stdin and stdout are pipes this end holds instead.
class ChildProcess {
public:
    struct Launch {
        std::vector<std::string> argv;
        // NAME=value, added to this process's own environment.
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
    // Its stdout, readable; -1 without piped stdio.
    [[nodiscard]] int output() const noexcept { return output_; }
    // Still running.
    [[nodiscard]] bool running();
    // SIGTERM, a grace period, SIGKILL; reaped. The exit status, or -1.
    int stop();

private:
    pid_t pid_ = -1;
    int input_ = -1;
    int output_ = -1;
    int status_ = -1;
};

}  // namespace rwin::integration
