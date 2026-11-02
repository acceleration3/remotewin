#include "guest/session/outbox.hpp"

#include "guest/session/tcp_client.hpp"

namespace rwin::guest {

void Outbox::send(const std::vector<std::uint8_t>& message) {
    if (failed_.load()) return;
    if (!link_.send(message)) failed_.store(true);
}

}
