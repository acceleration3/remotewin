#include "guest/capture/window_capture.hpp"

#include "guest/capture/content_bounds.hpp"
#include "guest/capture/graphics_device.hpp"
#include "guest/windows/window_surface.hpp"
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
#include <cstring>

namespace capture_abi = ABI::Windows::Graphics::Capture;
using capture_abi::IDirect3D11CaptureFrame;
using capture_abi::IDirect3D11CaptureFramePool;
using capture_abi::IDirect3D11CaptureFramePoolStatics2;
using capture_abi::IGraphicsCaptureItem;
using capture_abi::IGraphicsCaptureSession;
using capture_abi::IGraphicsCaptureSession2;
using capture_abi::IGraphicsCaptureSession3;
using ABI::Windows::Foundation::IClosable;

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

void release(void** object) {
    if (*object != nullptr) {
        static_cast<IUnknown*>(*object)->Release();
        *object = nullptr;
    }
}

}  // namespace

WindowCapture::WindowCapture(const GraphicsDevice& device) : device_(device) {}

WindowCapture::~WindowCapture() { stop(); }

void WindowCapture::close_session() {
    if (session_ != nullptr) {
        IClosable* closable = nullptr;
        if (SUCCEEDED(static_cast<IGraphicsCaptureSession*>(session_)->QueryInterface(
                __uuidof(IClosable), reinterpret_cast<void**>(&closable)))) {
            closable->Close();
            closable->Release();
        }
    }
    release(&session_);
}

void WindowCapture::stop() {
    close_session();
    release(&pool_);
    release(&item_);
    release(&staging_);
    started_for_ = 0;
    pool_width_ = 0;
    pool_height_ = 0;
}

bool WindowCapture::start(const WindowSurface& surface) {
    stop();
    if (!device_.valid()) return false;

    IGraphicsCaptureItemInterop* interop = nullptr;
    if (FAILED(activation_factory(
            RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem, &interop))) {
        RWIN_ERROR("capture", "no capture item factory");
        return false;
    }
    const HRESULT made = interop->CreateForWindow(
        static_cast<HWND>(surface.handle()), __uuidof(IGraphicsCaptureItem), &item_);
    interop->Release();
    if (FAILED(made) || item_ == nullptr) {
        RWIN_WARN("capture", "cannot capture this window: {:#010x}",
                  static_cast<unsigned>(made));
        return false;
    }

    if (!resize_pool(surface)) return false;
    started_for_ =
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(surface.handle()));
    return true;
}

bool WindowCapture::resize_pool(const WindowSurface& surface) {
    IDirect3D11CaptureFramePoolStatics2* statics = nullptr;
    if (FAILED(activation_factory(
            RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool,
            &statics))) {
        return false;
    }
    const ABI::Windows::Graphics::SizeInt32 size{static_cast<INT32>(surface.width()),
                                                 static_cast<INT32>(surface.height())};

    close_session();
    release(&pool_);

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
        kPoolBuffers, size, reinterpret_cast<IDirect3D11CaptureFramePool**>(&pool_));
    wrapped->Release();
    statics->Release();
    if (FAILED(created) || pool_ == nullptr) {
        RWIN_ERROR("capture", "no frame pool: {:#010x}", static_cast<unsigned>(created));
        return false;
    }
    if (FAILED(static_cast<IDirect3D11CaptureFramePool*>(pool_)->CreateCaptureSession(
            static_cast<IGraphicsCaptureItem*>(item_),
            reinterpret_cast<IGraphicsCaptureSession**>(&session_)))
        || session_ == nullptr) {
        RWIN_ERROR("capture", "no capture session");
        return false;
    }

    // The pointer, which belongs to the desktop rather than to the
    // window. Left on, the guest's cursor is painted into the frame and
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
    // the window and would be mirrored as if it were. Only newer builds
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
    RWIN_DEBUG("capture", "session started");
    pool_width_ = surface.width();
    pool_height_ = surface.height();
    release(&staging_);
    RWIN_DEBUG("capture", "capturing {}x{}", pool_width_, pool_height_);
    return true;
}

bool WindowCapture::read_back(void* frame_texture, const WindowSurface& /*surface*/) {
    auto* frame = static_cast<ID3D11Texture2D*>(frame_texture);
    D3D11_TEXTURE2D_DESC described{};
    frame->GetDesc(&described);

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
            return false;
        }
    }

    auto* staging = static_cast<ID3D11Texture2D*>(staging_);
    device_.context()->CopyResource(staging, frame);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(device_.context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        return false;
    }

    // Only the drawn part. The rows are copied rather than the block
    // because a texture's pitch is its own business and is routinely
    // wider than its pixels.
    const auto* from = static_cast<const std::uint8_t*>(mapped.pData);
    const std::size_t source_row =
        static_cast<std::size_t>(described.Width) * kBytesPerPixel;
    scratch_.resize(source_row * described.Height);
    for (std::uint32_t y = 0; y < described.Height; ++y) {
        std::memcpy(scratch_.data() + y * source_row,
                    from + static_cast<std::size_t>(y) * mapped.RowPitch, source_row);
    }
    device_.context()->Unmap(staging, 0);

    const ContentBounds drawn{scratch_.data(), described.Width, described.Height};
    if (drawn.empty()) return false;

    offset_x_ = drawn.left();
    offset_y_ = drawn.top();
    width_ = drawn.width();
    height_ = drawn.height();

    const std::size_t row = static_cast<std::size_t>(width_) * kBytesPerPixel;
    visible_.resize(row * height_);
    const std::size_t left = static_cast<std::size_t>(offset_x_) * kBytesPerPixel;
    for (std::uint32_t y = 0; y < height_; ++y) {
        std::memcpy(visible_.data() + y * row,
                    scratch_.data() + (offset_y_ + y) * source_row + left, row);
    }
    return true;
}

WindowCapture::Result WindowCapture::grab(const WindowSurface& surface) {
    if (!surface.valid()) return Result::Gone;

    const auto handle =
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(surface.handle()));
    if (started_for_ != handle && !start(surface)) return Result::Gone;
    if ((surface.width() != pool_width_ || surface.height() != pool_height_)
        && !resize_pool(surface)) {
        return Result::Gone;
    }

    RWIN_TRACE("capture", "asking for a frame");
    IDirect3D11CaptureFrame* frame = nullptr;
    if (FAILED(static_cast<IDirect3D11CaptureFramePool*>(pool_)->TryGetNextFrame(&frame))
        || frame == nullptr) {
        return Result::Unchanged;
    }
    RWIN_TRACE("capture", "got a frame");

    ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface* surface_of_frame =
        nullptr;
    bool read = false;
    if (SUCCEEDED(frame->get_Surface(&surface_of_frame)) && surface_of_frame != nullptr) {
        RWIN_TRACE("capture", "got the surface");
        IDirect3DDxgiInterfaceAccess* access = nullptr;
        if (SUCCEEDED(surface_of_frame->QueryInterface(
                __uuidof(IDirect3DDxgiInterfaceAccess),
                reinterpret_cast<void**>(&access)))
            && access != nullptr) {
            RWIN_TRACE("capture", "got dxgi access");
            ID3D11Texture2D* texture = nullptr;
            if (SUCCEEDED(access->GetInterface(__uuidof(ID3D11Texture2D),
                                               reinterpret_cast<void**>(&texture)))
                && texture != nullptr) {
                RWIN_TRACE("capture", "got the texture");
                read = read_back(texture, surface);
                texture->Release();
            }
            access->Release();
        }
        surface_of_frame->Release();
    }
    frame->Release();
    return read ? Result::Fresh : Result::Unchanged;
}

}  // namespace rwin::guest
