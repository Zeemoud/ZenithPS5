# ZenithPS5

An open-source PlayStation 5 emulator written in C++20, built around **high-level emulation (HLE)**.

The name is a nod to Zen 2, the x86-64 CPU architecture of the PS5: guest code runs natively on the host, and the real work lies in re-implementing the system layer, the GPU command stream and the shaders.

> **Status: pre-alpha.** ZenithPS5 can load and run small freestanding x86-64 programs (fixed-address or
> position-independent ELF files), bind their imports to host functions, and report crashes readably. It
> cannot run PS5 software yet: PS5 executable formats, the system libraries and the GPU are not implemented.

## Goals

- A clean, well-documented, testable architecture, not a one-off hack.
- HLE of the kernel and system libraries, resolved by NID, with an explicit record of what is implemented, stubbed or missing.
- A Vulkan renderer behind an abstract interface.
- Linux first, Windows and macOS later, with all OS-specific code isolated in `src/platform/`.

## Approach

ZenithPS5 loads the original executable at runtime and provides the system libraries itself (HLE), rather than rewriting binaries ahead of time. See [`docs/adr/`](docs/adr/) for the reasoning behind major decisions.

## Roadmap

1. **Foundation**: common types, logging, assertions, build and CI. *Done.*
2. **Loader**: ELF parsing, segment mapping, relocations, imports by name. *Done for standard ELF files.*
   Next: several modules that call each other, then PS5 module formats and NID resolution.
3. **Memory and CPU**: guest address space, native execution of guest code, HLE call stubs, crash reports.
   *Done.* Next: thread-local storage and threads.
4. **Kernel HLE**: threads, synchronization, memory, time, filesystem. *Not started.*
5. **Video output**: minimal Vulkan presentation of a framebuffer. *Not started.*
6. **GPU**: command buffer decoding, RDNA shader recompilation to SPIR-V. *Not started.*
7. **Audio, input, storage.** *Not started.*

Validation targets are freely distributable test binaries and homebrew, not commercial games.

## Building

Linux (including WSL2), with a C++20 compiler:

```bash
sudo apt install build-essential cmake ninja-build libgtest-dev
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Available presets: `debug`, `release`, `asan`. The CLI is built as `build/<preset>/bin/zenith` (usage: `zenith program.elf`). See the
[getting started guide](docs/dev/getting-started.md) for requirements, formatting and editor setup.

## Repository layout

| Path | Purpose |
|---|---|
| `src/` | Emulator source, one folder per responsibility |
| `docs/` | [Architecture](docs/architecture/overview.md), [decisions](docs/adr/README.md), [ELF support](docs/formats/elf.md), [HLE](docs/hle/README.md), [developer guides](docs/dev/getting-started.md), [legal policy](docs/legal/policy.md) |
| `tests/` | Unit and integration tests, test fixtures |
| `tools/` | Developer tooling (NID database generator, trace viewer...) |
| `cmake/` | CMake modules and helpers |
| `third_party/` | External dependencies |

## Legal

ZenithPS5 does **not** include, and will never include, any Sony firmware, code, keys or copyrighted game content. Contributions containing such material will be rejected. Users are expected to run software they have obtained legally.

ZenithPS5 is not affiliated with or endorsed by Sony Interactive Entertainment. "PlayStation" is a trademark of Sony Interactive Entertainment Inc.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md). Open an issue before starting significant work. Security issues: [`SECURITY.md`](SECURITY.md).

## License

[GPL-2.0-only](LICENSE)
