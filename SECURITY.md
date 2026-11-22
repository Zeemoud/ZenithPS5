# Security

## Guest programs are not sandboxed

ZenithPS5 runs guest code **natively, in the host process, with your user's privileges**. A guest program can
do anything the emulator can: read and write your files, use the network, crash the process. The memory
protections of the guest address space protect the emulator's own data structures from mistakes, not from a
malicious program.

**Only run programs you trust, and prefer a virtual machine for anything else.**

## Reporting a vulnerability

Please report security problems **privately**, not in a public issue: use GitHub's private vulnerability
reporting (the *Security* tab of the repository, then *Report a vulnerability*).

Useful reports include a way to reproduce the problem with a freely distributable input, the commit you tested,
and your system. Examples of what we want to know about:

- a crafted ELF file that makes the loader read or write outside its buffers,
- an HLE function that trusts a guest pointer or size it should have checked,
- anything that lets a guest program affect the emulator's memory in a way that is not already possible by
  design.

Because the project is pre-alpha, only the latest commit on `main` is supported. There is no security release
process yet; fixes land on `main`.
