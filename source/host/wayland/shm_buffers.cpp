#include "host/wayland/shm_buffers.hpp"

#include <algorithm>

namespace rwin::host {

ShmBuffer* ShmBuffers::free_buffer(Size size) {
    std::erase_if(buffers_, [size](const std::unique_ptr<ShmBuffer>& buffer) {
        return buffer->size() != size && !buffer->held();
    });
    for (const std::unique_ptr<ShmBuffer>& buffer : buffers_) {
        if (!buffer->held()) return buffer.get();
    }
    if (buffers_.size() >= kMost) return nullptr;
    buffers_.push_back(std::make_unique<ShmBuffer>(shm_, size));
    return buffers_.back().get();
}

}  // namespace rwin::host
