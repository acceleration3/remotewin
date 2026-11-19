## What this changes

<!-- What the change does, and why. Link the issue it fixes: "Fixes #123". -->

## How it was tested

<!-- The tests you added, and the compositor, GPU and applications you tried it with. -->

## Checklist

- [ ] `ctest --test-dir build -LE integration` passes.
- [ ] For window placement or sizing changes, the integration tests pass too (`ctest --test-dir build -L integration`).
- [ ] A wire change bumps its feature's version, or adds a feature, so both halves negotiate it.
- [ ] Both halves still build from one tree: the host half and the mingw guest half.
- [ ] The code follows [`CLAUDE.md`](https://github.com/acceleration3/remotewin/blob/main/CLAUDE.md).
