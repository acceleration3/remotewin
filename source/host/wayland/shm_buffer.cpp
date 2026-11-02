#include "host/wayland/shm_buffer.hpp"

#include <wayland-client.h>

#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace rwin::host {

ShmBuffer::ShmBuffer(wl_shm* shm, Size size, const Signal<>* freed)
    : freed_(freed), size_(size), bytes_(static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height) * 4),
      memory_(::memfd_create("remotewin-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING)) {
    if (!memory_.valid() || ::ftruncate(memory_.get(), static_cast<off_t>(bytes_)) != 0) {
        throw std::runtime_error(std::string("no shared memory for a picture: ") + std::strerror(errno));
    }
    void* mapped = ::mmap(nullptr, bytes_, PROT_READ | PROT_WRITE, MAP_SHARED, memory_.get(), 0);
    if (mapped == MAP_FAILED) throw std::runtime_error(std::string("cannot map a picture: ") + std::strerror(errno));
    pixels_ = static_cast<std::uint8_t*>(mapped);
    wl_shm_pool* pool = wl_shm_create_pool(shm, memory_.get(), static_cast<std::int32_t>(bytes_));
    buffer_ = wl_shm_pool_create_buffer(pool, 0, size.width, size.height, stride(), WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    static constexpr wl_buffer_listener kListener{&ShmBuffer::released};
    wl_buffer_add_listener(buffer_, &kListener, this);
}

ShmBuffer::~ShmBuffer() {
    if (buffer_ != nullptr) wl_buffer_destroy(buffer_);
    if (pixels_ != nullptr) ::munmap(pixels_, bytes_);
}

void ShmBuffer::released(void* self, wl_buffer* ) {
    auto* buffer = static_cast<ShmBuffer*>(self);
    buffer->held_ = false;
    if (buffer->freed_ != nullptr) (*buffer->freed_)();
}

}
