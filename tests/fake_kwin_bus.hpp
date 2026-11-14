#pragma once

#include <systemd/sd-bus.h>
#include <systemd/sd-id128.h>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace rwin::test {
class FakeKWinBus {
public:
    FakeKWinBus() {
        static int made = 0;
        const std::string name = "rwin-test-bus-" + std::to_string(::getpid()) + "-" + std::to_string(++made);
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

    void hold_reports(bool hold) { hold_ = hold; }

    void release() {
        on_bus([this] {
            for (const Report& report : held_) send(report);
            held_.clear();
        });
    }

    void report_again(std::size_t placement) {
        on_bus([this, placement] {
            if (placement < placed_.size()) send(placed_[placement]);
        });
    }

    [[nodiscard]] int loads() const { return loads_; }
    [[nodiscard]] int unloads() const { return unloads_; }
    [[nodiscard]] int orders() const { return orders_; }
    [[nodiscard]] int placed_reports() const { return reports_; }
    [[nodiscard]] int idle_answers() const { return idle_; }

    [[nodiscard]] std::string where(const std::string& tag) {
        const std::lock_guard lock{mutex_};
        const auto found = where_.find(tag);
        return found == where_.end() ? std::string() : found->second;
    }

private:
    struct Script {
        std::string name;
        bool watcher = false;
        bool started = false;
    };

    struct Report {
        const char* method;
        std::string text;
    };

    static constexpr const char* kPlacementPath = "/remotewin/placement";
    static constexpr const char* kPlacement = "org.remotewin.Placement";

    void on_bus(std::function<void()> job) {
        std::unique_lock lock{mutex_};
        job_ = std::move(job);
        done_.wait(lock, [this] { return !job_ || served_; });
        job_ = nullptr;
    }

    void run_job() {
        std::unique_lock lock{mutex_};
        if (!job_) return;
        const std::function<void()> job = std::exchange(job_, nullptr);
        lock.unlock();
        job();
        sd_bus_flush(bus_);
        lock.lock();
        done_.notify_all();
    }

    void send(const Report& report) {
        if (std::strcmp(report.method, "placed") == 0) ++reports_;
        sd_bus_call_method_async(bus_, nullptr, ":1.1", kPlacementPath, kPlacement, report.method, nullptr, nullptr,
                                 "s", report.text.c_str());
    }

    void ask() {
        sd_bus_call_method_async(bus_, nullptr, ":1.1", kPlacementPath, kPlacement, "next", &FakeKWinBus::ordered,
                                 this, "");
    }

    void carry(const std::string& order) {
        std::istringstream words{order};
        std::string verb;
        std::string tag;
        std::string serial;
        words >> verb >> tag >> serial;
        std::string x = "0";
        std::string y = "0";
        if (verb == "place") words >> x >> y;
        ++orders_;
        {
            const std::lock_guard lock{mutex_};
            where_[tag] = x + " " + y;
        }
        const Report moved{"moved", tag + " " + x + " " + y + " 800 600 0 0 0"};
        const Report placed{"placed", tag + " " + serial + " " + x + " " + y};
        placed_.push_back(placed);
        if (hold_) {
            held_.push_back(moved);
            held_.push_back(placed);
            return;
        }
        send(moved);
        send(placed);
    }

    static int ordered(sd_bus_message* reply, void* self, sd_bus_error*) {
        auto* kwin = static_cast<FakeKWinBus*>(self);
        const char* order = nullptr;
        if (sd_bus_message_is_method_error(reply, nullptr) != 0 || sd_bus_message_read(reply, "s", &order) < 0) return 0;
        if (*order == '\0') {
            ++kwin->idle_;
        } else {
            kwin->carry(order);
        }
        kwin->ask();
        return 0;
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
        const std::string source{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
        kwin->scripts_.push_back(Script{name, source.find("callDBus(service") != std::string::npos, false});
        return sd_bus_reply_method_return(call, "i", ++kwin->loads_);
    }

    static int start(sd_bus_message* call, void* self, sd_bus_error*) {
        auto* kwin = static_cast<FakeKWinBus*>(self);
        for (Script& script : kwin->scripts_) {
            if (std::exchange(script.started, true) || !script.watcher) continue;
            kwin->send(Report{"outputs", "A 0 0 1920 1080 1"});
            kwin->ask();
        }
        return sd_bus_reply_method_return(call, "");
    }

    static int unload_script(sd_bus_message* call, void* self, sd_bus_error*) {
        auto* kwin = static_cast<FakeKWinBus*>(self);
        const char* name = nullptr;
        if (sd_bus_message_read(call, "s", &name) < 0) return -EINVAL;
        std::erase_if(kwin->scripts_, [name](const Script& script) { return script.name == name; });
        ++kwin->unloads_;
        return sd_bus_reply_method_return(call, "b", 1);
    }

    void serve() {
        int client = -1;
        while (!stop_ && client < 0) {
            pollfd waiting{listener_, POLLIN, 0};
            if (::poll(&waiting, 1, 20) > 0) client = ::accept4(listener_, nullptr, nullptr, SOCK_CLOEXEC);
        }
        if (client >= 0) serve(client);
        const std::lock_guard lock{mutex_};
        served_ = true;
        done_.notify_all();
    }

    void serve(int client) {
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
            run_job();
            if (processed == 0) sd_bus_wait(bus_, 500);
        }
        sd_bus_close(bus_);
        sd_bus_unref(bus_);
    }

    int listener_ = -1;
    std::thread thread_;
    sd_bus* bus_ = nullptr;
    std::atomic<bool> stop_{false};
    std::atomic<bool> hold_{false};
    std::atomic<int> loads_{0};
    std::atomic<int> unloads_{0};
    std::atomic<int> orders_{0};
    std::atomic<int> reports_{0};
    std::atomic<int> idle_{0};
    std::mutex mutex_;
    std::condition_variable done_;
    std::function<void()> job_;
    bool served_ = false;
    std::map<std::string, std::string> where_;
    std::vector<Script> scripts_;
    std::vector<Report> held_;
    std::vector<Report> placed_;
};

}
