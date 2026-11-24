// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/result.hpp"
#include "common/types.hpp"
#include "core/imports.hpp"
#include "core/library_source.hpp"
#include "loader/elf/elf_loader.hpp"
#include "memory/address_space.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace zenith::core {

struct ProgramOptions {
    GuestAddr imageBase = 0x400000; // where the program (if position-independent) is loaded
    LibrarySource libraries;        // finds the libraries the host does not provide; empty: there are none
};

// A program and the shared libraries it needs, all mapped, relocated and ready to run.
struct LoadedProgram {
    std::vector<loader::Module> modules; // [0] is the program, then the libraries in the order they were loaded

    GuestAddr entry() const { return modules.front().image.entry; }
};

// Loads a program and, recursively, every library it needs:
//   - a library the host provides (the ImportTable knows its name) needs no file;
//   - any other is looked up through `options.libraries`, parsed, and mapped after the modules already loaded.
//     Each library is loaded once, however many modules need it.
//
// Then every module is relocated against one global scope, as ELF does: a symbol is found in the first module
// (in load order) that exports it, else in the host libraries, else it becomes a stub that stops the program with
// a report if it is called (see ImportTable). So modules can call each other, and a library can use the host.
//
// Not supported yet: the initialization functions of libraries (DT_INIT, DT_INIT_ARRAY) are not run.
//
// Either everything is loaded or nothing is: on failure, every module mapped so far is unmapped.
[[nodiscard]] Result<LoadedProgram> loadProgram(const loader::ElfFile& elf, std::span<const u8> data,
                                                memory::AddressSpace& space, ImportTable& imports,
                                                std::string_view name, const ProgramOptions& options = {});

} // namespace zenith::core
