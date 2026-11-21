// SPDX-License-Identifier: GPL-2.0-only
#include "platform/virtual_memory.hpp"

#include "common/log.hpp"

#include <cerrno>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000 // Linux 4.17+; older kernels treat it as a hint, which we check below
#endif

namespace zenith::platform {
namespace {

int toNative(Protection protection) {
    int native = PROT_NONE;
    if (hasFlag(protection, Protection::Read)) {
        native |= PROT_READ;
    }
    if (hasFlag(protection, Protection::Write)) {
        native |= PROT_WRITE;
    }
    if (hasFlag(protection, Protection::Execute)) {
        native |= PROT_EXEC;
    }
    return native;
}

} // namespace

usize pageSize() {
    static const usize size = static_cast<usize>(sysconf(_SC_PAGESIZE));
    return size;
}

bool reserveFixed(void* address, usize size) {
    void* result =
        mmap(address, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (result == MAP_FAILED) {
        ZENITH_LOG_DEBUG(Memory, "reserveFixed({}, {:#x}) failed: {}", address, size, std::strerror(errno));
        return false;
    }
    if (result != address) {
        // Kernel without MAP_FIXED_NOREPLACE: the address was only a hint and was not honored.
        munmap(result, size);
        ZENITH_LOG_DEBUG(Memory, "reserveFixed({}, {:#x}) failed: range already in use", address, size);
        return false;
    }
    return true;
}

bool protect(void* address, usize size, Protection protection) {
    if (mprotect(address, size, toNative(protection)) != 0) {
        ZENITH_LOG_DEBUG(Memory, "protect({}, {:#x}) failed: {}", address, size, std::strerror(errno));
        return false;
    }
    return true;
}

bool decommit(void* address, usize size) {
    // MADV_DONTNEED on private anonymous memory drops the pages: they read as zero afterwards.
    if (madvise(address, size, MADV_DONTNEED) != 0) {
        ZENITH_LOG_DEBUG(Memory, "decommit({}, {:#x}) failed: {}", address, size, std::strerror(errno));
        return false;
    }
    return protect(address, size, Protection::None);
}

bool release(void* address, usize size) {
    if (munmap(address, size) != 0) {
        ZENITH_LOG_DEBUG(Memory, "release({}, {:#x}) failed: {}", address, size, std::strerror(errno));
        return false;
    }
    return true;
}

} // namespace zenith::platform
