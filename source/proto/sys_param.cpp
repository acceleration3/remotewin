#include "proto/sys_param.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void SysParam::write(MessageWriter& out) const {
    out.put_u32(static_cast<std::uint32_t>(param));
    switch (param) {
        case Param::WorkArea:
            work_area.write(out);
            return;
        case Param::DragFullWindows:
            out.put_u8(enabled ? 1 : 0);
            return;
    }
}

std::optional<SysParam> SysParam::read(MessageReader& in) {
    SysParam setting;
    const std::uint32_t param = in.u32();
    if (!in.ok()) return std::nullopt;
    if (param == static_cast<std::uint32_t>(Param::WorkArea)) {
        const std::optional<Rect> area = Rect::read(in);
        if (!area.has_value()) return std::nullopt;
        setting.param = Param::WorkArea;
        setting.work_area = *area;
        return setting;
    }
    if (param == static_cast<std::uint32_t>(Param::DragFullWindows)) {
        setting.param = Param::DragFullWindows;
        setting.enabled = in.u8() != 0;
        if (!in.ok()) return std::nullopt;
        return setting;
    }
    return std::nullopt;
}

}  // namespace rwin
