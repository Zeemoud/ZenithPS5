# HLE: the libraries the host provides

The HLE is the set of host functions that stand in for the system libraries a program calls. This page explains
how it works and how to add a function. [`status.md`](status.md) lists what exists.

## Concepts

- A **library** has a name (the `DT_NEEDED` entry programs are linked against, for example `libkernel.so`). The
  host can provide it, as functions registered here. A library the host does not provide is loaded from its file
  like the program itself ([ADR 0004](../adr/0004-programs-and-libraries-loaded-together.md)); the host
  libraries take precedence when a name is both.
- A **function** is a C++ function registered under a **symbol name**. Lookup is global, as in ELF: a symbol is
  found whichever library it was registered under.
- The `ImportTable` (`src/core/imports.hpp`) holds both. When a program is loaded, each import it uses gets a
  stub in guest memory: for an implemented function the stub jumps to it, for any other symbol it stops the
  program and names the missing function ([ADR 0003](../adr/0003-imports-bound-through-stubs.md)).

## Writing a host function

```cpp
#include "cpu/stub_table.hpp"

u64 hostAdd(u64 a, u64 b, u64, u64, u64, u64) noexcept {
    return a + b;
}

// where the library is registered:
imports.addFunction("libexample.so", "example_add", hostAdd);
```

The rules:

- The signature is `cpu::HostFunction`: six integer arguments (the System V argument registers RDI, RSI, RDX,
  RCX, R8, R9), and the return value goes to RAX.
- It runs **natively on the guest stack**. Do not use a lot of stack.
- It must be `noexcept`: an exception cannot cross guest frames.
- A guest pointer is a host pointer (identity mapping), but it comes from untrusted code. Check it before use,
  as `hostWrite` in `src/core/bridge.cpp` does with `AddressSpace::isMapped`.
- To end the program from inside a host function, call `cpu::exitGuest(code)`. It does not return and it
  abandons the frames in between, so call it when nothing in them needs cleanup.
- Log with the `Libs` (or a more specific) channel: `ZENITH_LOG_DEBUG(Libs, "...")`.

Add a test that calls the function through a real import (see `tests/unit/core/test_run_elf.cpp`), and update
[`status.md`](status.md).

## The test bridge

`src/core/bridge.hpp` also exposes the same two test services at fixed addresses (`0x100000` and `0x100010`),
for the oldest test programs. It is a test scaffold, not a PS5 interface, and will go away once the programs
that use it are converted to imports.
