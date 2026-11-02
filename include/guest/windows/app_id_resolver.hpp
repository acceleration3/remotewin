#pragma once

#include <string>

struct IUnknown;

namespace rwin::guest {
class AppIdResolver {
public:
    AppIdResolver();
    ~AppIdResolver();

    AppIdResolver(const AppIdResolver&) = delete;
    AppIdResolver& operator=(const AppIdResolver&) = delete;
    AppIdResolver(AppIdResolver&&) = delete;
    AppIdResolver& operator=(AppIdResolver&&) = delete;

    [[nodiscard]] std::string app_id_of(void* window) const;

private:
    IUnknown* resolver_ = nullptr;
};

}
