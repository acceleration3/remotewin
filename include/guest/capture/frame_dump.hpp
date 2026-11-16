#pragma once

#include "guest/capture/capture_stream.hpp"
#include "proto/window_id.hpp"

#include <cstdint>
#include <filesystem>
#include <set>

namespace rwin::guest {

class GraphicsDevice;
class WindowSurface;

class FrameDump {
public:
    explicit FrameDump(const GraphicsDevice& device) noexcept : device_(device) {}

    void ask(WindowId window);
    void write_if_asked(WindowId window, const CaptureStream::Frame& frame, const WindowSurface& surface);

private:
    [[nodiscard]] bool write_texture(const CaptureStream::Frame& frame, const std::filesystem::path& to) const;

    const GraphicsDevice& device_;
    std::set<std::uint64_t> asked_;
    std::uint32_t written_ = 0;
};

}
