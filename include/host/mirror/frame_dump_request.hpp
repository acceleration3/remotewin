#pragma once

struct sd_bus_error;
struct sd_bus_message;
struct sd_bus_slot;

namespace rwin::host {

class BusConnection;
class GuestLink;

class FrameDumpRequest {
public:
    FrameDumpRequest(BusConnection& bus, GuestLink& link);
    ~FrameDumpRequest();

    FrameDumpRequest(const FrameDumpRequest&) = delete;
    FrameDumpRequest& operator=(const FrameDumpRequest&) = delete;
    FrameDumpRequest(FrameDumpRequest&&) = delete;
    FrameDumpRequest& operator=(FrameDumpRequest&&) = delete;

private:
    static int dump_asked(sd_bus_message* call, void* self, sd_bus_error* error);

    GuestLink& link_;
    sd_bus_slot* object_ = nullptr;
};

}
