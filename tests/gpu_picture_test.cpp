// A picture drawn from a dma-buf on the GPU lands where the pixel path's
// QPainter puts one, to the device pixel, keyed as the pixel path keys it;
// and a frame's slot is let go of as soon as this side's copy of it is done
// on the GPU, without waiting for another frame. The dma-buf is made here with GBM on the GPU Qt draws
// on, as the GPU device makes one: a host with no such GPU, or no display,
// judges nothing.
#include "host/export/shared_frame.hpp"
#include "host/export/slot_lease.hpp"
#include "host/export/surface_memory.hpp"
#include "host/link/guest_link.hpp"
#include "host/mirror/gpu_picture.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

#include <gbm.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <unistd.h>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

constexpr int kWidth = 61;
constexpr int kHeight = 37;
// The colour a keyed window paints where it wants to be see-through.
constexpr QRgb kKey = 0xFF102030;

// Every texel its own value, premultiplied, one of them the key.
QRgb pattern(int x, int y) {
    if (x == 7 && y == 5) return kKey;
    const int alpha = (x + y) % 3 == 0 ? 0x80 : 0xFF;
    const auto channel = [&](int value) { return (value & 0xFF) * alpha / 0xFF; };
    return qRgba(channel(x * 4), channel(y * 6), channel(x + y), alpha);
}

// A linear GBM buffer on the render node, filled through a mapping.
class Buffer {
public:
    explicit Buffer(const char* node) : fd_(::open(node, O_RDWR | O_CLOEXEC)) {
        if (fd_ < 0) return;
        device_ = gbm_create_device(fd_);
        if (device_ == nullptr) return;
        bo_ = gbm_bo_create(device_, kWidth, kHeight, GBM_FORMAT_ARGB8888, GBM_BO_USE_LINEAR | GBM_BO_USE_RENDERING);
        if (bo_ == nullptr) return;
        std::uint32_t stride = 0;
        void* mapping = nullptr;
        auto* pixels = static_cast<std::uint8_t*>(
            gbm_bo_map(bo_, 0, 0, kWidth, kHeight, GBM_BO_TRANSFER_WRITE, &stride, &mapping));
        if (pixels == nullptr) return;
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                const QRgb value = pattern(x, y);
                std::memcpy(pixels + y * stride + x * 4, &value, sizeof value);
            }
        }
        gbm_bo_unmap(bo_, mapping);
    }
    ~Buffer() {
        if (bo_ != nullptr) gbm_bo_destroy(bo_);
        if (device_ != nullptr) gbm_device_destroy(device_);
        if (fd_ >= 0) ::close(fd_);
    }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    [[nodiscard]] bool made() const { return bo_ != nullptr; }

    // As the GPU device would export it: the layout implicit, the driver's
    // own record of it read from the memory.
    [[nodiscard]] std::shared_ptr<const rwin::host::SurfaceMemory> exported(std::uint64_t surface) const {
        const rwin::host::ExportedLayout layout{kWidth, kHeight, rwin::host::ExportedLayout::kArgb8888,
                                                gbm_bo_get_stride(bo_), 0,
                                                rwin::host::ExportedLayout::kImplicitModifier};
        return std::make_shared<const rwin::host::SurfaceMemory>(surface, layout, rwin::host::UniqueFd{gbm_bo_get_fd(bo_)});
    }

private:
    int fd_ = -1;
    gbm_device* device_ = nullptr;
    gbm_bo* bo_ = nullptr;
};

bool released_within(const std::weak_ptr<const rwin::host::SlotLease>& lease, int milliseconds) {
    QElapsedTimer waited;
    waited.start();
    while (!lease.expired() && waited.elapsed() < milliseconds) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return lease.expired();
}

void draws_one_to_one_and_keyed(rwin::host::GpuPicture& picture, const Buffer& buffer) {
    rwin::host::GuestLink link;
    auto lease = std::make_shared<const rwin::host::SlotLease>(link, rwin::SlotFree{1, 1, 0});
    const std::weak_ptr<const rwin::host::SlotLease> first_lease = lease;
    picture.show_frame(rwin::host::SharedFrame{buffer.exported(1), std::move(lease), nullptr, 1});
    check(released_within(first_lease, 2000), "the slot is freed once this side's copy of it is done");
    rwin::host::PictureLayout layout;
    layout.picture = QRect(QPoint(3, 2), QSize(kWidth, kHeight));
    layout.color_key = kKey;
    layout.bars = true;
    picture.lay_out(layout);
    const QImage drawn = picture.drawn().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    check(!drawn.isNull(), "the picture was drawn");
    int wrong = 0;
    for (int y = 0; y < drawn.height(); ++y) {
        for (int x = 0; x < drawn.width(); ++x) {
            const QPoint from(x - 3, y - 2);
            QRgb expected = 0xFF000000;
            if (from.x() >= 0 && from.y() >= 0 && from.x() < kWidth && from.y() < kHeight) {
                expected = pattern(from.x(), from.y()) == kKey ? 0 : pattern(from.x(), from.y());
            }
            if (drawn.pixel(x, y) != expected) {
                if (wrong < 4) std::printf("  at %d,%d: %08x, not %08x\n", x, y, drawn.pixel(x, y), expected);
                ++wrong;
            }
        }
    }
    std::printf("drawn %dx%d, %d pixel(s) wrong\n", drawn.width(), drawn.height(), wrong);
    check(wrong == 0, "every texel on exactly one device pixel, the key see-through, black bars round it");

    // A shaped window: nothing of the picture outside its region.
    layout.shape = QRegion(QRect(QPoint(3, 2), QSize(10, kHeight)));
    picture.lay_out(layout);
    const QImage shaped = picture.drawn().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    check(shaped.pixel(3 + 1, 2 + 1) == pattern(1, 1), "inside its region the picture is drawn");
    check(shaped.pixel(3 + 20, 2 + 1) == 0, "outside it nothing is");
    layout.shape = QRegion();
    // Fitted: resampled, not a copy, and still inside the picture's place -
    // three quarters of the window's device size, whatever its scale.
    layout.picture = QRect(QPoint(0, 0), drawn.size() * 3 / 4);
    layout.smooth = true;
    layout.bars = false;
    picture.lay_out(layout);
    const QImage fitted = picture.drawn().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QPoint inside = layout.picture.center();
    check(qAlpha(fitted.pixel(inside)) != 0 && fitted.pixel(fitted.width() - 1, fitted.height() - 1) == 0,
          "fitted, the picture fills its place and nothing past it");

    // The slot freed, the picture is still there to draw again: it is this
    // side's copy that is drawn.
    layout.picture = QRect(QPoint(3, 2), QSize(kWidth, kHeight));
    layout.smooth = false;
    layout.bars = true;
    picture.lay_out(layout);
    const QImage again = picture.drawn().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    check(again.pixel(3 + 1, 2 + 1) == pattern(1, 1), "drawn again after its slot was freed");
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // offscreen and minimal render GL in software, with no EGL to import a
    // dma-buf into: the GPU path is refused there by design, not broken.
    if (QGuiApplication::platformName() == QLatin1String("offscreen") ||
        QGuiApplication::platformName() == QLatin1String("minimal")) {
        std::printf("no EGL on the %s platform: nothing judged\n",
                    qPrintable(QGuiApplication::platformName()));
        return EXIT_SUCCESS;
    }
    Buffer buffer("/dev/dri/renderD128");
    if (!buffer.made()) {
        std::printf("no GBM buffer on renderD128: nothing judged\n");
        return EXIT_SUCCESS;
    }
    QWidget window;
    window.setAttribute(Qt::WA_TranslucentBackground);
    window.resize(80, 50);
    auto* picture = new rwin::host::GpuPicture();
    QWidget* view = QWidget::createWindowContainer(picture, &window);
    view->setGeometry(window.rect());
    window.show();
    QElapsedTimer shown;
    shown.start();
    while (!picture->isExposed() && shown.elapsed() < 2000) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    check(picture->isExposed(), "the picture's window is on screen");
    bool failed = false;
    QObject::connect(picture, &rwin::host::GpuPicture::failed, [&](const QString& why) {
        std::printf("GPU path refused: %s\n", qPrintable(why));
        failed = true;
    });
    draws_one_to_one_and_keyed(*picture, buffer);
    QCoreApplication::processEvents();
    check(!failed, "the GPU path was not refused");
    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("gpu picture: all checks passed\n");
    return EXIT_SUCCESS;
}
