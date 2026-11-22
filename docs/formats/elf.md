# ELF support

What the loader (`src/loader/elf`) accepts today. Anything not listed is refused with a message, or ignored
where noted.

## File

| Feature | Support |
|---|---|
| Class, byte order, machine | ELF64, little-endian, x86-64 only |
| `ET_EXEC` (fixed addresses) | Yes: segments go where they were linked |
| `ET_DYN` (position-independent) | Yes: loaded at a base address chosen by the host (default `0x400000`) |
| Other types (`ET_REL`, `ET_CORE`...) | Refused |
| Program headers | 1 to 256, each 56 bytes |

## Segments

| Segment | Support |
|---|---|
| `PT_LOAD` | Mapped with the permissions of its flags. The bytes beyond the file data are zero (`.bss`) |
| `PT_DYNAMIC` | Parsed for `ET_DYN` |
| `PT_NOTE`, `PT_GNU_STACK`, `PT_GNU_RELRO`... | Ignored. In particular, `RELRO` is not applied: relocated data stays writable |
| `PT_TLS` | Ignored: thread-local storage is not set up |

A segment does not need to start on a page boundary, but two segments cannot share a 16 KiB guest page.
Linking with `-z max-page-size=0x4000` keeps them apart. The entry point must be inside an executable segment,
and every address must be below 2^47.

## Dynamic section

| Tag | Support |
|---|---|
| `DT_NEEDED` | The library must be provided by the host (see [HLE](../hle/README.md)), else the program is refused |
| `DT_SYMTAB`, `DT_STRTAB`, `DT_STRSZ`, `DT_SYMENT` | Read (`DT_SYMENT` must be 24) |
| `DT_RELA`, `DT_RELASZ`, `DT_RELAENT` | Read (`DT_RELAENT` must be 24) |
| `DT_JMPREL`, `DT_PLTRELSZ` | Read: the PLT relocations, bound at load time |
| `DT_RELR`, `DT_RELRSZ` | Detected, refused (compact relocations are not supported) |
| `DT_INIT`, `DT_INIT_ARRAY`... | Ignored: only the entry point runs, constructors do not |
| Others | Ignored |

## Relocations

| Type | Meaning | Support |
|---|---|---|
| `R_X86_64_NONE` | nothing | Skipped |
| `R_X86_64_RELATIVE` | `base + addend` | Yes |
| `R_X86_64_64` | `symbol + addend` | Yes |
| `R_X86_64_GLOB_DAT` | `symbol` (GOT entry) | Yes |
| `R_X86_64_JUMP_SLOT` | `symbol` (PLT entry) | Yes, bound eagerly |
| Any other | | Refused with the type number |

A symbol defined by the program itself resolves to its own address. An imported symbol is resolved by name
through the host ([ADR 0003](../adr/0003-imports-bound-through-stubs.md)). An unresolved *weak* import
becomes 0.

## Not covered here

PS5 executables use their own container and dynamic tags. They are not handled by this loader, and no
decryption or signature handling is, or will be, part of this project (see the
[legal policy](../legal/policy.md)).
