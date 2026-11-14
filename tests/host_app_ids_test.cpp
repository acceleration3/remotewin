#include "host/link/guest_windows.hpp"
#include "host/mirror/guest_application.hpp"
#include "host/mirror/host_app_ids.hpp"
#include "host/mirror/mirror_scope.hpp"
#include "proto/window_fields.hpp"
#include "proto/window_id.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using rwin::WindowFields;
using rwin::WindowId;
using rwin::host::GuestApplication;
using rwin::host::GuestWindows;
using rwin::host::HostAppIds;
using rwin::host::MirrorScope;

constexpr std::uint32_t kBrowser = 8740;
constexpr std::uint32_t kContent = 9120;
constexpr std::uint32_t kInstaller = 13268;
constexpr std::uint32_t kClickToRun = 4864;
constexpr const char* kBrowserEntry = "lsw-app-0123456789abcdef";
constexpr const char* kBrowserAumid = "308046B0AF4A39CB";
constexpr const char* kBrowserImage = "C:\\Program Files\\Mozilla Firefox\\firefox.exe";
constexpr const char* kInstallerAumid = "C:\\Users\\lsw\\Downloads\\OfficeSetup.exe";
constexpr const char* kClickToRunAumid =
    "{6D809377-6AF0-444B-8957-A3773F02200E}\\Common Files\\microsoft shared\\ClickToRun\\OfficeClickToRun.exe";
constexpr std::string_view kNamespace = "lsw-guest.";
constexpr std::size_t kMostLength = 80;

constexpr WindowId kBrowserWindow{0x10, 1};
constexpr WindowId kTab{0x20, 2};
constexpr WindowId kSplash{0x30, 3};
constexpr WindowId kProgress{0x40, 4};
constexpr WindowId kWelcome{0x50, 5};
constexpr WindowId kDialog{0x60, 6};

WindowFields made_by(std::uint32_t process, std::string aumid, std::string image,
                     std::vector<WindowFields::ProcessRef> tree = {}) {
    WindowFields fields;
    fields.show = WindowFields::Show::Shown;
    fields.app_id = {std::move(aumid), process, std::move(image), std::move(tree)};
    return fields;
}

std::string derived(std::string aumid, std::string image = {}) {
    return GuestApplication{WindowFields::AppId{std::move(aumid), 1, std::move(image), {}}}.host_app_id();
}

bool valid(const std::string& id) {
    if (id.empty() || id.size() > kMostLength || !id.starts_with(kNamespace)) return false;
    for (std::size_t at = 0; at < id.size(); ++at) {
        const char c = id[at];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        if (!letter && !digit && c != '-' && c != '_' && c != '.') return false;
        if (digit && (at == 0 || id[at - 1] == '.')) return false;
        if (c == '.' && (at + 1 == id.size() || id[at + 1] == '.')) return false;
    }
    return std::count(id.begin(), id.end(), '.') == 1;
}

GuestWindows browser_with_an_installer() {
    GuestWindows windows;
    static_cast<void>(windows.tell(kBrowserWindow, made_by(kBrowser, kBrowserAumid, kBrowserImage, {{kBrowser, 5}})));
    static_cast<void>(
        windows.tell(kTab, made_by(kContent, kBrowserAumid, kBrowserImage, {{kContent, 6}, {kBrowser, 5}})));
    static_cast<void>(windows.tell(kSplash, made_by(kInstaller, kInstallerAumid,
                                                    "C:\\Users\\lsw\\Downloads\\OfficeSetup.exe",
                                                    {{kInstaller, 7}, {kBrowser, 5}})));
    static_cast<void>(windows.tell(kProgress, made_by(kInstaller, kInstallerAumid,
                                                      "C:\\Users\\lsw\\Downloads\\OfficeSetup.exe",
                                                      {{kInstaller, 7}, {kBrowser, 5}})));
    static_cast<void>(windows.tell(
        kWelcome, made_by(kClickToRun, kClickToRunAumid,
                          "C:\\Program Files\\Common Files\\microsoft shared\\ClickToRun\\OfficeClickToRun.exe",
                          {{kClickToRun, 8}, {kInstaller, 7}, {kBrowser, 5}})));
    return windows;
}

void the_launched_application_keeps_the_given_id() {
    const GuestWindows windows = browser_with_an_installer();
    for (const MirrorScope::Launch& launch :
         {MirrorScope::Launch{kBrowser, 5, {}, {}}, MirrorScope::Launch{kBrowser, 5, kBrowserImage, {}},
          MirrorScope::Launch{kBrowser, 5, {}, {kBrowserAumid}}, MirrorScope::Launch{4000, 4, {}, {kBrowserAumid}},
          MirrorScope::Launch{4000, 4, "c:/program files/mozilla firefox/FIREFOX.EXE", {}}}) {
        MirrorScope scope{launch};
        static_cast<void>(scope.claim(windows));
        const HostAppIds ids{kBrowserEntry, scope};
        check(ids.of(kBrowserWindow, windows) == kBrowserEntry, "the launched application's window has the given id");
        check(ids.of(kTab, windows) == kBrowserEntry, "a window of another of its processes has it too");
        check(ids.of(kSplash, windows) != kBrowserEntry && ids.of(kWelcome, windows) != kBrowserEntry,
              "a window of another application it started does not");
    }
}

void another_application_in_the_mirror_has_its_own_id() {
    const GuestWindows windows = browser_with_an_installer();
    MirrorScope scope{MirrorScope::Launch{kBrowser, 5, {}, {}}};
    static_cast<void>(scope.claim(windows));
    check(scope.belongs(kSplash, windows) && scope.belongs(kWelcome, windows), "the installer's windows are mirrored");
    const HostAppIds ids{kBrowserEntry, scope};
    const std::string installer = ids.of(kSplash, windows);
    check(valid(installer), "the installer's id is a valid one in the guest namespace");
    check(installer == ids.of(kProgress, windows), "two windows of the installer share it");
    check(installer == derived(kInstallerAumid), "it is the id derived from the installer's identity");
    check(ids.of(kWelcome, windows) == derived(kClickToRunAumid) && ids.of(kWelcome, windows) != installer,
          "a third application has another");
}

void the_same_application_in_two_mirrors_has_one_id() {
    const GuestWindows windows = browser_with_an_installer();
    MirrorScope browser{MirrorScope::Launch{kBrowser, 5, {}, {}}};
    static_cast<void>(browser.claim(windows));
    MirrorScope unowned{MirrorScope::Launch{kClickToRun, {}, {}, {}}};
    static_cast<void>(unowned.claim(windows));
    MirrorScope other{MirrorScope::Launch{4000, 4, "C:\\Windows\\explorer.exe", {"Some.Other.App"}}};
    static_cast<void>(other.claim(windows));
    const std::string seen = HostAppIds{kBrowserEntry, browser}.of(kWelcome, windows);
    check(seen == HostAppIds{{}, unowned}.of(kWelcome, windows), "a mirror with no id of its own derives the same");
    check(seen == HostAppIds{"lsw-app-fedcba9876543210", other}.of(kWelcome, windows),
          "and so does a mirror of another application");
}

void no_given_id_means_every_window_is_derived() {
    const GuestWindows windows = browser_with_an_installer();
    MirrorScope scope{MirrorScope::Launch{kBrowser, 5, {}, {}}};
    static_cast<void>(scope.claim(windows));
    const HostAppIds ids{{}, scope};
    check(ids.of(kBrowserWindow, windows) == derived(kBrowserAumid), "the launched application's window is derived");
    check(ids.of(kTab, windows) == ids.of(kBrowserWindow, windows), "its windows share that id");
    check(ids.of(kSplash, windows) == derived(kInstallerAumid), "another application's is its own");
    GuestWindows nameless;
    static_cast<void>(nameless.tell(kBrowserWindow, made_by(kBrowser, {}, {})));
    check(ids.of(kBrowserWindow, nameless) == "lsw-guest.unknown", "a window that says nothing still has an id");
    check(HostAppIds{kBrowserEntry, scope}.of(kBrowserWindow, nameless) == kBrowserEntry,
          "and the given one where there is one");
}

void every_window_of_a_mirror_of_everything_has_the_given_id() {
    const GuestWindows windows = browser_with_an_installer();
    MirrorScope scope{MirrorScope::Launch{}};
    static_cast<void>(scope.claim(windows));
    const HostAppIds ids{"given", scope};
    check(ids.of(kBrowserWindow, windows) == "given" && ids.of(kWelcome, windows) == "given",
          "with nothing to tell applications apart by, the given id is every window's");
}

void a_launch_that_shows_nothing_itself_is_its_first_window() {
    GuestWindows windows;
    static_cast<void>(windows.tell(kBrowserWindow, made_by(900, "Vendor.App", "C:\\App\\app.exe", {{900, 9}, {800, 8}})));
    MirrorScope scope{MirrorScope::Launch{800, 8, "C:\\App\\Update.exe", {}}};
    static_cast<void>(scope.claim(windows));
    static_cast<void>(windows.tell(kSplash, made_by(901, "Other.Tool", "C:\\App\\tool.exe", {{901, 10}, {900, 9}, {800, 8}})));
    static_cast<void>(scope.claim(windows));
    const HostAppIds ids{kBrowserEntry, scope};
    check(ids.of(kBrowserWindow, windows) == kBrowserEntry,
          "the first application a windowless launcher shows is the launched one");
    check(ids.of(kSplash, windows) == derived("Other.Tool"), "what that one starts later is another");
    GuestWindows child_first = browser_with_an_installer();
    MirrorScope named{MirrorScope::Launch{4000, 4, {}, {kBrowserAumid}}};
    GuestWindows installer_only;
    static_cast<void>(installer_only.tell(kSplash, child_first.find(kSplash)->fields()));
    static_cast<void>(named.claim(installer_only));
    check(HostAppIds{kBrowserEntry, named}.of(kSplash, installer_only) == derived(kInstallerAumid),
          "a launch that names its application never takes another's window for it");
}

void an_owned_window_has_its_owners_id() {
    GuestWindows windows = browser_with_an_installer();
    WindowFields dialog = made_by(kClickToRun, kClickToRunAumid, {});
    dialog.owner = kSplash;
    static_cast<void>(windows.tell(kDialog, dialog));
    WindowFields nested = made_by(kBrowser, kBrowserAumid, kBrowserImage, {{kBrowser, 5}});
    nested.owner = kDialog;
    static_cast<void>(windows.tell(WindowId{0x70, 7}, nested));
    WindowFields hidden = made_by(kInstaller, kInstallerAumid, {});
    hidden.owner = WindowId::hidden_owner();
    static_cast<void>(windows.tell(WindowId{0x80, 8}, hidden));
    WindowFields gone = made_by(kInstaller, kInstallerAumid, {});
    gone.owner = WindowId{0x999, 9};
    static_cast<void>(windows.tell(WindowId{0x90, 9}, gone));
    MirrorScope scope{MirrorScope::Launch{kBrowser, 5, {}, {}}};
    static_cast<void>(scope.claim(windows));
    const HostAppIds ids{kBrowserEntry, scope};
    check(ids.of(kDialog, windows) == ids.of(kSplash, windows), "a dialog is its owner's application here");
    check(ids.of(WindowId{0x70, 7}, windows) == ids.of(kSplash, windows), "and so is what the dialog owns");
    check(ids.of(WindowId{0x80, 8}, windows) == derived(kInstallerAumid)
              && ids.of(WindowId{0x90, 9}, windows) == derived(kInstallerAumid),
          "a hidden or unknown owner leaves the window its own");
    WindowFields a = made_by(kInstaller, kInstallerAumid, {});
    a.owner = WindowId{0xb0, 11};
    WindowFields b = made_by(kInstaller, kInstallerAumid, {});
    b.owner = WindowId{0xa0, 10};
    static_cast<void>(windows.tell(WindowId{0xa0, 10}, a));
    static_cast<void>(windows.tell(WindowId{0xb0, 11}, b));
    check(ids.of(WindowId{0xa0, 10}, windows) == derived(kInstallerAumid), "owners in a loop end");
}

void an_identity_that_changes_changes_the_id() {
    GuestWindows windows = browser_with_an_installer();
    MirrorScope scope{MirrorScope::Launch{kBrowser, 5, {}, {}}};
    static_cast<void>(scope.claim(windows));
    const HostAppIds ids{kBrowserEntry, scope};
    WindowFields late = made_by(kInstaller, {}, "C:\\Users\\lsw\\Downloads\\OfficeSetup.exe", {{kInstaller, 7}, {kBrowser, 5}});
    static_cast<void>(windows.tell(kSplash, late));
    const std::string by_image = ids.of(kSplash, windows);
    check(valid(by_image) && by_image == ids.of(kSplash, windows), "an image alone gives an id, the same each time");
    late.app_id.aumid = "Microsoft.Office.Setup_8wekyb3d8bbwe!Setup";
    check(windows.tell(kSplash, late).changed == WindowFields::kAppId, "the identity said later is a change");
    static_cast<void>(scope.claim(windows));
    const std::string by_aumid = ids.of(kSplash, windows);
    check(by_aumid != by_image && by_aumid == derived("Microsoft.Office.Setup_8wekyb3d8bbwe!Setup"),
          "the id follows the identity");
    check(by_aumid == ids.of(kSplash, windows), "and stays until the next change");
    late.app_id.aumid = kBrowserAumid;
    static_cast<void>(windows.tell(kSplash, late));
    check(ids.of(kSplash, windows) == kBrowserEntry, "a window that turns out the launched application's gets its id");
}

void derived_ids_are_valid_and_tell_applications_apart() {
    const std::string long_path = "C:\\Program Files\\" + std::string(400, 'd') + "\\"
                                  + std::string(300, 'N') + " setup (x64).exe";
    const std::vector<std::string> identities{
        kBrowserAumid,
        kInstallerAumid,
        kClickToRunAumid,
        "Microsoft.WindowsNotepad_8wekyb3d8bbwe!App",
        "Microsoft.WindowsCalculator_8wekyb3d8bbwe!App",
        "C:\\Program Files (x86)\\Some Vendor\\My App 2.exe",
        "C:\\Program Files\\7-Zip\\7zFM.exe",
        "D:\\Spiele\\\xC3\x9C" "berpr\xC3\xBC" "fung\\\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E.exe",
        "D:\\Games\\Stra\xC3\x9F" "e.exe",
        "C:\\Tools\\...exe",
        "C:\\Tools\\-.-.exe",
        "Vendor.Product.Sub.1",
        "\\",
        long_path,
    };
    std::vector<std::string> ids;
    for (const std::string& identity : identities) {
        const std::string id = derived(identity);
        if (!valid(id)) std::printf("FAIL: '%s' is no valid id\n", id.c_str());
        check(valid(id), "a derived id is valid");
        check(std::find(ids.begin(), ids.end(), id) == ids.end(), "and no other application's");
        ids.push_back(id);
    }
    check(derived(kInstallerAumid).starts_with("lsw-guest.officesetup-"), "a path gives its program's name");
    check(derived(kClickToRunAumid).starts_with("lsw-guest.officeclicktorun-"), "a known folder path too");
    check(derived("Microsoft.WindowsNotepad_8wekyb3d8bbwe!App").starts_with("lsw-guest.microsoft-windowsnotepad-"),
          "a packaged application gives its package's name");
    check(derived(kBrowserAumid).starts_with("lsw-guest._308046b0af4a39cb-"), "a name is never led by a digit");
    check(derived("C:\\Program Files\\7-Zip\\7zFM.exe").starts_with("lsw-guest._7zfm-"), "nor a program's");
    check(derived({}, kBrowserImage).starts_with("lsw-guest.firefox-"), "with no application id the image names it");
    check(derived("c:/users/LSW/downloads/officesetup.EXE") == derived(kInstallerAumid),
          "case and slashes do not make another application");
    check(derived(kInstallerAumid) != derived("C:\\Users\\lsw\\Desktop\\OfficeSetup.exe"),
          "the same program name elsewhere is another application");
    check(!derived(kInstallerAumid).starts_with("lsw-app-") && derived(kInstallerAumid) != "lsw-manager",
          "no derived id is one of the manager's entries");
}

}

int main() {
    the_launched_application_keeps_the_given_id();
    another_application_in_the_mirror_has_its_own_id();
    the_same_application_in_two_mirrors_has_one_id();
    no_given_id_means_every_window_is_derived();
    every_window_of_a_mirror_of_everything_has_the_given_id();
    a_launch_that_shows_nothing_itself_is_its_first_window();
    an_owned_window_has_its_owners_id();
    an_identity_that_changes_changes_the_id();
    derived_ids_are_valid_and_tell_applications_apart();
    if (failures == 0) std::printf("host app ids: all checks passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
