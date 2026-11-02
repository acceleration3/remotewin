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

class DmabufBuffers {
public:
    using Made = std::function<void(const SurfaceMemory&, wl_buffer*, const std::string&)>;
    using Released = std::function<void(wl_buffer*)>;

    DmabufBuffers(zwp_linux_dmabuf_v1* dmabuf, Made made, Released released);
    ~DmabufBuffers();

    DmabufBuffers(const DmabufBuffers&) = delete;
    DmabufBuffers& operator=(const DmabufBuffers&) = delete;
    DmabufBuffers(DmabufBuffers&&) = delete;
    DmabufBuffers& operator=(DmabufBuffers&&) = delete;

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
    void forget(Entry& entry);

    static constexpr std::size_t kMost = 6;

    zwp_linux_dmabuf_v1* dmabuf_;
    Made made_;
    Released released_;
    std::vector<Entry> entries_;
    std::uint64_t uses_ = 0;
};

}
