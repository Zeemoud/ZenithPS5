# ZenithPS5

An open-source PlayStation 5 emulator written in C++20, built around **high-level emulation (HLE)**.

The name is a nod to Zen 2, the x86-64 CPU architecture of the PS5: guest code runs natively on the host, and the real work lies in re-implementing the system layer, the GPU command stream and the shaders.

> **Status: pre-alpha.** The project structure and tooling are in place; there is nothing runnable yet.

## Goals

- A clean, well-documented, testable architecture, not a one-off hack.
- HLE of the kernel and system libraries, resolved by NID, with an explicit record of what is implemented, stubbed or missing.
- A Vulkan renderer behind an abstract interface.
- Linux first, Windows and macOS later, with all OS-specific code isolated in `src/platform/`.

## Approach

ZenithPS5 loads the original executable at runtime and provides the system libraries itself (HLE), rather than rewriting binaries ahead of time. See [`docs/adr/`](docs/adr/) for the reasoning behind major decisions.

## Roadmap

1. **Foundation**: common types, logging, assertions, build and CI.
2. **Loader**: ELF parsing, segment mapping, relocations, NID resolution.
3. **Memory and CPU**: controlled guest address space, native execution of guest code, HLE call interception.
4. **Kernel HLE**: threads, synchronization, memory, time, filesystem.
5. **Video output**: minimal Vulkan presentation of a framebuffer.
6. **GPU**: command buffer decoding, RDNA shader recompilation to SPIR-V.
7. **Audio, input, storage.**

Validation targets are freely distributable test binaries and homebrew, not commercial games.

## Building

Linux (including WSL2), with a C++20 compiler:

```bash
sudo apt install build-essential cmake ninja-build libgtest-dev
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Available presets: `debug`, `release`, `asan`. There is no runnable target yet.

## Repository layout

| Path | Purpose |
|---|---|
| `src/` | Emulator source, one folder per responsibility |
| `docs/` | Architecture, formats, HLE status, graphics, developer guides |
| `tests/` | Unit and integration tests, test fixtures |
| `tools/` | Developer tooling (NID database generator, trace viewer...) |
| `cmake/` | CMake modules and helpers |
| `third_party/` | External dependencies |

## Legal

ZenithPS5 does **not** include, and will never include, any Sony firmware, code, keys or copyrighted game content. Contributions containing such material will be rejected. Users are expected to run software they have obtained legally.

ZenithPS5 is not affiliated with or endorsed by Sony Interactive Entertainment. "PlayStation" is a trademark of Sony Interactive Entertainment Inc.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) once it exists. In the meantime, open an issue before starting significant work.

## License

[GPL-2.0-only](LICENSE)
