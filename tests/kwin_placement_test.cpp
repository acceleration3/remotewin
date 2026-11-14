#include "host/dbus/bus_connection.hpp"
#include "host/link/guest_window.hpp"
#include "host/loop/poll_loop.hpp"
#include "host/placement/kwin_placement.hpp"
#include "host/placement/placement_sync.hpp"

#include <systemd/sd-bus.h>
#include <systemd/sd-id128.h>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using rwin::Rect;
using rwin::WindowFields;
using rwin::WindowId;
using rwin::host::BusConnection;
using rwin::host::GuestWindow;
using rwin::host::KWinPlacement;
using rwin::host::PlacementSync;
using rwin::host::Point;
using rwin::host::PointF;
using rwin::host::PollLoop;

int failures = 0;

void check(bool ok, const char* label) {
    if (ok) return;
    std::printf("FAIL: %s\n", label);
    ++failures;
}

class FakeKWinBus {
public:
    FakeKWinBus() {
        static int made = 0;
        const std::string name = "rwin-kwin-placement-test-" + std::to_string(::getpid()) + "-" + std::to_string(++made);
        listener_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path + 1, name.data(), name.size());
        const auto length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + name.size());
        if (::bind(listener_, reinterpret_cast<const sockaddr*>(&address), length) != 0 || ::listen(listener_, 1) != 0) {
            std::printf("FAIL: cannot listen as the test's bus\n");
            std::exit(1);
        }
        ::setenv("DBUS_SESSION_BUS_ADDRESS", ("unix:abstract=" + name).c_str(), 1);
        thread_ = std::thread([this] { serve(); });
    }

    ~FakeKWinBus() {
        stop_ = true;
        thread_.join();
        ::close(listener_);
    }

    FakeKWinBus(const FakeKWinBus&) = delete;
    FakeKWinBus& operator=(const FakeKWinBus&) = delete;
    FakeKWinBus(FakeKWinBus&&) = delete;
    FakeKWinBus& operator=(FakeKWinBus&&) = delete;

    void hold_starts(bool hold) { hold_ = hold; }

    void release() {
        std::unique_lock lock{mutex_};
        release_ = true;
        changed_.wait(lock, [this] { return !release_; });
    }

    [[nodiscard]] int loads() const { return loads_; }
    [[nodiscard]] int starts() const { return starts_; }
    [[nodiscard]] int unloads() const { return unloads_; }
    [[nodiscard]] std::size_t loaded_now() {
        const std::lock_guard lock{mutex_};
        return scripts_.size();
    }

private:
    struct Script {
        std::string name;
        std::string source;
        bool started = false;
    };

    struct Report {
        const char* method;
        std::string text;
    };

    struct Held {
        sd_bus_message* call;
        std::vector<Report> reports;
    };

    static std::string between(const std::string& text, const std::string& before, const std::string& after) {
        const std::size_t from = text.find(before);
        if (from == std::string::npos) return {};
        const std::size_t to = text.find(after, from + before.size());
        if (to == std::string::npos) return {};
        return text.substr(from + before.size(), to - from - before.size());
    }

    static std::string unescaped(const std::string& js) {
        std::string out;
        for (std::size_t at = 0; at + 6 <= js.size() && js[at] == '\\'; at += 6) {
            const unsigned code = static_cast<unsigned>(std::stoul(js.substr(at + 2, 4), nullptr, 16));
            if (code < 0x80) {
                out += static_cast<char>(code);
            } else if (code < 0x800) {
                out += static_cast<char>(0xC0 | (code >> 6));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else {
                out += static_cast<char>(0xE0 | (code >> 12));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            }
        }
        return out;
    }

    static void reports_of(const Script& script, std::vector<Report>& reports) {
        if (script.source.find("workspace.screensChanged") != std::string::npos) {
            reports.push_back(Report{"outputs", "A 0 0 1920 1080 1"});
            return;
        }
        const std::string tag = unescaped(between(script.source, "const tag = \"", "\""));
        const std::string placement = between(script.source, "const placement = ", ";");
        std::string x = between(script.source, "w.frameGeometry = {x: ", ",");
        std::string y = between(script.source, ", y: ", ",");
        if (x.empty()) {
            x = "0";
            y = "0";
        }
        reports.push_back(Report{"moved", tag + " " + x + " " + y + " 800 600 0 0 0"});
        reports.push_back(Report{"placed", tag + " " + placement + " " + x + " " + y});
    }

    static int hello(sd_bus_message* call, void*, sd_bus_error*) { return sd_bus_reply_method_return(call, "s", ":1.1"); }

    static int name_has_owner(sd_bus_message* call, void*, sd_bus_error*) {
        return sd_bus_reply_method_return(call, "b", 1);
    }

    static int load_script(sd_bus_message* call, void* self, sd_bus_error*) {
        auto* kwin = static_cast<FakeKWinBus*>(self);
        const char* path = nullptr;
        const char* name = nullptr;
        if (sd_bus_message_read(call, "ss", &path, &name) < 0) return -EINVAL;
        std::ifstream file{path};
        if (!file) return sd_bus_reply_method_return(call, "i", -1);
        std::string source{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
        {
            const std::lock_guard lock{kwin->mutex_};
            kwin->scripts_.push_back(Script{name, std::move(source), false});
        }
        return sd_bus_reply_method_return(call, "i", ++kwin->loads_);
    }

    static int start(sd_bus_message* call, void* self, sd_bus_error*) {
        auto* kwin = static_cast<FakeKWinBus*>(self);
        std::vector<Report> reports;
        {
            const std::lock_guard lock{kwin->mutex_};
            for (Script& script : kwin->scripts_) {
                if (!std::exchange(script.started, true)) reports_of(script, reports);
            }
        }
        if (kwin->hold_) {
            kwin->held_.push_back(Held{sd_bus_message_ref(call), std::move(reports)});
        } else {
            kwin->answer(call, reports);
        }
        ++kwin->starts_;
        return 1;
    }

    static int unload_script(sd_bus_message* call, void* self, sd_bus_error*) {
        auto* kwin = static_cast<FakeKWinBus*>(self);
        const char* name = nullptr;
        if (sd_bus_message_read(call, "s", &name) < 0) return -EINVAL;
        {
            const std::lock_guard lock{kwin->mutex_};
            std::erase_if(kwin->scripts_, [name](const Script& script) { return script.name == name; });
        }
        ++kwin->unloads_;
        return sd_bus_reply_method_return(call, "b", 1);
    }

    void answer(sd_bus_message* call, const std::vector<Report>& reports) {
        for (const Report& report : reports) {
            sd_bus_call_method_async(bus_, nullptr, ":1.1", "/remotewin/placement", "org.remotewin.Placement",
                                     report.method, nullptr, nullptr, "s", report.text.c_str());
        }
        sd_bus_reply_method_return(call, "");
        sd_bus_flush(bus_);
    }

    void release_held() {
        std::unique_lock lock{mutex_};
        if (!release_) return;
        lock.unlock();
        for (Held& held : held_) {
            answer(held.call, held.reports);
            sd_bus_message_unref(held.call);
        }
        held_.clear();
        lock.lock();
        release_ = false;
        changed_.notify_all();
    }

    void serve() {
        int client = -1;
        while (!stop_ && client < 0) {
            pollfd waiting{listener_, POLLIN, 0};
            if (::poll(&waiting, 1, 20) > 0) client = ::accept4(listener_, nullptr, nullptr, SOCK_CLOEXEC);
        }
        if (client < 0) return;
        static const sd_bus_vtable kDriver[] = {
            SD_BUS_VTABLE_START(0),
            SD_BUS_METHOD("Hello", "", "s", &FakeKWinBus::hello, SD_BUS_VTABLE_UNPRIVILEGED),
            SD_BUS_METHOD("NameHasOwner", "s", "b", &FakeKWinBus::name_has_owner, SD_BUS_VTABLE_UNPRIVILEGED),
            SD_BUS_VTABLE_END,
        };
        static const sd_bus_vtable kScripting[] = {
            SD_BUS_VTABLE_START(0),
            SD_BUS_METHOD("loadScript", "ss", "i", &FakeKWinBus::load_script, SD_BUS_VTABLE_UNPRIVILEGED),
            SD_BUS_METHOD("start", "", "", &FakeKWinBus::start, SD_BUS_VTABLE_UNPRIVILEGED),
            SD_BUS_METHOD("unloadScript", "s", "b", &FakeKWinBus::unload_script, SD_BUS_VTABLE_UNPRIVILEGED),
            SD_BUS_VTABLE_END,
        };
        sd_id128_t id{};
        sd_id128_randomize(&id);
        const bool up = sd_bus_new(&bus_) >= 0 && sd_bus_set_fd(bus_, client, client) >= 0
                        && sd_bus_set_server(bus_, 1, id) >= 0 && sd_bus_start(bus_) >= 0
                        && sd_bus_add_object_vtable(bus_, nullptr, "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                                    kDriver, this) >= 0
                        && sd_bus_add_object_vtable(bus_, nullptr, "/Scripting", "org.kde.kwin.Scripting", kScripting,
                                                    this) >= 0;
        if (!up) {
            std::printf("FAIL: the test's bus did not start\n");
            std::exit(1);
        }
        while (!stop_) {
            const int processed = sd_bus_process(bus_, nullptr);
            if (processed < 0) break;
            release_held();
            if (processed == 0) sd_bus_wait(bus_, 500);
        }
        for (Held& held : held_) sd_bus_message_unref(held.call);
        held_.clear();
        sd_bus_close(bus_);
        sd_bus_unref(bus_);
        std::unique_lock lock{mutex_};
        release_ = false;
        changed_.notify_all();
    }

    int listener_ = -1;
    std::thread thread_;
    sd_bus* bus_ = nullptr;
    std::atomic<bool> stop_{false};
    std::atomic<bool> hold_{false};
    std::atomic<int> loads_{0};
    std::atomic<int> starts_{0};
    std::atomic<int> unloads_{0};
    std::mutex mutex_;
    std::condition_variable changed_;
    bool release_ = false;
    std::vector<Script> scripts_;
    std::vector<Held> held_;
};

bool run_until(PollLoop& loop, const std::function<bool()>& reached) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    const auto look = loop.timer([&] {
        if (reached() || std::chrono::steady_clock::now() > deadline) loop.quit(0);
    });
    look->start(std::chrono::milliseconds(1), true);
    static_cast<void>(loop.run());
    return reached();
}

void turn(PollLoop& loop) {
    const auto stop = loop.timer([&] { loop.quit(0); });
    stop->start(std::chrono::milliseconds::zero(), false);
    static_cast<void>(loop.run());
}

constexpr std::uint64_t kWindow = 0x000302ce00000001;

struct Placed {
    std::uint64_t window;
    PointF corner;
};

struct Placing {
    FakeKWinBus kwin;
    PollLoop loop;
    BusConnection bus{loop};
    KWinPlacement placement{loop, bus};
    std::vector<Placed> placed;

    Placing() {
        placement.window_placed.connect(
            [this](std::uint64_t window, PointF corner, std::int64_t) { placed.push_back(Placed{window, corner}); });
        check(run_until(loop, [this] { return placement.active(); }), "the watcher runs in the test's KWin");
        kwin.hold_starts(true);
    }

    void place_and_wait_for_its_start(PointF corner) {
        const int before = kwin.starts();
        placement.place(kWindow, corner);
        check(run_until(loop, [&] { return kwin.starts() == before + 1; }), "KWin is asked to start the placement");
    }
};

void a_report_waiting_when_its_placement_is_replaced() {
    Placing placing;
    placing.place_and_wait_for_its_start(PointF{100, 100});
    placing.kwin.release();
    placing.placement.place(kWindow, PointF{200, 200});
    check(placing.placed.empty(), "nothing is reported from inside a placement");
    const int before = placing.kwin.starts();
    check(run_until(placing.loop, [&] { return placing.kwin.starts() == before + 1; }),
          "the replacing placement is started");
    placing.kwin.release();
    check(run_until(placing.loop, [&] { return !placing.placed.empty(); }), "the replacing placement is reported");
    check(placing.placed.size() == 1 && placing.placed[0].window == kWindow
              && placing.placed[0].corner.x == 200 && placing.placed[0].corner.y == 200,
          "only the replacing placement is taken");
}

void a_report_waiting_when_a_third_placement_is_made() {
    Placing placing;
    placing.place_and_wait_for_its_start(PointF{100, 100});
    placing.placement.place(kWindow, PointF{200, 200});
    placing.kwin.release();
    placing.placement.place(kWindow, PointF{300, 300});
    check(placing.placed.empty(), "nothing is reported from inside a placement");
    check(run_until(placing.loop, [&] {
              placing.kwin.release();
              return !placing.placed.empty();
          }),
          "the last placement is reported");
    check(placing.placed.size() == 1 && placing.placed[0].corner.x == 300 && placing.placed[0].corner.y == 300,
          "only the last placement is taken");
}

void replaced_placements_are_unloaded() {
    Placing placing;
    placing.kwin.hold_starts(false);
    constexpr int kPlacements = 50;
    for (int step = 0; step < kPlacements; ++step) placing.placement.place(kWindow, PointF{100.0 + step, 100});
    check(run_until(placing.loop, [&] { return !placing.placed.empty() && placing.kwin.loaded_now() == 1; }),
          "every placement but the watcher is unloaded from KWin");
    check(placing.placed.size() == 1 && placing.placed[0].corner.x == 100.0 + kPlacements - 1,
          "the last of them is the one reported");
}

GuestWindow guest_at(WindowId window, int x, int y) {
    WindowFields fields;
    fields.geometry.frame = Rect{x, y, 800, 600};
    fields.geometry.picture = Rect{0, 0, 800, 600};
    return GuestWindow{window, fields};
}

void a_stream_of_guest_moves() {
    FakeKWinBus kwin;
    PollLoop loop;
    BusConnection bus{loop};
    PlacementSync sync{loop, bus};
    sync.prefer_output("A");
    sync.move_guest = [](WindowId, Point) { return true; };
    sync.compositor_has_state = [](WindowId) { return false; };
    check(run_until(loop, [&] { return sync.active(); }), "the watcher runs in the test's KWin");
    constexpr WindowId kDragged{0x302ce, 1};
    constexpr WindowId kBeside{0x40111, 1};
    constexpr WindowId kPassing{0x50222, 1};
    constexpr int kMoves = 4000;
    constexpr int kAppears = 700;
    constexpr int kDisappears = 2600;
    for (int move = 0; move < kMoves; ++move) {
        sync.follow_guest(guest_at(kDragged, 100 + move % 900, 80 + move % 400), 1, true, false);
        if (move % 5 == 0) sync.follow_guest(guest_at(kBeside, 300 + move % 200, 200), 1, true, false);
        if (move >= kAppears && move < kDisappears) {
            sync.follow_guest(guest_at(kPassing, 20 + move % 50, 20 + move % 70), 1 + move / 500, move % 11 != 0, false);
        }
        if (move == kDisappears) sync.forget(kPassing);
        if (move % 97 == 0) sync.hold_for_drag(kBeside);
        if (move % 97 == 40) sync.end_drag_hold(kBeside);
        if (move % 3 == 0) turn(loop);
    }
    check(run_until(loop, [&] { return kwin.loaded_now() == 1; }), "every placement is unloaded from KWin in the end");
    check(kwin.loads() > kMoves / 2, "the moves were placed through KWin");
}

}

int main() {
    a_report_waiting_when_its_placement_is_replaced();
    a_report_waiting_when_a_third_placement_is_made();
    replaced_placements_are_unloaded();
    a_stream_of_guest_moves();
    if (failures == 0) std::printf("kwin_placement: all passed\n");
    return failures == 0 ? 0 : 1;
}
