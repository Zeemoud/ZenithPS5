// SPDX-License-Identifier: GPL-2.0-only
#include "core/run_elf.hpp"

#include "common/log.hpp"
#include "cpu/guest_call.hpp"
#include "cpu/guest_stack.hpp"
#include "loader/elf/elf_file.hpp"
#include "loader/elf/elf_loader.hpp"
#include "memory/address_space.hpp"

namespace zenith::core {
namespace {

// Where the main guest stack goes. Far from the host's own mappings (AddressSanitizer, for one, reserves
// 0x5000'0000'0000 - 0x5400'0000'0000 for its allocator); a collision is reported as an error.
constexpr GuestAddr mainStackBase = 0x3000'0000'0000ULL;

} // namespace

Result<u64> runElf(std::span<const u8> data, std::string_view name, const RunOptions& options) {
    const auto elf = loader::ElfFile::parse(data);
    if (!elf.ok()) {
        return makeError("{}: {}", name, elf.error());
    }

    memory::AddressSpace space; // declared first: destroyed last, after everything mapped in it

    // The bridge goes first, so a program that wants its addresses is refused by the loader.
    const auto bridge = installBridge(space);
    if (!bridge.ok()) {
        return makeError("{}: {}", name, bridge.error());
    }

    const auto image = loader::loadElf(elf.value(), data, space, name, {.baseAddress = options.imageBase});
    if (!image.ok()) {
        return makeError("{}", image.error());
    }

    const auto stack = cpu::GuestStack::create(space, mainStackBase, options.stackSize, "main stack");
    if (!stack.ok()) {
        return makeError("{}: {}", name, stack.error());
    }

    RunContext context;
    context.space = &space;
    context.output = options.output;
    const ScopedRunContext scope(context);

    ZENITH_LOG_INFO(Core, "running '{}' from {:#x}", name, image.value().entry);
    return cpu::callGuest(image.value().entry, stack.value());
}

} // namespace zenith::core
