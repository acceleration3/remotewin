#!/usr/bin/env python3
"""Every running mirror's windows, three ways, side by side.

  guest   every window the guest half has told of, topmost first: owner,
          styles, rectangle, state (what Windows says), and whether it is
          the mirrored application's
  mirror  each mirror's own idea of the window: the role its surface has
          (toplevel, popup, subsurface) and what it is placed against, the
          state the compositor gave it and the state this side asked for
          (org.remotewin.Windows.Describe on the mirror's bus connection)
  kwin    the compositor's: stacking order, transient parent, layer,
          minimised and active (a KWin script, read back from the journal)

Then the disagreements between them, each on one line. Toplevels are
matched to KWin by the handle tag in their title; popups, which have no
title, by process and size. Windows are named "hwnd:serial", as the
logs name them.

  tools/window_states.py            # every mirror
  tools/window_states.py --json     # the three views as JSON
  tools/window_states.py 1421463    # one mirror, by its host process id
"""
import json
import os
import subprocess
import sys
import tempfile
import time
import uuid

MARKER = "⁣"
DIGITS = "​‌‍⁠"


def run(*args, check=True):
    return subprocess.run(args, capture_output=True, text=True, check=check).stdout


def mirror_pids(wanted):
    pids = [int(p) for p in run("pgrep", "-x", "remotewin", check=False).split()]
    return [p for p in pids if not wanted or p in wanted]


def bus_names(pid):
    names = []
    for line in run("busctl", "--user", "list", "--no-legend", "--no-pager").splitlines():
        fields = line.split()
        if len(fields) > 1 and fields[1] == str(pid) and fields[0].startswith(":"):
            names.append(fields[0])
    return names


def describe(pid):
    for name in bus_names(pid):
        out = subprocess.run(["busctl", "--user", "--json=short", "call", name, "/org/remotewin/Windows",
                              "org.remotewin.Windows", "Describe"], capture_output=True, text=True)
        if out.returncode == 0:
            return json.loads(json.loads(out.stdout)["data"][0])
    return None


KWIN_SCRIPT = r"""
const token = "%TOKEN%";
const pids = %PIDS%;
const marker = String.fromCharCode(0x2063);
function tagOf(w) {
    const caption = w.captionNormal || w.caption || "";
    const at = caption.indexOf(marker);
    return at < 0 ? "" : caption.substring(at);
}
const order = workspace.stackingOrder;
for (let i = 0; i < order.length; ++i) {
    const w = order[i];
    if (pids.indexOf(w.pid) < 0) continue;
    const g = w.frameGeometry;
    const t = w.transientFor;
    print(token + JSON.stringify({
        z: i, pid: w.pid, id: String(w.internalId), tag: tagOf(w),
        caption: String(w.caption), geometry: [g.x, g.y, g.width, g.height],
        transient_for: t ? String(t.internalId) : null,
        minimized: w.minimized, active: w.active, keep_above: w.keepAbove,
        popup: w.popupWindow, normal: w.normalWindow, dialog: w.dialog, utility: w.utility,
        hidden: w.hidden, skip_taskbar: w.skipTaskbar, layer: w.layer, wants_input: w.wantsInput
    }));
}
print(token + "done");
"""


def kwin(pids):
    token = "RWINSTATE" + uuid.uuid4().hex[:8] + " "
    script = KWIN_SCRIPT.replace("%TOKEN%", token).replace("%PIDS%", json.dumps(pids))
    with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as f:
        f.write(script)
        path = f.name
    name = "rwin-states-" + token.strip()
    since = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(time.time() - 1))
    try:
        loaded = run("busctl", "--user", "call", "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting",
                     "loadScript", "ss", path, name)
        script_id = loaded.split()[-1]
        run("busctl", "--user", "call", "org.kde.KWin", f"/Scripting/Script{script_id}",
            "org.kde.kwin.Script", "run")
        lines = []
        for _ in range(20):
            time.sleep(0.1)
            text = run("journalctl", "--user", "--no-pager", "-o", "cat", "--since", since)
            lines = [l[l.index(token) + len(token):] for l in text.splitlines() if token in l]
            if "done" in lines:
                break
        run("busctl", "--user", "call", "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting",
            "unloadScript", "s", name, check=False)
    finally:
        os.unlink(path)
    return [json.loads(l) for l in lines if l != "done"]


# A window's tag is its WindowId's key: hwnd in the high 32 bits, serial
# in the low, two bits a character.
TAG_DIGITS = 32


def handle_of_tag(tag):
    if len(tag) != TAG_DIGITS + 1 or tag[0] != MARKER:
        return None
    value = 0
    for char in tag[1:]:
        if char not in DIGITS:
            return None
        value = (value << 2) | DIGITS.index(char)
    return value


def key_of(name):
    hwnd, serial = name.split(":")
    return (int(hwnd, 16) << 32) | int(serial)


def name_of(key):
    return f"{hex(key >> 32)}:{key & 0xFFFFFFFF}"


def match_kwin(report, windows):
    by_handle = {}
    toplevels = {}
    for w in windows:
        h = handle_of_tag(w["tag"])
        if h is not None:
            toplevels[w["id"]] = h
    popups = [w for w in windows if handle_of_tag(w["tag"]) is None]
    for w in windows:
        h = toplevels.get(w["id"])
        if h is not None:
            by_handle[h] = w
    for entry in report["windows"]:
        handle = key_of(entry["handle"])
        mirror = entry["mirror"]
        if handle in by_handle or mirror is None:
            continue
        size = mirror["logical_size"]
        best = None
        for w in popups:
            if w.get("_taken"):
                continue
            dw, dh = abs(w["geometry"][2] - size[0]), abs(w["geometry"][3] - size[1])
            if dw <= 2 and dh <= 2 and (best is None or dw + dh < best[0]):
                best = (dw + dh, w)
        if best:
            best[1]["_taken"] = True
            by_handle[handle] = best[1]
    unmatched = [w for w in windows if handle_of_tag(w["tag"]) is None and not w.get("_taken")]
    return by_handle, toplevels, unmatched


def compare(report, kwin_windows):
    by_handle, toplevels, unmatched = match_kwin(report, kwin_windows)
    id_to_handle = {w["id"]: (toplevels.get(w["id"]) or next(
        (h for h, kw in by_handle.items() if kw is w), None)) for w in kwin_windows}
    lines, issues = [], []
    guest_order = []
    for entry in report["windows"]:
        h = key_of(entry["handle"])
        g, m, k = entry["guest"], entry["mirror"], by_handle.get(h)
        if not g["belongs"] and m is None:
            continue
        guest_order.append(h)
        flags = [f for f in ("owned", "stands_alone", "activates", "topmost", "disabled", "tool_window",
                             "in_taskbar", "hidden", "minimized", "maximized", "active") if g.get(f)]
        lines.append(f"{entry['handle']} '{g['title']}' [{g['class'][:40]}] pid={g['pid']} app={g['aumid']}")
        lines.append(f"    guest   owner={g['owner']} anchor={g['anchor']} rect={g['rect']} style={g['style']}"
                     f" ex={g['ex_style']} {' '.join(flags)}")
        if not g["belongs"]:
            issues.append(f"{entry['handle']}: not the application's, and mirrored")
        if m is None:
            lines.append("    mirror  none")
            if not g["hidden"]:
                issues.append(f"{entry['handle']}: the guest shows it and there is no mirror")
        else:
            mflags = [f for f in ("shown", "mapped", "activated", "maximized", "fullscreen", "asked_minimized",
                                  "asked_maximized", "asked_fullscreen", "modal", "grabs") if m.get(f)]
            lines.append(f"    mirror  {m['role']} parent={m['parent']} placed={m['placed']}"
                         f" size={m['logical_size'][2:]} scale={m['scale']:.2f} frames={m['frames_presented']}"
                         f" {' '.join(mflags)}")
        if k is None:
            lines.append("    kwin    none")
            if m is not None and m["mapped"]:
                issues.append(f"{entry['handle']}: the mirror is visible and KWin has no window for it")
        else:
            parent = id_to_handle.get(k["transient_for"]) if k["transient_for"] else None
            kflags = [f for f in ("popup", "normal", "dialog", "utility", "minimized", "active", "keep_above",
                                  "hidden", "skip_taskbar") if k.get(f)]
            lines.append(f"    kwin    z={k['z']} layer={k['layer']} geometry={[round(v) for v in k['geometry']]}"
                         f" transient_for={name_of(parent) if parent else k['transient_for']} {' '.join(kflags)}"
                         f"{'' if k['wants_input'] else ' no-focus'}")
        if m is not None:
            if g["owned"] and not g["stands_alone"] and m["role"] == "toplevel" and m["parent"] is None:
                issues.append(f"{entry['handle']}: owned without standing alone, mirrored as a toplevel of its own")
            if g["minimized"] != m["asked_minimized"]:
                issues.append(f"{entry['handle']}: minimised in the guest={g['minimized']}, "
                              f"asked here={m['asked_minimized']}")
        if k is not None and m is not None:
            if k["minimized"] != m["asked_minimized"]:
                issues.append(f"{entry['handle']}: KWin minimised={k['minimized']}, the mirror asked "
                              f"{m['asked_minimized']}")
            if g["owned"] and not k["transient_for"]:
                issues.append(f"{entry['handle']}: owned by {g['owner']} in the guest, transient for nothing "
                              "in KWin")
    # Stacking: the guest's order of the windows KWin has, against KWin's.
    shown = [h for h in guest_order if h in by_handle and not by_handle[h]["minimized"]]
    kwin_order = sorted(shown, key=lambda h: -by_handle[h]["z"])
    if shown != kwin_order:
        issues.append("stacking differs: guest top→bottom " + " ".join(name_of(h) for h in shown)
                      + " | KWin top→bottom " + " ".join(name_of(h) for h in kwin_order))
    for w in unmatched:
        issues.append(f"KWin window {w['id']} {[round(v) for v in w['geometry']]} matches no guest window")
    return lines, issues


def main(argv):
    as_json = "--json" in argv
    wanted = {int(a) for a in argv if a.isdigit()}
    pids = mirror_pids(wanted)
    if not pids:
        print("no remotewin running", file=sys.stderr)
        return 1
    reports = {pid: describe(pid) for pid in pids}
    kwin_windows = kwin(pids)
    if as_json:
        print(json.dumps({"mirrors": reports, "kwin": kwin_windows}, indent=2))
        return 0
    for pid, report in reports.items():
        print(f"== remotewin {pid}")
        if report is None:
            print("   no window report on the bus (a build from before org.remotewin.Windows, or --desktop)")
            continue
        print(f"   app id {report['app_id']}")
        lines, issues = compare(report, [w for w in kwin_windows if w["pid"] == pid])
        for line in lines:
            print("   " + line)
        print("   disagreements:" if issues else "   no disagreements")
        for issue in issues:
            print("     - " + issue)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
