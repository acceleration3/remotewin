#pragma once

#include <cstdint>

namespace rwin {
struct KeyStroke;
}

namespace rwin::guest {
void put_key(const KeyStroke& stroke);

}
