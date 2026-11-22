# Getting started

## Requirements

Linux, including Ubuntu on WSL2 (Windows and macOS are not supported yet), on an x86-64 machine:

- GCC 13+ or Clang 17+ (the code uses `std::format`),
- CMake 3.25+ and Ninja,
- GoogleTest (`libgtest-dev`),
- `clang-format` 18, the version the CI uses. On Ubuntu 24.04, `sudo apt install clang-format` gives it.

```bash
sudo apt install build-essential cmake ninja-build libgtest-dev clang-format
```

## Build and test

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The presets are `debug`, `release` and `asan` (Debug with AddressSanitizer and UBSan). Each builds in
`build/<preset>/`. To try Clang: `cmake -S . -B build/clang -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang`.

## Run a program

```bash
./build/debug/bin/zenith build/debug/tests/homebrew/hello.elf
./build/debug/bin/zenith --log-level=debug program.elf
```

Log levels: `trace`, `debug`, `info` (default), `warn`, `error`, `off`. The logs go to stderr, the program's own
output to stdout. If the program crashes, `zenith` prints a report (reason, fault address, registers, the guest
call in progress) and then dies from the original signal.

Guest code is **not sandboxed**: it runs natively in the process with your privileges. Run only programs you
trust, ideally inside a VM (see [`SECURITY.md`](../../SECURITY.md)).

## Format

```bash
scripts/format.sh           # format everything in place
scripts/format.sh --check   # what the CI runs: reports violations, changes nothing
```

Run it before each commit. Different `clang-format` versions format a few constructs differently, which is why
the CI pins version 18.

## Editor setup (VS Code)

The project exports `compile_commands.json` into the build directory. With the `clangd` extension, a `.clangd`
file at the root pointing to it removes false errors:

```yaml
CompileFlags:
  CompilationDatabase: build/debug
```

Disable the C/C++ extension's IntelliSense to avoid duplicated diagnostics
(`"C_Cpp.intelliSenseEngine": "disabled"`).

## Add a guest test program

Small assembly programs in `tests/homebrew/<name>/<name>.S` are built with the host toolchain and run through the
CLI by `ctest`. Register one in `tests/homebrew/CMakeLists.txt`:

```cmake
zenith_add_homebrew(hello_pie "Hello from a relocated guest!" PIE)
```

with the regular expression the output must match, and one of the options `PIE` (position-independent),
`IMPORTS` (linked against the test library, so it has real imports) or `EXPECT_CRASH`.
