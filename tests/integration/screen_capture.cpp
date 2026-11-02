#include "screen_capture.hpp"

#include "host/dbus/bus_connection.hpp"
#include "host/export/unique_fd.hpp"

#include <systemd/sd-bus.h>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace rwin::integration {

namespace {
constexpr std::uint32_t kRgb32 = 4;
constexpr std::uint32_t kArgb32 = 5;
constexpr std::uint32_t kArgb32Premultiplied = 6;
constexpr std::uint32_t kRgbx8888 = 16;
constexpr std::uint32_t kRgba8888 = 17;
constexpr std::uint32_t kRgba8888Premultiplied = 18;

struct Described {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    std::uint32_t format = 0;
};

bool read_described(sd_bus_message* reply, Described& described) {
    if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{sv}") < 0) return false;
    while (sd_bus_message_enter_container(reply, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
        const char* key = nullptr;
        if (sd_bus_message_read(reply, "s", &key) < 0) return false;
        char type = 0;
        const char* contents = nullptr;
        if (sd_bus_message_peek_type(reply, &type, &contents) < 0 || contents == nullptr) return false;
        const std::string_view name{key};
        std::uint32_t* into = name == "width"    ? &described.width
                              : name == "height" ? &described.height
                              : name == "stride" ? &described.stride
                              : name == "format" ? &described.format
                                                 : nullptr;
        if (into != nullptr && std::string_view{contents} == "u") {
            if (sd_bus_message_read(reply, "v", "u", into) < 0) return false;
        } else if (sd_bus_message_skip(reply, "v") < 0) {
            return false;
        }
        if (sd_bus_message_exit_container(reply) < 0) return false;
    }
    return sd_bus_message_exit_container(reply) >= 0;
}

std::uint32_t pixel_of(const std::uint8_t* bytes, std::uint32_t format) {
    const std::uint32_t alpha = format == kRgbx8888 || format == kRgb32 ? 0xFF : 0;
    if (format == kRgbx8888 || format == kRgba8888 || format == kRgba8888Premultiplied) {
        return (alpha != 0 ? alpha : bytes[3]) << 24 | std::uint32_t{bytes[0]} << 16 | std::uint32_t{bytes[1]} << 8
               | bytes[2];
    }
    return (alpha != 0 ? alpha : bytes[3]) << 24 | std::uint32_t{bytes[2]} << 16 | std::uint32_t{bytes[1]} << 8
           | bytes[0];
}

bool known(std::uint32_t format) {
    return format == kRgb32 || format == kArgb32 || format == kArgb32Premultiplied || format == kRgbx8888
           || format == kRgba8888 || format == kRgba8888Premultiplied;
}

}

std::uint32_t ScreenCapture::Picture::at(int x, int y) const noexcept {
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    return pixels[static_cast<std::size_t>(y) * width + x];
}

std::optional<ScreenCapture::Picture> ScreenCapture::area(int x, int y, int width, int height) {
    int ends[2] = {-1, -1};
    if (::pipe2(ends, O_CLOEXEC) != 0) return std::nullopt;
    const host::UniqueFd reading{ends[0]};
    host::UniqueFd writing{ends[1]};
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    Described described;
    const int called = sd_bus_call_method(bus_.bus(), "org.kde.KWin", "/org/kde/KWin/ScreenShot2",
                                          "org.kde.KWin.ScreenShot2", "CaptureArea", &error, &reply, "iiuua{sv}h", x,
                                          y, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1,
                                          "native-resolution", "b", 1, writing.get());
    writing = host::UniqueFd{};
    const bool answered = called >= 0 && read_described(reply, described);
    if (called < 0) std::fprintf(stderr, "screen: KWin will not capture: %s\n", error.message);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    if (!answered || !known(described.format) || described.stride < described.width * 4) return std::nullopt;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(described.stride) * described.height);
    std::size_t got = 0;
    while (got < bytes.size()) {
        const ssize_t read = ::read(reading.get(), bytes.data() + got, bytes.size() - got);
        if (read < 0 && errno == EINTR) continue;
        if (read <= 0) break;
        got += static_cast<std::size_t>(read);
    }
    if (got < bytes.size()) return std::nullopt;
    Picture picture{static_cast<int>(described.width), static_cast<int>(described.height), {}};
    picture.pixels.reserve(static_cast<std::size_t>(described.width) * described.height);
    for (std::uint32_t row = 0; row < described.height; ++row) {
        for (std::uint32_t column = 0; column < described.width; ++column) {
            picture.pixels.push_back(
                pixel_of(bytes.data() + static_cast<std::size_t>(row) * described.stride + column * 4, described.format));
        }
    }
    return picture;
}

}
