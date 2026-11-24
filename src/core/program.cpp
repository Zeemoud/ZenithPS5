// SPDX-License-Identifier: GPL-2.0-only
#include "core/program.hpp"

#include "common/log.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <unordered_map>

namespace zenith::core {
namespace {

// Libraries are placed one after the other, each starting on a 64 KiB boundary.
constexpr u64 libraryAlignment = 64 * KiB;

GuestAddr firstFreeAddressAfter(const loader::Module& module) {
    GuestAddr end = module.image.base;
    for (const loader::LoadedSegment& segment : module.image.segments) {
        end = std::max(end, segment.start + segment.size);
    }
    return (end + libraryAlignment - 1) / libraryAlignment * libraryAlignment;
}

bool isLoaded(const std::vector<loader::Module>& modules, const std::string& library) {
    return std::any_of(modules.begin(), modules.end(),
                       [&library](const loader::Module& module) { return module.name == library; });
}

} // namespace

Result<LoadedProgram> loadProgram(const loader::ElfFile& elf, std::span<const u8> data, memory::AddressSpace& space,
                                  ImportTable& imports, std::string_view name, const ProgramOptions& options) {
    LoadedProgram program;

    auto fail = [&program, &space](const std::string& message) -> Result<LoadedProgram> {
        for (loader::Module& module : program.modules) {
            loader::unmapModule(module, space);
        }
        program.modules.clear();
        return makeError("{}", message);
    };

    auto mainModule = loader::mapModule(elf, data, space, name, options.imageBase);
    if (!mainModule.ok()) {
        return makeError("{}", mainModule.error());
    }
    program.modules.push_back(std::move(mainModule).value());
    if (const auto entryCheck = loader::checkEntryPoint(program.modules.front()); !entryCheck.ok()) {
        return fail(entryCheck.error());
    }
    GuestAddr nextBase = firstFreeAddressAfter(program.modules.back());

    // Map the libraries, breadth first. Index, not reference: the vector grows while we walk it.
    for (usize index = 0; index < program.modules.size(); ++index) {
        const auto needed = loader::neededLibraries(program.modules[index]);
        if (!needed.ok()) {
            return fail(needed.error());
        }

        for (const std::string& library : needed.value()) {
            const std::string requester = program.modules[index].name;
            program.modules[index].image.libraries.push_back(library);

            if (imports.hasLibrary(library) || isLoaded(program.modules, library)) {
                continue;
            }
            if (!options.libraries) {
                return fail(
                    std::format("'{}': the program needs the library '{}', which is not provided", requester, library));
            }

            const auto bytes = options.libraries(library);
            if (!bytes.ok()) {
                return fail(std::format("'{}' needs the library '{}': {}", requester, library, bytes.error()));
            }
            const auto parsed = loader::ElfFile::parse(bytes.value());
            if (!parsed.ok()) {
                return fail(std::format("library '{}': {}", library, parsed.error()));
            }
            if (parsed.value().type() != loader::elf::typeSharedObject) {
                return fail(std::format("library '{}' is not a shared object", library));
            }

            auto mapped = loader::mapModule(parsed.value(), bytes.value(), space, library, nextBase);
            if (!mapped.ok()) {
                return fail(mapped.error());
            }
            program.modules.push_back(std::move(mapped).value());
            nextBase = firstFreeAddressAfter(program.modules.back());
        }
    }

    // One global scope, in load order: the first module that exports a symbol wins.
    std::unordered_map<std::string, GuestAddr> scope;
    for (usize index = 0; index < program.modules.size(); ++index) {
        const auto exports = loader::exportedSymbols(program.modules[index]);
        if (!exports.ok()) {
            if (index == 0) {
                // The program itself rarely exports anything that matters: do not refuse it for this.
                ZENITH_LOG_DEBUG(Loader, "'{}' exports nothing usable: {}", name, exports.error());
                continue;
            }
            return fail(exports.error());
        }
        for (const loader::ExportedSymbol& symbol : exports.value()) {
            scope.emplace(symbol.name, symbol.address);
        }
    }

    const loader::SymbolResolver resolver = [&scope, &imports](std::string_view symbol) -> Result<GuestAddr> {
        if (const auto found = scope.find(std::string(symbol)); found != scope.end()) {
            return found->second;
        }
        return imports.resolve(symbol);
    };

    for (loader::Module& module : program.modules) {
        const auto relocated = loader::relocateModule(module, resolver);
        if (!relocated.ok()) {
            return fail(relocated.error());
        }
    }
    for (loader::Module& module : program.modules) {
        const auto protectedModule = loader::protectModule(module, space);
        if (!protectedModule.ok()) {
            return fail(protectedModule.error());
        }
    }
    return program;
}

} // namespace zenith::core
