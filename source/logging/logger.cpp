#include "logging/logger.hpp"

#include <chrono>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace rwin {

namespace {

std::chrono::steady_clock::time_point process_start = std::chrono::steady_clock::now();

const char* name_of(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "trace";
        case LogLevel::Debug: return "debug";
        case LogLevel::Info:  return " info";
        case LogLevel::Warn:  return " warn";
        case LogLevel::Error: return "error";
    }
    return "?????";
}

LogLevel level_from_environment() {
    const char* wanted = std::getenv("RWIN_LOG");
    if (wanted == nullptr) return LogLevel::Info;
    const std::string_view name{wanted};
    if (name == "trace") return LogLevel::Trace;
    if (name == "debug") return LogLevel::Debug;
    if (name == "warn") return LogLevel::Warn;
    if (name == "error") return LogLevel::Error;
    return LogLevel::Info;
}

// One writer at a time, or two threads interleave halfway through a line
// and the log stops being readable exactly when it is most needed.
std::mutex& writing() {
    static std::mutex mutex;
    return mutex;
}

}  // namespace

Logger::Logger() : ceiling_(level_from_environment()) {}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::write(LogLevel level, std::string_view tag, std::string_view message) {
    const auto elapsed = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - process_start)
                             .count();
    const std::size_t thread =
        std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0xFFFF;
    const std::string line = std::format("[{:12.6f}] {} {:<8.8} {:04x} | {}\n", elapsed,
                                         name_of(level), tag, thread, message);

    const std::lock_guard lock{writing()};
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fflush(stderr);
}

void Logger::opened(std::string_view program) {
    write(LogLevel::Info, "log",
          std::format("{} log opened: level={} (RWIN_LOG=trace|debug|info|warn|error)",
                      program, name_of(ceiling_)));
}

}  // namespace rwin
