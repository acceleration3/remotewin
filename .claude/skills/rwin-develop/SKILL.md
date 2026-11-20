---
name: rwin-develop
description: Build and test remotewin - the Wayland host client and the mingw guest - with the rwin-dev MCP server, run the unit tests and the integration cases against a headless KWin, compare a running mirror's windows with KWin's, and read frame timing. Use for any edit-build-test loop in remotewin.
---

# Develop remotewin

The `rwin-dev` MCP server (`tools/mcp/rwin_dev.py`) runs the steps as background jobs at four CPUs under the build
lock (`wait_job`, `job_log`, `cancel_job`). Read `CLAUDE.md` first: its C++ rules apply to every change, and every bug
fix ships the test that would have caught it.

## Builds

- `build side=host`: `build/`, the `remotewin` client and the tests. It needs wayland-client, wayland-protocols 1.38
  or newer, xkbcommon, libdrm 2.4.116 or newer and libsystemd.
- `build side=guest`: `build-guest/`, `remotewin-guest.exe` cross-built with `cmake/mingw-w64.cmake`, statically
  linked. `target=remotewin-wgcbench` builds the capture benchmark.
- LSW builds remotewin in its own trees with the tests off; inside LSW use its `lsw-dev` server to build and deploy
  the shipped binaries.

## Tests

- `test kind=unit`: every test except integration, what CI runs. `proto/` is tested host-native: both ends must
  agree byte for byte.
- `test kind=integration`: the resize and window cases against a headless `kwin_wayland --virtual` in a private
  D-Bus session, about 25 s. `case=<name>` runs one (`integration_cases` lists them); `trace=true` adds the host's
  trace log and the Wayland debug log. Exit 77 means skipped (no KWin).
- `ci`: the host build with the unit tests, then the guest cross-build.

## A running mirror

- `window_states` compares what each running `remotewin` reports over D-Bus with what KWin shows, for geometry and
  stacking. It loads a KWin script into the live session for a moment.
- Run the host with `RWIN_LOG=trace RWIN_LOG_FILE=<file>` for its log, and with `RWIN_TIMING_DIR=<dir>` for per-frame
  timing CSVs (LSW's `scripts/analyze-frame-timing.py` summarises them).

## Off limits

`remotewin-probe` and the window-flag conformance matrix stay off unless the user asks: the server refuses to build
the probe.
