# 0004. A program and its libraries are mapped first, then relocated against one global scope

Status: accepted

## Context

A program calls functions that live in shared libraries, and those libraries may call each other, the host, or
even the program. To bind an import, the loader needs the *address* of the symbol that satisfies it, so the
module that exports it must already be mapped. If each module were fully loaded before the next one, a
dependency cycle (or simply a library that uses a symbol of a library loaded after it) could not be resolved.

Library names also come from the program, which is untrusted, and linkers leave the entry point of a library
meaningless.

## Decision

- The loader exposes its work in steps (`mapModule`, `exportedSymbols`, `relocateModule`, `protectModule`,
  `unmapModule`); `loadElf` is the single-module composition of them. `core::loadProgram` orchestrates several
  modules: **map all, collect all exports, relocate all, protect all**.
- Libraries are found breadth first from the program's `DT_NEEDED` entries, each loaded once, placed one after
  the other (each on a 64 KiB boundary). A library the host provides wins over a file of the same name.
- Symbols are looked up in a **single global scope**, as in ELF: modules in load order (the program first), then
  the host libraries, then an unimplemented stub ([ADR 0003](0003-imports-bound-through-stubs.md)). The first
  definition wins.
- The number of exported symbols is read from `DT_GNU_HASH` or `DT_HASH`, not guessed from the layout of the
  file.
- A library is looked up by **plain file name only**, in the library directories: no separators, no `.` or `..`.
- Only the program's entry point must be in an executable segment.
- If anything fails, every module mapped so far is unmapped.

## Consequences

- Cycles between libraries work, and so does a library that imports from the program.
- The load order is the interposition order: the same rule as ELF, and predictable.
- Initialization functions (`DT_INIT`, `DT_INIT_ARRAY`) of libraries are not run, so a library that depends on
  constructors will misbehave. This is the main known gap, to close before real system libraries.
- There is no unloading, and no lazy binding.
- A hostile program cannot make the host read arbitrary files through a library name.
