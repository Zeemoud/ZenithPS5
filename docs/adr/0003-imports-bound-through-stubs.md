# 0003. Imports are bound by name through stubs, and missing functions are named

Status: accepted

## Context

A dynamically linked program refers to functions it does not contain. The loader must give each of them an
address. A real system library may be large, and for a long time only a small part of it will be implemented.
A program that jumps to an unresolved address crashes with no hint of what was missing.

## Decision

- The host provides **libraries** (`DT_NEEDED` names) and registers **functions** under symbol names in an
  `ImportTable`. Symbol lookup is global, as in ELF.
- Binding is **eager**: at load time, each import gets the address of a small stub in guest memory, and GOT and
  PLT slots are written with it. A stub for an implemented function jumps to the host function.
- A symbol the host does not implement still gets a stub. The program loads and runs until it actually calls
  it; then the program stops and the report names the missing function.
- A program that needs a library the host does not provide is refused at load time, naming the library.

## Consequences

- The report of a missing function says exactly what to implement next.
- Stubs carry an identifier (loaded into the first argument register) so a single host function serves all the
  unimplemented ones. The call's own first argument is lost, which does not matter because the program stops.
- Lazy binding is not emulated: PLT slots are bound at load time.
- This mechanism handles standard ELF imports. How PS5 modules name their imports (hashed identifiers and
  Sony-specific dynamic tags) is a separate layer that will sit on top, mapping those identifiers to symbol
  names before they reach the `ImportTable`.
