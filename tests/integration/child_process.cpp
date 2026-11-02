#include "child_process.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <thread>

namespace rwin::integration {

namespace {

constexpr auto kGrace = std::chrono::milliseconds(1500);
constexpr auto kReapPoll = std::chrono::milliseconds(10);

}

ChildProcess::ChildProcess(const Launch& launch) {
    int to_child[2] = {-1, -1};
    int from_child[2] = {-1, -1};
    if (launch.piped_stdio && (::pipe2(to_child, O_CLOEXEC) != 0 || ::pipe2(from_child, O_CLOEXEC) != 0)) {
        throw std::runtime_error("no pipes for " + launch.argv.front());
    }
    pid_ = ::fork();
    if (pid_ < 0) throw std::runtime_error("cannot fork for " + launch.argv.front());
    if (pid_ == 0) {
        ::setpgid(0, 0);
        for (const auto& [name, value] : launch.env) ::setenv(name.c_str(), value.c_str(), 1);
        const int log = ::open(launch.log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (launch.piped_stdio) {
            ::dup2(to_child[0], STDIN_FILENO);
            ::dup2(from_child[1], STDOUT_FILENO);
        } else {
            const int nothing = ::open("/dev/null", O_RDONLY);
            ::dup2(nothing, STDIN_FILENO);
            ::dup2(log, STDOUT_FILENO);
        }
        ::dup2(log, STDERR_FILENO);
        std::vector<char*> argv;
        argv.reserve(launch.argv.size() + 1);
        for (const std::string& arg : launch.argv) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        ::execvp(argv.front(), argv.data());
        ::_exit(127);
    }
    if (launch.piped_stdio) {
        ::close(to_child[0]);
        ::close(from_child[1]);
        input_ = to_child[1];
        output_ = from_child[0];
        ::fcntl(output_, F_SETFL, ::fcntl(output_, F_GETFL) | O_NONBLOCK);
    }
}

ChildProcess::~ChildProcess() { stop(); }

bool ChildProcess::running() {
    if (pid_ <= 0) return false;
    int status = 0;
    if (::waitpid(pid_, &status, WNOHANG) == pid_) {
        status_ = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        pid_ = -1;
        return false;
    }
    return true;
}

int ChildProcess::stop() {
    if (input_ >= 0) ::close(std::exchange(input_, -1));
    if (running()) {
        ::kill(pid_, SIGTERM);
        const auto until = std::chrono::steady_clock::now() + kGrace;
        while (running() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(kReapPoll);
        if (running()) {
            ::kill(-pid_, SIGKILL);
            ::kill(pid_, SIGKILL);
            int status = 0;
            ::waitpid(pid_, &status, 0);
            pid_ = -1;
        }
    }
    if (output_ >= 0) ::close(std::exchange(output_, -1));
    return status_;
}

}
