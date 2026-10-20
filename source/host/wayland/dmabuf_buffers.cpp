#include "host/wayland/dmabuf_buffers.hpp"

#include "host/export/surface_memory.hpp"
#include "logging/logger.hpp"

#include <linux-dmabuf-v1-client-protocol.h>
#include <wayland-client.h>

#include <algorithm>

namespace rwin::host {

DmabufBuffers::DmabufBuffers(zwp_linux_dmabuf_v1* dmabuf, Made made, Released released)
    : dmabuf_(dmabuf), made_(std::move(made)), released_(std::move(released)) {}

DmabufBuffers::~DmabufBuffers() {
    for (Entry& entry : entries_) {
        if (entry.params != nullptr) zwp_linux_buffer_params_v1_destroy(entry.params);
        if (entry.buffer != nullptr) wl_buffer_destroy(entry.buffer);
    }
}

wl_buffer* DmabufBuffers::buffer_for(const std::shared_ptr<const SurfaceMemory>& memory) {
    const auto known = std::find_if(entries_.begin(), entries_.end(),
                                    [&](const Entry& entry) { return entry.memory == memory; });
    if (known != entries_.end()) {
        known->used = ++uses_;
        return known->buffer;
    }
    evict();
    entries_.push_back(Entry{memory, nullptr, nullptr, false, ++uses_});
    make(entries_.back());
    return nullptr;
}

void DmabufBuffers::make(Entry& entry) {
    const ExportedLayout& layout = entry.memory->layout();
    entry.params = zwp_linux_dmabuf_v1_create_params(dmabuf_);
    zwp_linux_buffer_params_v1_add(entry.params, entry.memory->dma_buf(), 0, layout.offset, layout.stride,
                                   static_cast<std::uint32_t>(layout.modifier >> 32),
                                   static_cast<std::uint32_t>(layout.modifier & 0xFFFF'FFFFu));
    static constexpr zwp_linux_buffer_params_v1_listener kListener{&DmabufBuffers::created, &DmabufBuffers::failed};
    zwp_linux_buffer_params_v1_add_listener(entry.params, &kListener, this);
    zwp_linux_buffer_params_v1_create(entry.params, static_cast<std::int32_t>(layout.width),
                                      static_cast<std::int32_t>(layout.height), layout.fourcc, 0);
}

void DmabufBuffers::created(void* self, zwp_linux_buffer_params_v1* params, wl_buffer* buffer) {
    auto* buffers = static_cast<DmabufBuffers*>(self);
    const auto entry = std::find_if(buffers->entries_.begin(), buffers->entries_.end(),
                                    [params](const Entry& known) { return known.params == params; });
    zwp_linux_buffer_params_v1_destroy(params);
    if (entry == buffers->entries_.end()) {
        wl_buffer_destroy(buffer);
        return;
    }
    entry->params = nullptr;
    entry->buffer = buffer;
    static constexpr wl_buffer_listener kListener{&DmabufBuffers::released};
    wl_buffer_add_listener(buffer, &kListener, buffers);
    buffers->made_(*entry->memory, buffer, std::string());
}

void DmabufBuffers::failed(void* self, zwp_linux_buffer_params_v1* params) {
    auto* buffers = static_cast<DmabufBuffers*>(self);
    const auto entry = std::find_if(buffers->entries_.begin(), buffers->entries_.end(),
                                    [params](const Entry& known) { return known.params == params; });
    zwp_linux_buffer_params_v1_destroy(params);
    if (entry == buffers->entries_.end()) return;
    entry->params = nullptr;
    entry->refused = true;
    const ExportedLayout& layout = entry->memory->layout();
    buffers->made_(*entry->memory, nullptr,
                   "the compositor would not take surface " + std::to_string(entry->memory->surface()) + " ("
                       + std::to_string(layout.width) + "x" + std::to_string(layout.height) + ", modifier "
                       + std::to_string(layout.modifier) + ")");
}

void DmabufBuffers::released(void* self, wl_buffer* buffer) { static_cast<DmabufBuffers*>(self)->released_(buffer); }

// The least lately used whose making is over; a buffer the compositor
// still holds is destroyed all the same - it keeps its own hold on the
// memory until it is done.
// Memory no ring holds any more (its window's ring was made again) goes
// first, whatever room there is: kept, it would keep the old ring's GPU
// memory alive.
void DmabufBuffers::evict() {
    std::erase_if(entries_, [this](Entry& entry) {
        if (entry.memory.use_count() != 1) return false;
        forget(entry);
        return true;
    });
    while (entries_.size() >= kMost) {
        const auto oldest = std::min_element(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
            if ((a.params == nullptr) != (b.params == nullptr)) return a.params == nullptr;
            return a.used < b.used;
        });
        forget(*oldest);
        entries_.erase(oldest);
    }
}

void DmabufBuffers::forget(Entry& entry) {
    if (entry.params != nullptr) zwp_linux_buffer_params_v1_destroy(entry.params);
    if (entry.buffer != nullptr) {
        released_(entry.buffer);
        wl_buffer_destroy(entry.buffer);
    }
}

}  // namespace rwin::host
