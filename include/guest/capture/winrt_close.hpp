#pragma once

namespace rwin::guest {

// WinRT's Close, which ends what a Release alone leaves running, on
// ``object`` if it is closable; it stays referenced either way.
void close_winrt(void* object);

}  // namespace rwin::guest
