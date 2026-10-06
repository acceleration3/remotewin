#pragma once

#include <QObject>
#include <QPointF>

#include <cstdint>
#include <memory>

struct zwp_relative_pointer_manager_v1;
struct zwp_relative_pointer_v1;
struct wl_pointer;

namespace rwin::host {

// The pointer's own motion (zwp_relative_pointer_v1), told whenever the
// pointer moves over one of this client's surfaces - locked or not - in
// the device's counts before any acceleration.
class RelativePointer : public QObject {
    Q_OBJECT
public:
    RelativePointer(zwp_relative_pointer_manager_v1* manager, wl_pointer* pointer, QObject* parent = nullptr);

Q_SIGNALS:
    void moved(QPointF unaccelerated);
    // The same motion as the pointer itself made it, in the surface's
    // logical units: what wl_pointer.motion moves by.
    void moved_by(QPointF accelerated);

private:
    struct Release {
        void operator()(zwp_relative_pointer_v1* relative) const noexcept;
    };

    static void motion(void* self, zwp_relative_pointer_v1* relative, std::uint32_t time_high,
                       std::uint32_t time_low, std::int32_t dx, std::int32_t dy, std::int32_t dx_unaccelerated,
                       std::int32_t dy_unaccelerated);

    std::unique_ptr<zwp_relative_pointer_v1, Release> relative_;
};

}  // namespace rwin::host
