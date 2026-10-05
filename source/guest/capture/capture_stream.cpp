#include "guest/capture/capture_stream.hpp"

#include "guest/capture/frame_signal.hpp"
#include "guest/capture/graphics_device.hpp"
#include "logging/logger.hpp"

// mingw's windows.foundation.h declares IReference<boolean> and
// IReference<BYTE> separately, and its own boolean is unsigned char — so
// the two are one type defined twice and the header does not compile.
// Suppressing the later of the pair leaves the type present under the
// other name, which is the one nothing here asks for by name.
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
#include <utility>

namespace capture_abi = ABI::Windows::Graphics::Capture;
using capture_abi::IDirect3D11CaptureFrame;
using capture_abi::IDirect3D11CaptureFramePool;
using capture_abi::IDirect3D11CaptureFramePoolStatics2;
using capture_abi::IGraphicsCaptureItem;
using capture_abi::IGraphicsCaptureSession;
using capture_abi::IGraphicsCaptureSession2;
using capture_abi::IGraphicsCaptureSession3;
using ABI::Windows::Foundation::IClosable;
using FrameArrived = __FITypedEventHandler_2_Windows__CGraphics__CCapture__CDirect3D11CaptureFramePool_IInspectable;

// mingw ships the capture headers but not the Direct3D11 interop one, so
// the interface it declares is declared here — at namespace scope and
// with its identifier attached, which is what makes a COM interface
// callable rather than merely nameable.
MIDL_INTERFACE("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")
IDirect3DDxgiInterfaceAccess : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID iid, void** object) = 0;
};
__CRT_UUID_DECL(IDirect3DDxgiInterfaceAccess, 0xA9B3D012, 0x3DF2, 0x4EE3, 0xB8, 0xD1,
                0x86, 0x95, 0xF4, 0x57, 0xD3, 0xC1)

namespace rwin::guest {

namespace {

constexpr std::size_t kBytesPerPixel = 4;
// Two buffers: one being composed into while the other is read.
constexpr int kPoolBuffers = 2;
// DirectXPixelFormat::B8G8R8A8UIntNormalized. The only format the
// compositor hands out, and the one that carries alpha.
constexpr int kBgra8 = 87;

template <typename T>
HRESULT activation_factory(const wchar_t* runtime_class, T** out) {
    HSTRING_HEADER header{};
    HSTRING name = nullptr;
    const HRESULT named = ::WindowsCreateStringReference(
        runtime_class, static_cast<UINT32>(::wcslen(runtime_class)), &header, &name);
    if (FAILED(named)) return named;
    return ::RoGetActivationFactory(name, __uuidof(T), reinterpret_cast<void**>(out));
}

// WinRT's Close, which ends what a Release alone leaves running.
void close_winrt(void* object) {
    if (object == nullptr) return;
    IClosable* closable = nullptr;
    if (SUCCEEDED(static_cast<IUnknown*>(object)->QueryInterface(
            __uuidof(IClosable), reinterpret_cast<void**>(&closable)))) {
        closable->Close();
        closable->Release();
    }
}

void release(void** object) {
    if (*object != nullptr) {
        static_cast<IUnknown*>(*object)->Release();
        *object = nullptr;
    }
}

// What the pool calls, on a thread of its own, as each frame is composed.
// Agile: a free-threaded pool calls it from whichever thread it likes.
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
    HRESULT STDMETHODCALLTYPE Invoke(IDirect3D11CaptureFramePool* /*pool*/, IInspectable* /*nothing*/) override {
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

}  // namespace

CaptureStream::CaptureStream(const GraphicsDevice& device, const FrameSignal& arrivals)
    : device_(device), arrivals_(arrivals) {}

CaptureStream::~CaptureStream() { close(); }

void CaptureStream::close_session() {
    close_winrt(session_);
    release(&session_);
}

// The pool holds its buffers until it is closed, not until it is
// released: a capture of every window the matrix makes, each released
// and none closed, is buffers the size of a maximised window apiece.
void CaptureStream::close_pool() {
    if (pool_ != nullptr && arrival_token_.has_value()) {
        static_cast<IDirect3D11CaptureFramePool*>(pool_)->remove_FrameArrived(EventRegistrationToken{*arrival_token_});
    }
    arrival_token_.reset();
    close_winrt(pool_);
    release(&pool_);
}

void CaptureStream::close() {
    close_session();
    close_pool();
    release(&item_);
    release(&staging_);
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
    if (resize(size)) return true;
    close();
    return false;
}

bool CaptureStream::resize(Extent size) {
    IDirect3D11CaptureFramePoolStatics2* statics = nullptr;
    if (FAILED(activation_factory(
            RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool,
            &statics))) {
        return false;
    }
    const ABI::Windows::Graphics::SizeInt32 pool_size{static_cast<INT32>(size.width),
                                                      static_cast<INT32>(size.height)};

    close_session();
    close_pool();

    // The wrapped device, asked for by the interface a frame pool takes.
    ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice* wrapped = nullptr;
    if (FAILED(device_.winrt_device()->QueryInterface(
            __uuidof(ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice),
            reinterpret_cast<void**>(&wrapped)))) {
        statics->Release();
        RWIN_ERROR("capture", "the wrapped device is not a Direct3D device");
        return false;
    }

    // Free-threaded: frames then arrive without a dispatcher on this
    // thread to pump, which a console program does not have.
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

    // The pointer, which belongs to the desktop rather than to anything
    // captured. Left on, the guest's cursor is painted into the frame and
    // arrives as part of the picture — so the host shows two, its own and
    // a drawn one that lags behind it.
    IGraphicsCaptureSession2* pointer = nullptr;
    if (SUCCEEDED(static_cast<IGraphicsCaptureSession*>(session_)->QueryInterface(
            __uuidof(IGraphicsCaptureSession2), reinterpret_cast<void**>(&pointer)))) {
        pointer->put_IsCursorCaptureEnabled(false);
        pointer->Release();
    } else {
        RWIN_WARN("capture", "this build cannot turn off cursor capture");
    }

    // The yellow "you are being captured" border, which is not part of
    // the picture and would be mirrored as if it were. Only newer builds
    // allow turning it off; on older ones the request simply fails.
    IGraphicsCaptureSession3* borders = nullptr;
    if (SUCCEEDED(static_cast<IGraphicsCaptureSession*>(session_)->QueryInterface(
            __uuidof(IGraphicsCaptureSession3), reinterpret_cast<void**>(&borders)))) {
        borders->put_IsBorderRequired(false);
        borders->Release();
    }

    if (FAILED(static_cast<IGraphicsCaptureSession*>(session_)->StartCapture())) {
        RWIN_ERROR("capture", "the session would not start");
        return false;
    }
    size_ = size;
    release(&staging_);
    RWIN_DEBUG("capture", "capturing {}x{}", size_.width, size_.height);
    return true;
}

CaptureStream::Frame::~Frame() { let_go(); }

CaptureStream::Frame::Frame(Frame&& other) noexcept
    : frame_(std::exchange(other.frame_, nullptr)),
      texture_(std::exchange(other.texture_, nullptr)),
      content_(other.content_) {}

CaptureStream::Frame& CaptureStream::Frame::operator=(Frame&& other) noexcept {
    if (this == &other) return *this;
    let_go();
    frame_ = std::exchange(other.frame_, nullptr);
    texture_ = std::exchange(other.texture_, nullptr);
    content_ = other.content_;
    return *this;
}

// Closed as well as released: the pool composes into a buffer again only
// once its frame is closed.
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

    // The rows are copied rather than the block because a texture's pitch
    // is its own business and is routinely wider than its pixels.
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
    // Held from here, so every way out lets go of it.
    Frame held{frame, nullptr, {}};
    for (IDirect3D11CaptureFrame* newer = nullptr;
         SUCCEEDED(static_cast<IDirect3D11CaptureFramePool*>(pool_)->TryGetNextFrame(&newer)) && newer != nullptr;
         newer = nullptr) {
        held = Frame{newer, nullptr, {}};
        frame = newer;
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
    // The picture is in the top left of the buffer, as large as the frame
    // says it is, as far as the buffer holds it.
    D3D11_TEXTURE2D_DESC described{};
    texture->GetDesc(&described);
    const Extent picture{std::min(static_cast<std::uint32_t>(std::max(content.Width, 0)), described.Width),
                         std::min(static_cast<std::uint32_t>(std::max(content.Height, 0)), described.Height)};
    held.texture_ = texture;
    held.content_ = picture;
    if (picture.width == 0 || picture.height == 0) return std::nullopt;
    return held;
}

}  // namespace rwin::guest
