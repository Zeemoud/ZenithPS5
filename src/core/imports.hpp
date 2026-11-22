// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/result.hpp"
#include "common/types.hpp"
#include "cpu/stub_table.hpp"
#include "memory/address_space.hpp"

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace zenith::core {

// The libraries the host provides to guest programs (the HLE), and the stubs that bind program imports to them.
//
// A host library is a name ("libzenithtest.so") plus functions registered under symbol names. When a program
// imports a symbol, resolve() returns the guest address of a stub:
//   - for a function the host implements: a stub that jumps to the host function;
//   - for any other symbol: a stub that, when the program actually calls it, stops the program and reports the
//     name of the missing function. A program can therefore load and run until it needs something the HLE
//     does not have yet, and the report says exactly what is missing.
//
// ELF symbol lookup is global (not per library): a symbol is found whichever library it was registered under.
// The stubs live in guest memory, so this object must not outlive its AddressSpace.
class ImportTable {
public:
    static constexpr GuestAddr defaultBase = 0x200000; // between the test bridge and the usual program base
    static constexpr u64 defaultCapacity = 512;

    [[nodiscard]] static Result<ImportTable> create(memory::AddressSpace& space, GuestAddr base = defaultBase,
                                                    u64 capacity = defaultCapacity);

    // Registers a host function under `symbol`, as part of `library`.
    void addFunction(std::string_view library, std::string symbol, cpu::HostFunction function);

    // Declares a library that exports nothing yet (so programs may link against it).
    void addLibrary(std::string library);

    bool hasLibrary(std::string_view library) const;

    // The guest address to bind `symbol` to. The same symbol always gets the same stub.
    [[nodiscard]] Result<GuestAddr> resolve(std::string_view symbol);

    // Name of the unimplemented function that stub `id` stands for (see the report in resolve()).
    const std::string& unimplementedName(u32 id) const;

    // Names resolved to an "unimplemented" stub so far, in the order they were first requested.
    const std::vector<std::string>& unimplementedNames() const { return unimplemented_; }

private:
    explicit ImportTable(cpu::StubTable stubs) : stubs_(std::move(stubs)) {}

    cpu::StubTable stubs_;
    std::set<std::string, std::less<>> libraries_;
    std::map<std::string, cpu::HostFunction, std::less<>> functions_;
    std::map<std::string, GuestAddr, std::less<>> bound_;
    std::vector<std::string> unimplemented_;
};

} // namespace zenith::core
