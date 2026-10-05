#pragma once

#include <memory>

namespace rwin::host {

// A Wayland object of this client's, destroyed with the request its
// protocol names for that.
template <typename Proxy, void (*Destroy)(Proxy*)>
struct ProxyDestroyer {
    void operator()(Proxy* proxy) const noexcept { Destroy(proxy); }
};

template <typename Proxy, void (*Destroy)(Proxy*)>
using OwnedProxy = std::unique_ptr<Proxy, ProxyDestroyer<Proxy, Destroy>>;

}  // namespace rwin::host
