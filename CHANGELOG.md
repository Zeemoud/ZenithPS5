# Changelog

All notable changes are listed here. The format follows [Keep a Changelog](https://keepachangelog.com/), and
the project will use [Semantic Versioning](https://semver.org/) once it makes releases.

## [Unreleased]

### Added

- **Foundation**: base types, per-channel logging, assertions, `Result<T>`; CMake presets (`debug`, `release`,
  `asan`); CI with GCC and Clang, and a formatting check with `clang-format` 18.
- **Guest memory**: a host virtual memory wrapper for Linux, and an `AddressSpace` that identity-maps guest
  regions in 16 KiB pages and never replaces an existing host mapping.
- **ELF loader**: fixed-address (`ET_EXEC`) and position-independent (`ET_DYN`) programs, segments that do not
  start on a page boundary, the relocations `RELATIVE`, `64`, `GLOB_DAT` and `JUMP_SLOT`, and a rollback when
  loading fails.
- **Imports**: symbols bound by name through host libraries, an `ImportTable` with stubs, and a stub for
  unimplemented functions that stops the program and names the missing function.
- **Running guest code**: a guest stack with a guard area, calling guest code natively (System V ABI), host
  functions callable from the guest, `exitGuest`, and a crash handler that prints a readable report (reason,
  fault address, registers, guest call).
- **Command line**: `zenith [--log-level <level>] <program.elf>`.
- **Tests**: unit tests per module, small assembly guest programs built by every build and run through the
  CLI, and CLI behavior tests.
- **Documentation**: architecture overview, three ADRs, ELF support, HLE guide and status, developer guides,
  legal policy, contributing guide, security policy.
