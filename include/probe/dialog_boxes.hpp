#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

struct HWND__;

namespace rwin {
struct ProbeAction;
}

namespace rwin::probe {

class InputJournal;

class DialogBoxes {
public:
    explicit DialogBoxes(InputJournal& journal);

    [[nodiscard]] bool run_modal(const ProbeAction& action, std::string& failure);

    [[nodiscard]] HWND__* open_modeless(const ProbeAction& action, std::string& failure);

    [[nodiscard]] bool end(HWND__* dialog, std::string& failure);

private:
    struct Opening;

    static std::intptr_t __stdcall procedure(HWND__* dialog, unsigned message,
                                             std::uintptr_t wparam, std::intptr_t lparam);
    std::intptr_t answer(HWND__* dialog, unsigned message, std::uintptr_t wparam,
                         std::intptr_t lparam);
    void place(HWND__* dialog, const ProbeAction& action) const;
    [[nodiscard]] const void* template_for(const ProbeAction& action);

    InputJournal& journal_;
    std::vector<std::uint16_t> template_;
    std::set<HWND__*> modeless_;
};

}
