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

// Dialog boxes as an application makes them from a resource: from a
// template in memory, by DialogBoxIndirectParam for a modal one - Windows
// disables the owner and runs its own modal loop - and by
// CreateDialogIndirectParam for a modeless one, which leaves the owner as
// it was. No controls: the dialog is its frame and its face, and what
// Windows does to it and its owner is the point. Every input message a
// dialog is given is noted in the journal, as a probe window's are.
class DialogBoxes {
public:
    explicit DialogBoxes(InputJournal& journal);

    // Returns when the dialog ends - EndDialog, or destroyed with its
    // owner. False when it was never made; ``failure`` says why.
    [[nodiscard]] bool run_modal(const ProbeAction& action, std::string& failure);

    [[nodiscard]] HWND__* open_modeless(const ProbeAction& action, std::string& failure);

    // EndDialog for a modal dialog, DestroyWindow for a modeless one: how
    // each is meant to end.
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
    // DLGTEMPLATE and what follows it, as WORDs; rebuilt for each dialog.
    std::vector<std::uint16_t> template_;
    std::set<HWND__*> modeless_;
};

}  // namespace rwin::probe
