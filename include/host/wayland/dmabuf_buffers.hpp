#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct wl_buffer;
struct zwp_linux_buffer_params_v1;
struct zwp_linux_dmabuf_v1;

namespace rwin::host {

class SurfaceMemory;

// The compositor's buffers for one surface's shared surfaces
// (zwp_linux_dmabuf_v1): each slot's memory made a wl_buffer once, as it
// is first shown, in the layout its driver kept with it, and kept for as
// long as the slot may be shown again.
class DmabufBuffers {
public:
    // A buffer the compositor made or refused; refused with why.
    using Made = std::function<void(const SurfaceMemory&, wl_buffer*, const std::string&)>;
    using Released = std::function<void(wl_buffer*)>;

    // ``made`` hears every buffer made or refused; ``released`` every
    // wl_buffer.release of one.
    DmabufBuffers(zwp_linux_dmabuf_v1* dmabuf, Made made, Released released);
    ~DmabufBuffers();

    DmabufBuffers(const DmabufBuffers&) = delete;
    DmabufBuffers& operator=(const DmabufBuffers&) = delete;
    DmabufBuffers(DmabufBuffers&&) = delete;
    DmabufBuffers& operator=(DmabufBuffers&&) = delete;

    // The buffer of ``memory``; null while it is being made (``made``
    // says when), or when it was refused.
    [[nodiscard]] wl_buffer* buffer_for(const std::shared_ptr<const SurfaceMemory>& memory);

private:
    struct Entry {
        std::shared_ptr<const SurfaceMemory> memory;
        zwp_linux_buffer_params_v1* params = nullptr;
        wl_buffer* buffer = nullptr;
        bool refused = false;
        std::uint64_t used = 0;
    };

    static void created(void* self, zwp_linux_buffer_params_v1* params, wl_buffer* buffer);
    static void failed(void* self, zwp_linux_buffer_params_v1* params);
    static void released(void* self, wl_buffer* buffer);
    void make(Entry& entry);
    void evict();

    // Two rings' worth: the one shown and the one replacing it on a resize.
    static constexpr std::size_t kMost = 6;

    zwp_linux_dmabuf_v1* dmabuf_;
    Made made_;
    Released released_;
    std::vector<Entry> entries_;
    std::uint64_t uses_ = 0;
};

}  // namespace rwin::host
