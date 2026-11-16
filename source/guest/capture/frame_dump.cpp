#include "guest/capture/frame_dump.hpp"

#include "guest/capture/graphics_device.hpp"
#include "guest/windows/window_surface.hpp"
#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
// clang-format on

#include <array>
#include <format>
#include <fstream>
#include <string>

namespace rwin::guest {

namespace {

constexpr std::uint32_t kBytesPerPixel = 4;

std::string rect_text(const RECT& rect) {
    return std::format("{},{} {}x{}", rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}

std::filesystem::path dump_directory() {
    std::array<wchar_t, MAX_PATH + 1> temp{};
    const DWORD length = ::GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
    if (length == 0 || length > temp.size()) return std::filesystem::path{L"C:\\"};
    return std::filesystem::path{std::wstring{temp.data(), length}};
}

void write_geometry(const CaptureStream::Frame& frame, const WindowSurface& surface, const std::filesystem::path& to) {
    const auto window = static_cast<HWND>(surface.handle());
    D3D11_TEXTURE2D_DESC described{};
    frame.texture()->GetDesc(&described);
    RECT window_rect{};
    ::GetWindowRect(window, &window_rect);
    RECT bounds{};
    const HRESULT bounded =
        ::DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds));
    RECT client{};
    ::GetClientRect(window, &client);
    POINT client_corner{};
    ::ClientToScreen(window, &client_corner);
    std::ofstream out{to};
    out << std::format("content {}x{}\n", frame.content().width, frame.content().height);
    out << std::format("texture {}x{}\n", described.Width, described.Height);
    out << std::format("window_rect {}\n", rect_text(window_rect));
    out << std::format("frame_bounds {}{}\n", rect_text(bounds), SUCCEEDED(bounded) ? "" : " (unknown)");
    out << std::format("client {},{} {}x{}\n", client_corner.x, client_corner.y, client.right, client.bottom);
    out << std::format("zoomed {} iconic {} dpi {}\n", ::IsZoomed(window) != FALSE, ::IsIconic(window) != FALSE,
                       ::GetDpiForWindow(window));
    out << std::format("surface {}x{} inset {},{} border {}x{} margin_reserved {}\n", surface.width(),
                       surface.height(), surface.inset_x(), surface.inset_y(), surface.border_width(),
                       surface.border_height(), surface.margin_is_reserved());
}

void write_bitmap_header(std::ofstream& out, std::uint32_t width, std::uint32_t height) {
    const std::uint32_t pixels = width * height * kBytesPerPixel;
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + pixels;
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(BITMAPINFOHEADER);
    info.biWidth = static_cast<LONG>(width);
    info.biHeight = -static_cast<LONG>(height);
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    info.biSizeImage = pixels;
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));
}

}

void FrameDump::ask(WindowId window) { asked_.insert(window.key()); }

void FrameDump::write_if_asked(WindowId window, const CaptureStream::Frame& frame, const WindowSurface& surface) {
    if (asked_.erase(window.key()) == 0) return;
    const std::filesystem::path base =
        dump_directory() / std::format("rwin-frame-{:x}-{}-{}", window.hwnd, window.serial, written_++);
    std::filesystem::path picture = base;
    picture += ".bmp";
    std::filesystem::path geometry = base;
    geometry += ".txt";
    write_geometry(frame, surface, geometry);
    if (!write_texture(frame, picture)) return;
    RWIN_INFO("dump", "{}: frame written to {}", window, picture.string());
}

bool FrameDump::write_texture(const CaptureStream::Frame& frame, const std::filesystem::path& to) const {
    D3D11_TEXTURE2D_DESC described{};
    frame.texture()->GetDesc(&described);
    D3D11_TEXTURE2D_DESC wanted = described;
    wanted.Usage = D3D11_USAGE_STAGING;
    wanted.BindFlags = 0;
    wanted.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wanted.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    const HRESULT made = device_.device()->CreateTexture2D(&wanted, nullptr, &staging);
    if (FAILED(made) || staging == nullptr) {
        RWIN_WARN("dump", "no {}x{} staging texture for {}: {:#010x}", described.Width, described.Height,
                  to.string(), static_cast<unsigned>(made));
        return false;
    }
    device_.context()->CopyResource(staging, frame.texture());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT opened = device_.context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(opened)) {
        staging->Release();
        RWIN_WARN("dump", "the staging texture for {} would not map: {:#010x}", to.string(),
                  static_cast<unsigned>(opened));
        return false;
    }
    std::ofstream out{to, std::ios::binary};
    write_bitmap_header(out, described.Width, described.Height);
    const auto* rows = static_cast<const char*>(mapped.pData);
    for (std::uint32_t y = 0; y < described.Height; ++y) {
        out.write(rows + static_cast<std::size_t>(y) * mapped.RowPitch,
                  static_cast<std::streamsize>(described.Width) * kBytesPerPixel);
    }
    device_.context()->Unmap(staging, 0);
    staging->Release();
    if (!out) {
        RWIN_WARN("dump", "{} could not be written", to.string());
        return false;
    }
    return true;
}

}
