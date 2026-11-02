#include "host/mirror/json_text.hpp"

#include <format>

namespace rwin::host {

void JsonText::separate() {
    if (!fresh_) text_ += ',';
    fresh_ = false;
}

JsonText& JsonText::open_object() {
    separate();
    text_ += '{';
    fresh_ = true;
    return *this;
}

JsonText& JsonText::close_object() {
    text_ += '}';
    fresh_ = false;
    return *this;
}

JsonText& JsonText::open_array() {
    separate();
    text_ += '[';
    fresh_ = true;
    return *this;
}

JsonText& JsonText::close_array() {
    text_ += ']';
    fresh_ = false;
    return *this;
}

JsonText& JsonText::key(std::string_view name) {
    value(name);
    text_ += ':';
    fresh_ = true;
    return *this;
}

JsonText& JsonText::value(std::string_view text) {
    separate();
    text_ += '"';
    for (const char c : text) {
        switch (c) {
            case '"': text_ += "\\\""; break;
            case '\\': text_ += "\\\\"; break;
            case '\n': text_ += "\\n"; break;
            case '\r': text_ += "\\r"; break;
            case '\t': text_ += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    text_ += std::format("\\u{:04x}", static_cast<unsigned>(c));
                } else {
                    text_ += c;
                }
        }
    }
    text_ += '"';
    return *this;
}

JsonText& JsonText::value(bool truth) {
    separate();
    text_ += truth ? "true" : "false";
    return *this;
}

JsonText& JsonText::value(std::int64_t number) {
    separate();
    text_ += std::to_string(number);
    return *this;
}

JsonText& JsonText::value(double number) {
    separate();
    text_ += std::format("{}", number);
    return *this;
}

JsonText& JsonText::null() {
    separate();
    text_ += "null";
    return *this;
}

}
