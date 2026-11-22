# 0001. HLE at runtime instead of static recompilation

Status: accepted

## Context

There are two ways to run a PS5 program without the console:

- **High-level emulation (HLE) at runtime**: load the original executable when the emulator starts, and provide
  the system libraries it calls as host functions.
- **Static recompilation**: rewrite the executable ahead of time into a native binary, linked against
  reimplemented system libraries.

On the PS5 the guest CPU is x86-64, the same architecture as the supported hosts. Neither approach needs to
translate CPU instructions. In both, the real work is the system layer (kernel, libraries), the GPU command
stream and the shaders, and that work is the same.

## Decision

Load the program at runtime and implement the system layer as host functions (HLE).

## Consequences

- A program that fails can be debugged by running it as it is, with a log of every call it makes. There is no
  build step between the original program and what runs.
- Everything built for the system layer, the GPU and the shaders would be reused if static recompilation were
  ever added: only the loader would differ.
- Rewriting x86-64 code into native x86-64 code ahead of time brings little: since the CPU is not translated,
  the cost lies in the GPU and the shaders either way.
- The host must be x86-64: guest code is executed natively. An ARM host would need a CPU translator, which is
  not planned.
