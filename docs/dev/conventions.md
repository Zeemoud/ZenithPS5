# Conventions

## Code

- C++20. Every source file starts with `// SPDX-License-Identifier: GPL-2.0-only`.
- Include project headers by their path under `src/`: `#include "memory/address_space.hpp"`. Let
  `scripts/format.sh` order them.
- Names (`.clang-tidy` checks most of them): namespaces `lower_case`, types `CamelCase`, functions and constants
  `camelBack`, variables `lower_case`, private members `lower_case_` with a trailing underscore.
- Expected, recoverable failures (a malformed file, a refused mapping) return `Result<T>`
  (`src/common/result.hpp`). Violated assumptions use `ZENITH_VERIFY` (always checked) or `ZENITH_ASSERT` (debug
  only). `ZENITH_UNIMPLEMENTED` marks a missing piece of the HLE.
- Log with `ZENITH_LOG_*` and a channel (`Loader`, `Memory`, `Cpu`...). The arguments of a filtered message are
  not evaluated.
- Code that depends on the operating system goes in `src/platform/` and nowhere else.
- Modules depend downward only (see the [overview](../architecture/overview.md)). Do not add a dependency
  from a lower module to a higher one.

## Tests

- Every behavior has a test. `tests/unit` mirrors `src/`.
- A test of a failure checks the message when the message is part of the behavior (for example the name of a
  missing function).
- A test that touches guest addresses uses addresses far from the host's own mappings (the tests use
  `0x2000'0000'0000`), and runs in its own process (CTest does this for every test).
- A change that can only be tested with a real program adds one under `tests/homebrew`.

## Commits and pull requests

- Commit messages follow Conventional Commits: `feat(loader): ...`, `fix: ...`, `docs: ...`, `chore: ...`,
  `test: ...`.
- Run `scripts/format.sh` and `ctest` before pushing. The CI runs both, with GCC and Clang.
- A decision that would be hard to guess from the code gets an ADR in `docs/adr/`.
- A new or changed HLE function updates `docs/hle/status.md`.
