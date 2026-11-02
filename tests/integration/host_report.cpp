#include "host_report.hpp"

#include "host/dbus/bus_connection.hpp"

#include <systemd/sd-bus.h>

#include <charconv>
#include <cstdlib>
#include <string>
#include <string_view>

namespace rwin::integration {

namespace {

constexpr const char* kPath = "/org/remotewin/Windows";
constexpr const char* kInterface = "org.remotewin.Windows";
constexpr std::string_view kPictureKey = "\"picture\":[";
constexpr std::string_view kRoleKey = "\"role\":\"";
constexpr std::string_view kTitleKey = "\"title\":\"";
constexpr std::string_view kLastMirrorKey = "\"surfaces_made\"";

int number_at(std::string_view text, std::size_t& at) {
    int value = 0;
    const auto [end, error] = std::from_chars(text.data() + at, text.data() + text.size(), value);
    at = static_cast<std::size_t>(end - text.data()) + 1;
    return error == std::errc{} ? value : 0;
}

}

HostReport::HostReport(host::BusConnection& bus, pid_t host) : bus_(bus), host_(host) {}

std::string HostReport::find_name() {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    std::string found;
    if (sd_bus_call_method(bus_.bus(), "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                           "ListNames", &error, &reply, "")
        >= 0) {
        char** names = nullptr;
        if (sd_bus_message_read_strv(reply, &names) >= 0 && names != nullptr) {
            for (char** name = names; *name != nullptr; ++name) {
                sd_bus_creds* creds = nullptr;
                pid_t pid = 0;
                if (found.empty() && (*name)[0] == ':'
                    && sd_bus_get_name_creds(bus_.bus(), *name, SD_BUS_CREDS_PID, &creds) >= 0
                    && sd_bus_creds_get_pid(creds, &pid) >= 0 && pid == host_) {
                    found = *name;
                }
                sd_bus_creds_unref(creds);
                std::free(*name);
            }
            std::free(names);
        }
    }
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return found;
}

std::optional<std::string> HostReport::describe() {
    if (name_.empty()) name_ = find_name();
    if (name_.empty()) return std::nullopt;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    std::optional<std::string> report;
    if (sd_bus_call_method(bus_.bus(), name_.c_str(), kPath, kInterface, "Describe", &error, &reply, "") >= 0) {
        const char* text = nullptr;
        if (sd_bus_message_read(reply, "s", &text) >= 0 && text != nullptr) report = std::string(text);
    }
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return report;
}

std::optional<HostReport::Picture> HostReport::picture() {
    const std::optional<std::string> report = describe();
    if (!report.has_value()) return std::nullopt;
    const std::size_t key = report->find(kPictureKey);
    if (key == std::string::npos) return std::nullopt;
    std::size_t at = key + kPictureKey.size();
    number_at(*report, at);
    number_at(*report, at);
    const int width = number_at(*report, at);
    const int height = number_at(*report, at);
    return Picture{width, height};
}

std::optional<HostReport::Mirror> HostReport::mirror(std::string_view title) {
    const std::optional<std::string> report = describe();
    if (!report.has_value()) return std::nullopt;
    const std::string named = std::string(kTitleKey) + std::string(title) + "\"";
    for (std::size_t role = report->find(kRoleKey); role != std::string::npos;) {
        const std::size_t last = report->find(kLastMirrorKey, role);
        const std::string_view object = std::string_view(*report).substr(role, last - role);
        role = report->find(kRoleKey, role + kRoleKey.size());
        if (object.find(named) == std::string_view::npos) continue;
        Mirror mirror;
        const std::size_t kind = kRoleKey.size();
        mirror.role = std::string(object.substr(kind, object.find('"', kind) - kind));
        mirror.shown = object.find("\"shown\":true") != std::string_view::npos;
        mirror.mapped = object.find("\"mapped\":true") != std::string_view::npos;
        mirror.on_gpu = object.find("\"on_gpu\":true") != std::string_view::npos;
        if (const std::size_t key = object.find(kPictureKey); key != std::string_view::npos) {
            std::size_t at = key + kPictureKey.size();
            number_at(object, at);
            number_at(object, at);
            mirror.picture.width = number_at(object, at);
            mirror.picture.height = number_at(object, at);
        }
        return mirror;
    }
    return std::nullopt;
}

}
