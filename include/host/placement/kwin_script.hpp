#pragma once

#include <functional>
#include <string>

struct sd_bus_error;
struct sd_bus_message;
struct sd_bus_slot;

namespace rwin::host {

class BusConnection;

class KWinScript {
public:
    using Done = std::function<void(bool ran, const std::string& error)>;

    KWinScript(BusConnection& bus, std::string name, const std::string& source, Done done);
    ~KWinScript();

    KWinScript(const KWinScript&) = delete;
    KWinScript& operator=(const KWinScript&) = delete;
    KWinScript(KWinScript&&) = delete;
    KWinScript& operator=(KWinScript&&) = delete;

    [[nodiscard]] bool running() const noexcept { return running_; }

    [[nodiscard]] static bool reachable(BusConnection& bus);

private:
    static int loaded(sd_bus_message* reply, void* self, sd_bus_error* error);
    static int ran(sd_bus_message* reply, void* self, sd_bus_error* error);
    void fail(const std::string& why);

    BusConnection& bus_;
    std::string name_;
    std::string file_;
    Done done_;
    sd_bus_slot* call_ = nullptr;
    bool loaded_ = false;
    bool running_ = false;
};

}
