// remotewin-wgcbench: how steadily Windows.Graphics.Capture hands over the
// guest's monitor, through the same capture the mirror runs (CaptureStream).
//
// A window that changes every vsync keeps DWM composing - the compositor
// hands over a frame only when the screen changed - and the monitor is
// captured for the given seconds. Each frame's SystemRelativeTime is when
// DWM showed it, so the spacing between them is the capture's own rhythm:
// at 60 Hz every gap is one vsync, and a freeze is a long one.
//
//   remotewin-wgcbench [seconds=10] [d3d|gdi] [monitor|window]
//
// The window draws through a Direct3D 11 flip swap chain (d3d, a game's
// path) or GDI (gdi, a desktop app's), each frame paced to DWM's own
// (DwmFlush), so what is measured is the capture and not a flood of
// presents.
//
// Prints a line a second and a summary; exits 0 when the capture kept up -
// at least 57 frames a second and no gap past 50 ms - and 1 when it did not.

#include "guest/capture/capture_clock.hpp"
#include "guest/capture/capture_closer.hpp"
#include "guest/capture/capture_stream.hpp"
#include "guest/capture/frame_signal.hpp"
#include "guest/capture/graphics_device.hpp"

// clang-format off
#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <roapi.h>
// clang-format on

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

using rwin::guest::CaptureStream;
using rwin::guest::FrameSignal;
using rwin::guest::GraphicsDevice;

constexpr double kTicksPerMs = 10000.0;  // SystemRelativeTime is in 100 ns
constexpr double kPassFps = 57.0;
constexpr double kFrameMs = 1000.0 / 60.0;
// Frames arrive on the display's refresh, so a gap is a whole number of its
// intervals plus the clock's jitter: three intervals measure 49.98 ms or
// 50.02 ms alike. A gap is judged by the intervals it spans.
constexpr long long kPassGapFrames = 3;

long long frames_spanned(double gap_ms) { return std::llround(gap_ms / kFrameMs); }

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_DESTROY) {
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

HWND make_window() {
    WNDCLASSW kind{};
    kind.lpfnWndProc = window_proc;
    kind.hInstance = ::GetModuleHandleW(nullptr);
    kind.lpszClassName = L"rwin-wgcbench";
    kind.hCursor = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    ::RegisterClassW(&kind);
    HWND window = ::CreateWindowExW(WS_EX_TOPMOST, kind.lpszClassName, L"WGC benchmark",
                                    WS_OVERLAPPEDWINDOW | WS_VISIBLE, 200, 200, 640, 360, nullptr,
                                    nullptr, kind.hInstance, nullptr);
    return window;
}

IDXGISwapChain1* make_swap_chain(ID3D11Device* device, HWND window) {
    IDXGIDevice* dxgi = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    IDXGISwapChain1* chain = nullptr;
    if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi))) &&
        SUCCEEDED(dxgi->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory)))) {
        DXGI_ADAPTER_DESC described{};
        adapter->GetDesc(&described);
        std::printf("adapter: %ls (vendor %04x)\n", described.Description,
                    static_cast<unsigned>(described.VendorId));
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(factory->CreateSwapChainForHwnd(device, window, &desc, nullptr, nullptr, &chain)))
            chain = nullptr;
    }
    if (factory != nullptr) factory->Release();
    if (adapter != nullptr) adapter->Release();
    if (dxgi != nullptr) dxgi->Release();
    return chain;
}

// One frame drawn: the whole window a colour that cycles, so every vsync
// is a change DWM composes.
void draw(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain1* chain,
          std::uint64_t frame) {
    ID3D11Texture2D* buffer = nullptr;
    if (FAILED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer))))
        return;
    ID3D11RenderTargetView* target = nullptr;
    if (SUCCEEDED(device->CreateRenderTargetView(buffer, nullptr, &target))) {
        const float phase = static_cast<float>(frame % 120) / 120.0f;
        const float colour[4] = {phase, 1.0f - phase, 0.5f, 1.0f};
        context->ClearRenderTargetView(target, colour);
        target->Release();
    }
    buffer->Release();
    chain->Present(1, 0);
}

void draw_gdi(HWND window, std::uint64_t frame) {
    HDC dc = ::GetDC(window);
    RECT area{};
    ::GetClientRect(window, &area);
    const auto phase = static_cast<BYTE>((frame % 120) * 2);
    HBRUSH brush = ::CreateSolidBrush(RGB(phase, 255 - phase, 128));
    ::FillRect(dc, &area, brush);
    ::DeleteObject(brush);
    ::ReleaseDC(window, dc);
    ::GdiFlush();
}

// One BGRA pixel of a captured frame, read through a staging texture: what
// tells a frame that holds the window's picture from one that lost it (an
// overlay plane the capture left out shows the desktop behind instead).
struct Pixel {
    bool read = false;
    std::uint8_t blue = 0, green = 0, red = 0;
};

Pixel read_pixel(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture, std::uint32_t x,
                 std::uint32_t y) {
    Pixel pixel;
    D3D11_TEXTURE2D_DESC described{};
    texture->GetDesc(&described);
    if (x >= described.Width || y >= described.Height) return pixel;
    D3D11_TEXTURE2D_DESC staging_desc{};
    staging_desc.Width = 1;
    staging_desc.Height = 1;
    staging_desc.MipLevels = 1;
    staging_desc.ArraySize = 1;
    staging_desc.Format = described.Format;
    staging_desc.SampleDesc.Count = 1;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(device->CreateTexture2D(&staging_desc, nullptr, &staging))) return pixel;
    const D3D11_BOX box{x, y, 0, x + 1, y + 1, 1};
    context->CopySubresourceRegion(staging, 0, 0, 0, 0, texture, 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        const auto* bytes = static_cast<const std::uint8_t*>(mapped.pData);
        pixel.read = true;
        pixel.blue = bytes[0];
        pixel.green = bytes[1];
        pixel.red = bytes[2];
        context->Unmap(staging, 0);
    }
    staging->Release();
    return pixel;
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t at = static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1));
    return values[at];
}

}  // namespace

int main(int argc, char** argv) {
    const int seconds = argc > 1 ? std::max(1, std::atoi(argv[1])) : 10;
    const bool gdi = argc > 2 && std::strcmp(argv[2], "gdi") == 0;
    const bool whole_window = argc > 3 && std::strcmp(argv[3], "window") == 0;
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    static_cast<void>(::RoInitialize(RO_INIT_MULTITHREADED));
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    GraphicsDevice device;
    if (!device.valid()) {
        std::printf("no Direct3D 11 device\n");
        return 2;
    }
    HWND window = make_window();
    IDXGISwapChain1* chain = window != nullptr && !gdi ? make_swap_chain(device.device(), window) : nullptr;
    if (window == nullptr || (!gdi && chain == nullptr)) {
        std::printf("no swap chain for the animated window\n");
        return 2;
    }

    HMONITOR monitor = ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof info;
    ::GetMonitorInfoW(monitor, &info);
    const CaptureStream::Extent size{static_cast<std::uint32_t>(info.rcMonitor.right - info.rcMonitor.left),
                                     static_cast<std::uint32_t>(info.rcMonitor.bottom - info.rcMonitor.top)};
    FrameSignal arrivals;
    CaptureCloser closer;
    CaptureStream stream(device, arrivals, closer);
    RECT frame{};
    ::GetClientRect(window, &frame);
    const CaptureStream::Extent window_size{static_cast<std::uint32_t>(frame.right),
                                            static_cast<std::uint32_t>(frame.bottom)};
    const bool opened = whole_window ? stream.open_window(window, window_size) : stream.open_monitor(monitor, size);
    if (!opened) {
        std::printf("the compositor will not capture the %s\n", whole_window ? "window" : "monitor");
        return 2;
    }
    const CaptureStream::Extent shown = whole_window ? window_size : size;
    std::printf("capturing the %s, %ux%u, for %d s, the window drawn by %s\n", whole_window ? "window" : "monitor",
                shown.width, shown.height, seconds, gdi ? "GDI" : "Direct3D 11");

    std::vector<double> gaps_ms;
    Pixel last_pixel;
    std::int64_t last_source = 0;
    std::uint64_t frames = 0, drawn = 0, second_frames = 0;
    double second_worst = 0.0;
    const std::int64_t start = rwin::guest::capture_clock_now();
    std::int64_t second_start = start;
    const std::int64_t end = start + static_cast<std::int64_t>(seconds) * 10000000;
    bool open = true;
    while (open && rwin::guest::capture_clock_now() < end) {
        MSG message;
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) open = false;
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        if (gdi) draw_gdi(window, ++drawn);
        else draw(device.device(), device.context(), chain, ++drawn);
        static_cast<void>(::DwmFlush());
        while (auto frame = stream.next_frame()) {
            const std::int64_t source = frame->source_time();
            if (last_source != 0 && source > last_source) {
                const double gap = static_cast<double>(source - last_source) / kTicksPerMs;
                gaps_ms.push_back(gap);
                second_worst = std::max(second_worst, gap);
            }
            if (source > last_source) last_source = source;
            // The window's middle, in the captured picture's coordinates.
            if (!gdi) {
                RECT placed{};
                ::GetWindowRect(window, &placed);
                const std::uint32_t x = whole_window ? frame->content().width / 2
                                                     : static_cast<std::uint32_t>((placed.left + placed.right) / 2);
                const std::uint32_t y = whole_window ? frame->content().height / 2
                                                     : static_cast<std::uint32_t>((placed.top + placed.bottom) / 2);
                last_pixel = read_pixel(device.device(), device.context(), frame->texture(), x, y);
            }
            ++frames;
            ++second_frames;
        }
        const std::int64_t now = rwin::guest::capture_clock_now();
        if (now - second_start >= 10000000) {
            std::printf("  %5.1f s  %3llu frames  worst gap %6.1f ms\n",
                        static_cast<double>(now - start) / 1e7,
                        static_cast<unsigned long long>(second_frames), second_worst);
            second_start = now;
            second_frames = 0;
            second_worst = 0.0;
        }
    }
    const double elapsed = static_cast<double>(rwin::guest::capture_clock_now() - start) / 1e7;
    stream.close();
    if (chain != nullptr) chain->Release();
    ::DestroyWindow(window);

    const double fps = static_cast<double>(frames) / elapsed;
    const double worst = gaps_ms.empty() ? 0.0 : *std::max_element(gaps_ms.begin(), gaps_ms.end());
    const auto late = std::count_if(gaps_ms.begin(), gaps_ms.end(), [](double gap) { return gap > 20.0; });
    const auto stalls = std::count_if(gaps_ms.begin(), gaps_ms.end(),
                                      [](double gap) { return frames_spanned(gap) > kPassGapFrames; });
    std::printf("frames %llu in %.2f s: %.1f fps (drawn %.1f fps)\n", static_cast<unsigned long long>(frames),
                elapsed, fps, static_cast<double>(drawn) / elapsed);
    std::printf("gap ms: p50 %.1f  p95 %.1f  p99 %.1f  worst %.1f;  >20 ms %lld,  >3 frames %lld\n",
                percentile(gaps_ms, 0.50), percentile(gaps_ms, 0.95), percentile(gaps_ms, 0.99), worst,
                static_cast<long long>(late), static_cast<long long>(stalls));
    // The window draws blue 128 and red and green that add up to 255, all
    // through its cycle; a frame without the window's picture shows neither.
    const bool pictured = gdi || (last_pixel.read && last_pixel.blue >= 120 && last_pixel.blue <= 136 &&
                                  last_pixel.red + last_pixel.green >= 245 && last_pixel.red + last_pixel.green <= 265);
    if (!gdi)
        std::printf("window pixel in the last frame: %s r %u g %u b %u\n", pictured ? "the window's" : "NOT the window's",
                    last_pixel.red, last_pixel.green, last_pixel.blue);
    const bool kept_up = fps >= kPassFps && frames_spanned(worst) <= kPassGapFrames;
    std::printf("%s\n", kept_up && pictured ? "PASS" : "FAIL");
    return kept_up && pictured ? 0 : 1;
}
