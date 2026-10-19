#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace rwin::host {

// JSON written out as it goes: objects and arrays opened and closed in
// order, each value after its key in an object, commas where they belong.
class JsonText {
public:
    JsonText& open_object();
    JsonText& close_object();
    JsonText& open_array();
    JsonText& close_array();
    JsonText& key(std::string_view name);
    JsonText& value(std::string_view text);
    JsonText& value(const char* text) { return value(std::string_view(text)); }
    JsonText& value(bool truth);
    JsonText& value(std::int64_t number);
    JsonText& value(double number);
    JsonText& null();

    [[nodiscard]] const std::string& text() const noexcept { return text_; }

private:
    void separate();

    std::string text_;
    // Nothing written yet in the object or array now open, or a key just
    // written: no comma before what comes next.
    bool fresh_ = true;
};

}  // namespace rwin::host
