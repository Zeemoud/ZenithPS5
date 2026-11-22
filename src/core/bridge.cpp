// SPDX-License-Identifier: GPL-2.0-only
#include "core/bridge.hpp"

#include "common/assert.hpp"
#include "cpu/guest_call.hpp"

#include <cstdio>

namespace zenith::core {
namespace {

thread_local RunContext* currentContext = nullptr;

constexpr u64 errorValue = ~0ULL;

u64 hostWrite(u64 fd, u64 pointer, u64 length, u64, u64, u64) noexcept {
    const RunContext* context = currentContext;
    if (context == nullptr || (fd != 1 && fd != 2) || length > maxBridgeWriteSize) {
        return errorValue;
    }
    if (length == 0) {
        return 0;
    }
    if (!context->space->isMapped(pointer, length)) {
        return errorValue;
    }

    const std::string_view text(reinterpret_cast<const char*>(memory::AddressSpace::hostPointer(pointer)), length);
    if (context->output) {
        context->output(static_cast<int>(fd), text);
    } else {
        std::FILE* stream = fd == 1 ? stdout : stderr;
        std::fwrite(text.data(), 1, text.size(), stream);
        std::fflush(stream);
    }
    return length;
}

u64 hostExit(u64 code, u64, u64, u64, u64, u64) noexcept {
    cpu::exitGuest(code);
}

} // namespace

RunContext* currentRunContext() {
    return currentContext;
}

void registerTestLibrary(ImportTable& imports) {
    imports.addFunction(testLibraryName, "zenith_write", hostWrite);
    imports.addFunction(testLibraryName, "zenith_exit", hostExit);
}

ScopedRunContext::ScopedRunContext(RunContext& context) : previous_(currentContext) {
    currentContext = &context;
}

ScopedRunContext::~ScopedRunContext() {
    currentContext = previous_;
}

Result<cpu::StubTable> installBridge(memory::AddressSpace& space) {
    auto table = cpu::StubTable::create(space, bridgeBase, static_cast<u64>(BridgeFunction::Count));
    if (!table.ok()) {
        return makeError("cannot install the bridge: {}", table.error());
    }

    // The order of these calls fixes the addresses, which are part of the bridge's contract.
    const auto write = table.value().add(hostWrite);
    const auto exit = table.value().add(hostExit);
    if (!write.ok() || !exit.ok()) {
        return makeError("cannot install the bridge stubs");
    }
    ZENITH_VERIFY(write.value() == bridgeAddress(BridgeFunction::Write));
    ZENITH_VERIFY(exit.value() == bridgeAddress(BridgeFunction::Exit));

    return table;
}

} // namespace zenith::core
