# Contributing to ZenithPS5

Thank you for helping. ZenithPS5 is young, so the most useful contributions are small, tested and documented.

## Before you start

- **Open an issue first** for anything significant, so we can agree on the approach before you write code.
- Read the [legal policy](docs/legal/policy.md). Nothing that comes from Sony software (firmware, libraries,
  keys, games, dumps, disassembly) belongs in an issue, a pull request or a commit.
- Read the [architecture overview](docs/architecture/overview.md) and the [conventions](docs/dev/conventions.md).

## Setting up

The [getting started guide](docs/dev/getting-started.md) covers the requirements, building, testing and the
editor setup. In short:

```bash
sudo apt install build-essential cmake ninja-build libgtest-dev clang-format
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
```

## Making a change

1. Create a branch from `main`.
2. Write the change **with its tests**. A behavior without a test is not finished.
3. Format: `scripts/format.sh`. The CI runs `scripts/format.sh --check` with `clang-format` 18.
4. Build and test, at least with the `debug` preset. The `asan` preset catches memory errors that the other
   presets hide.
5. Update the documentation when it changes: `docs/hle/status.md` for an HLE function, `docs/formats/` for a
   format, an ADR in `docs/adr/` for a decision that is hard to guess from the code.
6. Write commit messages in the Conventional Commits style (`feat(loader): ...`, `fix: ...`, `docs: ...`).
7. Open a pull request and fill in the checklist. It must pass the CI: formatting, and a build with GCC and
   Clang, in Debug and Release.

## What makes a good pull request

- It does one thing. Refactoring and new behavior are separate pull requests.
- It keeps the dependency direction between modules (lower modules never depend on higher ones).
- It keeps OS-specific code in `src/platform/`.
- A failure path returns an error with a useful message, not a crash.

## Reporting bugs

Use the bug report form. Include the version or commit, your system, the exact command, and the log. Use
`--log-level=debug` if you can. Use only freely distributable test programs.

For security issues, see [`SECURITY.md`](SECURITY.md).

## License

By contributing, you agree that your contribution is licensed under the GPL-2.0-only license of the project.
