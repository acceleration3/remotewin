#pragma once

#include <memory>

namespace rwin::host {
template <typename Proxy, void (*Destroy)(Proxy*)>
struct ProxyDestroyer {
    void operator()(Proxy* proxy) const noexcept { Destroy(proxy); }
};

template <typename Proxy, void (*Destroy)(Proxy*)>
using OwnedProxy = std::unique_ptr<Proxy, ProxyDestroyer<Proxy, Destroy>>;

}
