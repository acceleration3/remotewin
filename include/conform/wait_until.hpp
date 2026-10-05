#pragma once

#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

#include <chrono>

namespace rwin::conform {

// Runs the event loop until ``done`` says so or ``timeout`` passes, and
// says which. The harness is a sequence of things that each take the
// other side a while; written as waits, it reads in the order it happens,
// while the mirror's own signals keep being delivered underneath.
template <typename Done>
[[nodiscard]] bool wait_until(Done done, std::chrono::milliseconds timeout) {
    constexpr int kLookEveryMs = 10;
    if (done()) return true;
    QElapsedTimer clock;
    clock.start();
    QEventLoop loop;
    QTimer look;
    bool finished = false;
    QObject::connect(&look, &QTimer::timeout, &loop, [&] {
        finished = done();
        if (finished || clock.hasExpired(timeout.count())) loop.quit();
    });
    look.start(kLookEveryMs);
    loop.exec();
    return finished;
}

// Lets the other side get on with it for ``span``.
inline void idle_for(std::chrono::milliseconds span) {
    static_cast<void>(wait_until([] { return false; }, span));
}

}  // namespace rwin::conform
