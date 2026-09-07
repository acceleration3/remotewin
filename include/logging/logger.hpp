#pragma once

#include <format>
#include <string>
#include <string_view>

namespace rwin {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

// Every line says when, how loud, from where, and in which thread:
//
//   [    0.041273]  info  link     main | guest dialled in from 10.0.2.15:49871
//
// The elapsed stamp is relative to process start, because what matters
// when two processes are talking to each other is how long after their
// own beginning each did something, and the two clocks need not agree.
class Logger {
public:
    static Logger& instance();

    void set_ceiling(LogLevel level) noexcept { ceiling_ = level; }
    [[nodiscard]] bool wants(LogLevel level) const noexcept { return level >= ceiling_; }

    // ``tag`` names the subsystem, not the file: the reader is following
    // a conversation between two processes, not a call stack.
    void write(LogLevel level, std::string_view tag, std::string_view message);

    void opened(std::string_view program);

private:
    Logger();

    LogLevel ceiling_ = LogLevel::Info;
};

}  // namespace rwin

#define RWIN_LOG(level, tag, ...)                                              \
    do {                                                                       \
        if (::rwin::Logger::instance().wants(level)) {                         \
            ::rwin::Logger::instance().write((level), (tag),                   \
                                             std::format(__VA_ARGS__));        \
        }                                                                      \
    } while (false)

#define RWIN_TRACE(tag, ...) RWIN_LOG(::rwin::LogLevel::Trace, tag, __VA_ARGS__)
#define RWIN_DEBUG(tag, ...) RWIN_LOG(::rwin::LogLevel::Debug, tag, __VA_ARGS__)
#define RWIN_INFO(tag, ...)  RWIN_LOG(::rwin::LogLevel::Info,  tag, __VA_ARGS__)
#define RWIN_WARN(tag, ...)  RWIN_LOG(::rwin::LogLevel::Warn,  tag, __VA_ARGS__)
#define RWIN_ERROR(tag, ...) RWIN_LOG(::rwin::LogLevel::Error, tag, __VA_ARGS__)
