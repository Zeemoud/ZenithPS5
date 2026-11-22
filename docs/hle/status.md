# HLE status

No PS5 system library is implemented yet. The only library is a test library used to exercise the mechanism.

| Library | Symbol | Status | Notes |
|---|---|---|---|
| `libzenithtest.so` | `zenith_write(fd, data, length)` | Implemented | Writes to the host's stdout (fd 1) or stderr (fd 2). Returns the byte count, or `~0` on a bad fd, unmapped memory or length over 1 MiB |
| `libzenithtest.so` | `zenith_exit(code)` | Implemented | Ends the program; `runElf` returns `code` |

Anything else a program imports is *unimplemented*: the program loads, and stops with a report naming the
function if it calls it.

When a real library gets its first function, add a section per library here: one row per symbol, with its
status (implemented, stubbed, missing) and what is known to be incomplete.
