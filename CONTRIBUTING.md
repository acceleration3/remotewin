# Contributing to remotewin

Thanks for your interest. remotewin has two halves, the Linux host and the Windows
guest, built from one tree and speaking one wire.

1. **Read the rules first.** [`CLAUDE.md`](CLAUDE.md) has the layout and the C++ rules.
2. **Build and test.** [The README](README.md#building) covers the build.
   `ctest --test-dir build -LE integration` runs the unit tests, and
   `ctest --test-dir build -L integration` the integration tests that resize,
   maximise and move real windows.
3. **Version wire changes.** The halves negotiate named, versioned features when
   they connect. A message change bumps its feature's version, or adds a feature.
4. **Say how you tested it.** Name the compositor, GPU and applications you tried.

**Reporting bugs.** If you use remotewin through LSW, report there, with LSW's log
archive. Otherwise use [the bug form](https://github.com/acceleration3/remotewin/issues/new/choose).

**License.** Contributions are licensed under the [Apache License 2.0](LICENSE).
