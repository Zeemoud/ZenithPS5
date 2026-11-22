// SPDX-License-Identifier: GPL-2.0-only
#include "core/imports.hpp"

#include "common/log.hpp"
#include "core/bridge.hpp"
#include "cpu/guest_call.hpp"

namespace zenith::core {
namespace {

// Called, through an id stub, when a program calls a function the host does not implement.
// `id` replaced the guest's first argument. Ends the program: runElf() reports the failure.
u64 hostUnimplemented(u64 id, u64, u64, u64, u64, u64) noexcept {
    RunContext* context = currentRunContext();
    if (context != nullptr && context->imports != nullptr && id < context->imports->unimplementedNames().size()) {
        context->failure = "the program called the unimplemented function '" +
                           context->imports->unimplementedName(static_cast<u32>(id)) + "'";
        ZENITH_LOG_ERROR(Core, "{}", context->failure);
    }
    cpu::exitGuest(~0ULL);
}

} // namespace

Result<ImportTable> ImportTable::create(memory::AddressSpace& space, GuestAddr base, u64 capacity) {
    auto stubs = cpu::StubTable::create(space, base, capacity, cpu::StubTable::idStubSize);
    if (!stubs.ok()) {
        return makeError("cannot create the import table: {}", stubs.error());
    }
    return ImportTable(std::move(stubs).value());
}

void ImportTable::addFunction(std::string_view library, std::string symbol, cpu::HostFunction function) {
    libraries_.emplace(library);
    functions_.insert_or_assign(std::move(symbol), function);
}

void ImportTable::addLibrary(std::string library) {
    libraries_.insert(std::move(library));
}

bool ImportTable::hasLibrary(std::string_view library) const {
    return libraries_.find(library) != libraries_.end();
}

Result<GuestAddr> ImportTable::resolve(std::string_view symbol) {
    if (const auto known = bound_.find(symbol); known != bound_.end()) {
        return known->second;
    }

    Result<GuestAddr> stub = makeError("unreachable");
    if (const auto function = functions_.find(symbol); function != functions_.end()) {
        stub = stubs_.add(function->second);
    } else {
        const u32 id = static_cast<u32>(unimplemented_.size());
        stub = stubs_.addWithId(hostUnimplemented, id);
        if (stub.ok()) {
            unimplemented_.emplace_back(symbol);
            ZENITH_LOG_WARN(Core, "import '{}' is not implemented: the program stops if it calls it", symbol);
        }
    }
    if (!stub.ok()) {
        return makeError("cannot bind the import '{}': {}", symbol, stub.error());
    }

    bound_.emplace(std::string(symbol), stub.value());
    return stub.value();
}

const std::string& ImportTable::unimplementedName(u32 id) const {
    return unimplemented_.at(id);
}

} // namespace zenith::core
