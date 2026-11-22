// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/result.hpp"
#include "common/types.hpp"
#include "core/imports.hpp"
#include "cpu/stub_table.hpp"
#include "memory/address_space.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace zenith::core {

// The test bridge: a handful of host services that guest programs reach through fixed addresses.
//
// It exists so small test programs can talk to the host before the real system libraries are emulated.
// It is not a PS5 interface: the real imports will be resolved by name through the HLE.
//
// Every function follows the System V ABI. From assembly:
//     mov $BRIDGE_WRITE, %eax
//     call *%rax
// with BRIDGE_WRITE = bridgeAddress(BridgeFunction::Write) (see tests/homebrew/hello for a complete example).
enum class BridgeFunction : u32 {
    // u64 write(u64 fd, const char* data, u64 length)
    //   Writes to the host's stdout (fd 1) or stderr (fd 2). Returns the byte count, or ~0 on error
    //   (bad fd, memory not mapped, length over maxBridgeWriteSize).
    Write = 0,

    // [[noreturn]] void exit(u64 code)
    //   Ends the program: runElf() returns `code`.
    Exit = 1,

    Count
};

inline constexpr GuestAddr bridgeBase = 0x100000; // 1 MiB: below the usual program addresses
inline constexpr u64 maxBridgeWriteSize = 1 * MiB;

constexpr GuestAddr bridgeAddress(BridgeFunction function) {
    return bridgeBase + static_cast<u64>(function) * cpu::StubTable::stubSize;
}

// Receives what the guest writes: the file descriptor (1 or 2) and the text.
using OutputSink = std::function<void(int fd, std::string_view text)>;

// What the bridge functions need to know about the program being run. Set by runElf() for the duration of
// the run, per thread.
struct RunContext {
    memory::AddressSpace* space = nullptr;
    ImportTable* imports = nullptr;
    OutputSink output;   // empty: write to the host's stdout/stderr
    std::string failure; // set by a host function that ends the program because of an error
};

// The context of the run in progress on this thread, or nullptr.
RunContext* currentRunContext();

class ScopedRunContext {
public:
    explicit ScopedRunContext(RunContext& context);
    ~ScopedRunContext();

    ScopedRunContext(const ScopedRunContext&) = delete;
    ScopedRunContext& operator=(const ScopedRunContext&) = delete;

private:
    RunContext* previous_;
};

// The test library, "libzenithtest.so": the same two services as the bridge, but as named imports.
//   u64 zenith_write(u64 fd, const char* data, u64 length)   // see BridgeFunction::Write
//   [[noreturn]] void zenith_exit(u64 code)                  // see BridgeFunction::Exit
inline constexpr std::string_view testLibraryName = "libzenithtest.so";
void registerTestLibrary(ImportTable& imports);

// Maps the bridge stubs at their fixed addresses. The returned table must stay alive while guest code runs.
[[nodiscard]] Result<cpu::StubTable> installBridge(memory::AddressSpace& space);

} // namespace zenith::core
