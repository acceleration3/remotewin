#include "guest/capture/capture_stream.hpp"

#include "guest/capture/capture_clock.hpp"
#include "guest/capture/capture_closer.hpp"
#include "guest/capture/frame_signal.hpp"
#include "guest/capture/graphics_device.hpp"
#include "guest/capture/winrt_close.hpp"
#include "logging/logger.hpp"

#define ____FIReference_1_boolean_INTERFACE_DEFINED__

// clang-format off
#include <windows.h>
#include <d3d11.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
// clang-format on

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <utility>

namespace capture_abi = ABI::Windows::Graphics::Capture;
using capture_abi::IDirect3D11CaptureFrame;
using capture_abi::IDirect3D11CaptureFramePool;
using capture_abi::IDirect3D11CaptureFramePoolStatics2;
using capture_abi::IGraphicsCaptureItem;
using capture_abi::IGraphicsCaptureSession;
using capture_abi::IGraphicsCaptureSession2;
using capture_abi::IGraphicsCaptureSession3;
using FrameArrived = __FITypedEventHandler_2_Windows__CGraphics__CCapture__CDirect3D11CaptureFramePool_IInspectable;

MIDL_INTERFACE("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")
IDirect3DDxgiInterfaceAccess : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID iid, void** object) = 0;
};
__CRT_UUID_DECL(IDirect3DDxgiInterfaceAccess, 0xA9B3D012, 0x3DF2, 0x4EE3, 0xB8, 0xD1,
                0x86, 0x95, 0xF4, 0x57, 0xD3, 0xC1)

struct CaptureInterval : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_MinUpdateInterval(INT64* hundred_ns) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_MinUpdateInterval(INT64 hundred_ns) = 0;
};

namespace rwin::guest {

namespace {

constexpr std::size_t kBytesPerPixel = 4;
constexpr int kPoolBuffers = 3;
constexpr int kBgra8 = 87;

void allow_every_composition(IGraphicsCaptureSession* session) {
    constexpr IID kSession5{0x67c0ea62, 0x1f85, 0x5061,
                           {0x92, 0x5a, 0x23, 0x9b, 0xe0, 0xac, 0x09, 0xcb}};
    CaptureInterval* interval = nullptr;
    if (FAILED(session->QueryInterface(kSession5, reinterpret_cast<void**>(&interval)))) {
        RWIN_WARN("capture", "this Windows build retains WGC's default capture throttle");
        return;
    }
    const HRESULT changed = interval->put_MinUpdateInterval(0);
    interval->Release();
    if (FAILED(changed))
        RWIN_WARN("capture", "cannot remove WGC's capture throttle: {:#010x}",
                  static_cast<unsigned>(changed));
}

template <typename T>
HRESULT activation_factory(const wchar_t* runtime_class, T** out) {
    HSTRING_HEADER header{};
    HSTRING name = nullptr;
    const HRESULT named = ::WindowsCreateStringReference(
        runtime_class, static_cast<UINT32>(::wcslen(runtime_class)), &header, &name);
    if (FAILED(named)) return named;
    return ::RoGetActivationFactory(name, __uuidof(T), reinterpret_cast<void**>(out));
}

void release(void** object) {
    if (*object != nullptr) {
        static_cast<IUnknown*>(*object)->Release();
        *object = nullptr;
    }
}

class FrameArrivedHandler final : public FrameArrived {
public:
    explicit FrameArrivedHandler(const rwin::guest::FrameSignal& arrivals) noexcept : arrivals_(arrivals) {}

    FrameArrivedHandler(const FrameArrivedHandler&) = delete;
    FrameArrivedHandler& operator=(const FrameArrivedHandler&) = delete;
    FrameArrivedHandler(FrameArrivedHandler&&) = delete;
    FrameArrivedHandler& operator=(FrameArrivedHandler&&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (out == nullptr) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(FrameArrived) || iid == __uuidof(IAgileObject)) {
            *out = static_cast<FrameArrived*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --references_;
        if (left == 0) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IDirect3D11CaptureFramePool* , IInspectable* ) override {
        arrivals_.raise();
        return S_OK;
    }

private:
    ~FrameArrivedHandler() = default;

    const rwin::guest::FrameSignal& arrivals_;
    std::atomic<ULONG> references_{1};
};

IGraphicsCaptureItemInterop* item_factory() {
    IGraphicsCaptureItemInterop* interop = nullptr;
    if (FAILED(activation_factory(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem,
                                  &interop))) {
        RWIN_ERROR("capture", "no capture item factory");
        return nullptr;
    }
    return interop;
}

}

CaptureStream::CaptureStream(const GraphicsDevice& device, const FrameSignal& arrivals, CaptureCloser& closer)
    : device_(device), arrivals_(arrivals), closer_(closer) {
    sample_pixels_ = std::getenv("RWIN_TIMING_PIXELS") != nullptr;
}

CaptureStream::~CaptureStream() { close(); }

void CaptureStream::close_capture() {
    if (pool_ != nullptr && arrival_token_.has_value()) {
        static_cast<IDirect3D11CaptureFramePool*>(pool_)->remove_FrameArrived(EventRegistrationToken{*arrival_token_});
    }
    arrival_token_.reset();
    void* const closing[] = {std::exchange(session_, nullptr), std::exchange(pool_, nullptr)};
    closer_.close(closing);
}

void CaptureStream::close() {
    close_capture();
    release(&item_);
    release(&staging_);
    release(&sample_);
    size_ = {};
}

bool CaptureStream::open_window(void* window, Extent size) {
    close();
    if (!device_.valid()) return false;
    IGraphicsCaptureItemInterop* interop = item_factory();
    if (interop == nullptr) return false;
    void* item = nullptr;
    const HRESULT made =
        interop->CreateForWindow(static_cast<HWND>(window), __uuidof(IGraphicsCaptureItem), &item);
    interop->Release();
    if (FAILED(made) || item == nullptr) {
        RWIN_WARN("capture", "cannot capture this window: {:#010x}", static_cast<unsigned>(made));
        return false;
    }
    return open(item, size);
}

bool CaptureStream::open_monitor(void* monitor, Extent size) {
    close();
    if (!device_.valid()) return false;
    IGraphicsCaptureItemInterop* interop = item_factory();
    if (interop == nullptr) return false;
    void* item = nullptr;
    const HRESULT made = interop->CreateForMonitor(static_cast<HMONITOR>(monitor),
                                                   __uuidof(IGraphicsCaptureItem), &item);
    interop->Release();
    if (FAILED(made) || item == nullptr) {
        RWIN_WARN("capture", "cannot capture this monitor: {:#010x}", static_cast<unsigned>(made));
        return false;
    }
    return open(item, size);
}

bool CaptureStream::open(void* item, Extent size) {
    item_ = item;
    if (start(size)) return true;
    close();
    return false;
}

bool CaptureStream::resize(Extent size) {
    if (pool_ != nullptr && session_ != nullptr && recreate(size)) return true;
    return start(size);
}

bool CaptureStream::capture_afresh(Extent size) { return start(size); }

bool CaptureStream::recreate(Extent size) {
    ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice* wrapped = nullptr;
    if (FAILED(device_.winrt_device()->QueryInterface(
            __uuidof(ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice),
            reinterpret_cast<void**>(&wrapped)))) {
        return false;
    }
    const ABI::Windows::Graphics::SizeInt32 pool_size{static_cast<INT32>(size.width),
                                                      static_cast<INT32>(size.height)};
    const HRESULT recreated = static_cast<IDirect3D11CaptureFramePool*>(pool_)->Recreate(
        wrapped, static_cast<ABI::Windows::Graphics::DirectX::DirectXPixelFormat>(kBgra8), kPoolBuffers,
        pool_size);
    wrapped->Release();
    if (FAILED(recreated)) {
        RWIN_WARN("capture", "the frame pool would not be made {}x{} again: {:#010x}; capturing afresh", size.width,
                  size.height, static_cast<unsigned>(recreated));
        return false;
    }
    size_ = size;
    outgrown_.reset();
    RWIN_DEBUG("capture", "capturing {}x{} in the same session", size_.width, size_.height);
    return true;
}

bool CaptureStream::start(Extent size) {
    IDirect3D11CaptureFramePoolStatics2* statics = nullptr;
    if (FAILED(activation_factory(
            RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool,
            &statics))) {
        return false;
    }
    const ABI::Windows::Graphics::SizeInt32 pool_size{static_cast<INT32>(size.width),
                                                      static_cast<INT32>(size.height)};

    close_capture();

    ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice* wrapped = nullptr;
    if (FAILED(device_.winrt_device()->QueryInterface(
            __uuidof(ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice),
            reinterpret_cast<void**>(&wrapped)))) {
        statics->Release();
        RWIN_ERROR("capture", "the wrapped device is not a Direct3D device");
        return false;
    }

    const HRESULT created = statics->CreateFreeThreaded(
        wrapped,
        static_cast<ABI::Windows::Graphics::DirectX::DirectXPixelFormat>(kBgra8),
        kPoolBuffers, pool_size, reinterpret_cast<IDirect3D11CaptureFramePool**>(&pool_));
    wrapped->Release();
    statics->Release();
    if (FAILED(created) || pool_ == nullptr) {
        RWIN_ERROR("capture", "no frame pool: {:#010x}", static_cast<unsigned>(created));
        return false;
    }
    auto* const handler = new FrameArrivedHandler(arrivals_);
    EventRegistrationToken token{};
    const HRESULT registered = static_cast<IDirect3D11CaptureFramePool*>(pool_)->add_FrameArrived(handler, &token);
    handler->Release();
    if (FAILED(registered)) {
        RWIN_ERROR("capture", "the frame pool will not say when a frame arrives: {:#010x}",
                   static_cast<unsigned>(registered));
        return false;
    }
    arrival_token_ = token.value;
    if (FAILED(static_cast<IDirect3D11CaptureFramePool*>(pool_)->CreateCaptureSession(
            static_cast<IGraphicsCaptureItem*>(item_),
            reinterpret_cast<IGraphicsCaptureSession**>(&session_)))
        || session_ == nullptr) {
        RWIN_ERROR("capture", "no capture session");
        return false;
    }

    IGraphicsCaptureSession2* pointer = nullptr;
    if (SUCCEEDED(static_cast<IGraphicsCaptureSession*>(session_)->QueryInterface(
            __uuidof(IGraphicsCaptureSession2), reinterpret_cast<void**>(&pointer)))) {
        pointer->put_IsCursorCaptureEnabled(false);
        pointer->Release();
    } else {
        RWIN_WARN("capture", "this build cannot turn off cursor capture");
    }

    IGraphicsCaptureSession3* borders = nullptr;
    if (SUCCEEDED(static_cast<IGraphicsCaptureSession*>(session_)->QueryInterface(
            __uuidof(IGraphicsCaptureSession3), reinterpret_cast<void**>(&borders)))) {
        borders->put_IsBorderRequired(false);
        borders->Release();
    }

    allow_every_composition(static_cast<IGraphicsCaptureSession*>(session_));
    if (FAILED(static_cast<IGraphicsCaptureSession*>(session_)->StartCapture())) {
        RWIN_ERROR("capture", "the session would not start");
        return false;
    }
    size_ = size;
    outgrown_.reset();
    release(&staging_);
    RWIN_DEBUG("capture", "capturing {}x{}", size_.width, size_.height);
    return true;
}

CaptureStream::Frame::~Frame() { let_go(); }

CaptureStream::Frame::Frame(Frame&& other) noexcept
    : frame_(std::exchange(other.frame_, nullptr)),
      texture_(std::exchange(other.texture_, nullptr)),
      content_(other.content_),
      source_time_(other.source_time_),
      taken_time_(other.taken_time_) {}

CaptureStream::Frame& CaptureStream::Frame::operator=(Frame&& other) noexcept {
    if (this == &other) return *this;
    let_go();
    frame_ = std::exchange(other.frame_, nullptr);
    texture_ = std::exchange(other.texture_, nullptr);
    content_ = other.content_;
    source_time_ = other.source_time_;
    taken_time_ = other.taken_time_;
    return *this;
}

void CaptureStream::Frame::let_go() noexcept {
    if (texture_ != nullptr) texture_->Release();
    texture_ = nullptr;
    close_winrt(frame_);
    release(&frame_);
}

std::optional<CaptureStream::Extent> CaptureStream::read(const Frame& frame, std::vector<std::uint8_t>& into) {
    ID3D11Texture2D* const texture = frame.texture();
    D3D11_TEXTURE2D_DESC described{};
    texture->GetDesc(&described);

    if (staging_ != nullptr) {
        D3D11_TEXTURE2D_DESC existing{};
        static_cast<ID3D11Texture2D*>(staging_)->GetDesc(&existing);
        if (existing.Width != described.Width || existing.Height != described.Height) {
            release(&staging_);
        }
    }
    if (staging_ == nullptr) {
        D3D11_TEXTURE2D_DESC wanted = described;
        wanted.Usage = D3D11_USAGE_STAGING;
        wanted.BindFlags = 0;
        wanted.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        wanted.MiscFlags = 0;
        if (FAILED(device_.device()->CreateTexture2D(
                &wanted, nullptr, reinterpret_cast<ID3D11Texture2D**>(&staging_)))) {
            RWIN_ERROR("capture", "no staging texture");
            return std::nullopt;
        }
    }

    auto* staging = static_cast<ID3D11Texture2D*>(staging_);
    device_.context()->CopyResource(staging, texture);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(device_.context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        return std::nullopt;
    }

    const Extent content = frame.content();
    const auto* from = static_cast<const std::uint8_t*>(mapped.pData);
    const std::size_t row = static_cast<std::size_t>(content.width) * kBytesPerPixel;
    into.resize(row * content.height);
    for (std::uint32_t y = 0; y < content.height; ++y) {
        std::memcpy(into.data() + y * row,
                    from + static_cast<std::size_t>(y) * mapped.RowPitch, row);
    }
    device_.context()->Unmap(staging, 0);
    return content;
}

std::optional<CaptureStream::Extent> CaptureStream::read_next(std::vector<std::uint8_t>& into) {
    const std::optional<Frame> frame = next_frame();
    if (!frame.has_value()) return std::nullopt;
    return read(*frame, into);
}

std::optional<CaptureStream::Frame> CaptureStream::next_frame() {
    if (pool_ == nullptr) return std::nullopt;
    RWIN_TRACE("capture", "asking for a frame");
    IDirect3D11CaptureFrame* frame = nullptr;
    if (FAILED(static_cast<IDirect3D11CaptureFramePool*>(pool_)->TryGetNextFrame(&frame))
        || frame == nullptr) {
        return std::nullopt;
    }
    Frame held{frame, nullptr, {}};
    std::uint64_t drained = 0;
    for (IDirect3D11CaptureFrame* newer = nullptr;
         SUCCEEDED(static_cast<IDirect3D11CaptureFramePool*>(pool_)->TryGetNextFrame(&newer)) && newer != nullptr;
         newer = nullptr) {
        held = Frame{newer, nullptr, {}};
        frame = newer;
        ++drained;
    }

    ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface* surface_of_frame = nullptr;
    if (FAILED(frame->get_Surface(&surface_of_frame)) || surface_of_frame == nullptr) return std::nullopt;
    IDirect3DDxgiInterfaceAccess* access = nullptr;
    const HRESULT accessed = surface_of_frame->QueryInterface(__uuidof(IDirect3DDxgiInterfaceAccess),
                                                              reinterpret_cast<void**>(&access));
    surface_of_frame->Release();
    if (FAILED(accessed) || access == nullptr) return std::nullopt;
    ID3D11Texture2D* texture = nullptr;
    const HRESULT got = access->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
    access->Release();
    if (FAILED(got) || texture == nullptr) return std::nullopt;

    ABI::Windows::Graphics::SizeInt32 content{};
    if (FAILED(frame->get_ContentSize(&content))) {
        content = {static_cast<INT32>(size_.width), static_cast<INT32>(size_.height)};
    }
    D3D11_TEXTURE2D_DESC described{};
    texture->GetDesc(&described);
    if (content.Width > 0 && content.Height > 0
        && (static_cast<UINT>(content.Width) > described.Width || static_cast<UINT>(content.Height) > described.Height)) {
        texture->Release();
        outgrown_ = Extent{static_cast<std::uint32_t>(content.Width), static_cast<std::uint32_t>(content.Height)};
        RWIN_DEBUG("capture", "a {}x{} picture in {}x{} frames: dropped", content.Width, content.Height,
                   described.Width, described.Height);
        return std::nullopt;
    }
    const Extent picture{std::min(static_cast<std::uint32_t>(std::max(content.Width, 0)), described.Width),
                         std::min(static_cast<std::uint32_t>(std::max(content.Height, 0)), described.Height)};
    held.texture_ = texture;
    held.content_ = picture;
    ABI::Windows::Foundation::TimeSpan composed{};
    if (SUCCEEDED(frame->get_SystemRelativeTime(&composed)))
        held.source_time_ = composed.Duration;
    held.taken_time_ = capture_clock_now();
    if (picture.width == 0 || picture.height == 0) return std::nullopt;
    timing_.note("capture", ++captured_, held.source_time_, drained);
    if (sample_pixels_) sample_frame(held);
    return held;
}

void CaptureStream::sample_frame(const Frame& frame) {
    if (sample_ == nullptr) {
        D3D11_TEXTURE2D_DESC described{};
        described.Width = 1;
        described.Height = 1;
        described.MipLevels = 1;
        described.ArraySize = 1;
        described.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        described.SampleDesc.Count = 1;
        described.Usage = D3D11_USAGE_STAGING;
        described.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device_.device()->CreateTexture2D(&described, nullptr,
                                                     reinterpret_cast<ID3D11Texture2D**>(&sample_))))
            return;
    }
    const auto size = frame.content();
    const UINT x = size.width / 2;
    const UINT y = size.height / 2;
    const D3D11_BOX centre{x, y, 0, x + 1, y + 1, 1};
    auto* texture = static_cast<ID3D11Texture2D*>(sample_);
    device_.context()->CopySubresourceRegion(texture, 0, 0, 0, 0, frame.texture(), 0, &centre);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(device_.context()->Map(texture, 0, D3D11_MAP_READ, 0, &mapped))) return;
    timing_.note("pixel", captured_, frame.source_time(),
                 static_cast<const unsigned char*>(mapped.pData)[2]);
    device_.context()->Unmap(texture, 0);
}

}
