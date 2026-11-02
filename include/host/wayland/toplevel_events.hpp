#pragma once

namespace rwin::host {

struct ToplevelConfigure;

class ToplevelEvents {
public:
    virtual void configured(const ToplevelConfigure& configure) = 0;
    virtual void close_requested() = 0;

protected:
    ~ToplevelEvents() = default;
};

}
