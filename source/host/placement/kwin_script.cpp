#include "host/placement/kwin_script.hpp"

#include "host/dbus/bus_connection.hpp"
#include "logging/logger.hpp"

#include <systemd/sd-bus.h>

#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace rwin::host {

namespace {

constexpr const char* kService = "org.kde.KWin";
constexpr const char* kScripting = "/Scripting";
constexpr const char* kScriptingInterface = "org.kde.kwin.Scripting";

std::string error_of(sd_bus_message* reply) {
    const sd_bus_error* error = sd_bus_message_get_error(reply);
    return error != nullptr && error->message != nullptr ? error->message : "no answer";
}

}  // namespace

KWinScript::KWinScript(BusConnection& bus, std::string name, const std::string& source, Done done)
    : bus_(bus), name_(std::move(name)), done_(std::move(done)) {
    std::string path = (std::filesystem::temp_directory_path() / "remotewin-XXXXXX.js").string();
    const int fd = ::mkstemps(path.data(), 3);
    if (fd < 0) {
        fail("cannot write a script file: " + std::string(std::strerror(errno)));
        return;
    }
    file_ = path;
    const bool written = ::write(fd, source.data(), source.size()) == static_cast<ssize_t>(source.size());
    ::close(fd);
    if (!written) {
        fail("cannot write " + file_);
        return;
    }
    const int asked = sd_bus_call_method_async(bus_.bus(), &call_, kService, kScripting, kScriptingInterface,
                                               "loadScript", &KWinScript::loaded, this, "ss", file_.c_str(),
                                               name_.c_str());
    if (asked < 0) {
        fail("loadScript: " + std::string(std::strerror(-asked)));
        return;
    }
    bus_.pump();
}

KWinScript::~KWinScript() {
    if (call_ != nullptr) sd_bus_slot_unref(call_);
    if (loaded_) {
        static_cast<void>(sd_bus_call_method_async(bus_.bus(), nullptr, kService, kScripting, kScriptingInterface,
                                                   "unloadScript", nullptr, nullptr, "s", name_.c_str()));
        bus_.pump();
    }
    if (!file_.empty()) ::unlink(file_.c_str());
}

bool KWinScript::reachable(BusConnection& bus) {
    if (!bus.connected()) return false;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    const int asked = sd_bus_call_method(bus.bus(), "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                         "org.freedesktop.DBus", "NameHasOwner", &error, &reply, "s", kService);
    int owned = 0;
    if (asked >= 0) static_cast<void>(sd_bus_message_read(reply, "b", &owned));
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return owned != 0;
}

int KWinScript::loaded(sd_bus_message* reply, void* self, sd_bus_error* /*error*/) {
    auto* script = static_cast<KWinScript*>(self);
    script->call_ = sd_bus_slot_unref(script->call_);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        script->fail("loadScript: " + error_of(reply));
        return 0;
    }
    std::int32_t id = -1;
    if (sd_bus_message_read(reply, "i", &id) < 0) {
        script->fail("loadScript gave no id");
        return 0;
    }
    script->loaded_ = true;
    if (id < 0) {
        script->fail("loadScript refused " + script->name_ + " (already loaded?)");
        return 0;
    }
    // Started through Scripting.start, which runs every script loaded and
    // not yet running, rather than through /Scripting/Script<id>: KWin
    // numbers a script by how many it has, so after an unload the id can
    // be a live script's, and its object path that script's - a run there
    // runs the other script, or fails on its file, and never this one.
    const int asked = sd_bus_call_method_async(script->bus_.bus(), &script->call_, kService, kScripting,
                                               kScriptingInterface, "start", &KWinScript::ran, script, "");
    if (asked < 0) script->fail("start: " + std::string(std::strerror(-asked)));
    return 0;
}

int KWinScript::ran(sd_bus_message* reply, void* self, sd_bus_error* /*error*/) {
    auto* script = static_cast<KWinScript*>(self);
    script->call_ = sd_bus_slot_unref(script->call_);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        script->fail("start: " + error_of(reply));
        return 0;
    }
    script->running_ = true;
    if (script->done_) script->done_(true, std::string());
    return 0;
}

void KWinScript::fail(const std::string& why) {
    RWIN_WARN("placement", "script {} did not run: {}", name_, why);
    if (done_) done_(false, why);
}

}  // namespace rwin::host
