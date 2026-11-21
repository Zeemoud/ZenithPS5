// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/types.hpp"

namespace zenith::cpu {

// Describes the guest call in progress. It lives on the stack of callGuest() and is read by the crash
// handler, which is why it is plain data.
struct GuestCallInfo {
    GuestAddr entry = 0;
    GuestAddr stackBase = 0; // lowest address of the stack mapping: start of the guard area
    GuestAddr guardEnd = 0;  // end of the guard area
    GuestAddr stackTop = 0;  // initial stack pointer
};

namespace detail {

// Sets the guest call in progress on this thread (nullptr for none) and returns the previous one.
const GuestCallInfo* exchangeCurrentGuestCall(const GuestCallInfo* call);

} // namespace detail

// Installs handlers for SIGSEGV, SIGBUS, SIGILL and SIGFPE that print a crash report on stderr (reason,
// fault address, registers, and the guest call in progress), then let the process die from the original
// signal, so exit codes and core dumps keep working.
//
// The report is written with async-signal-safe calls only. The handlers run on an alternate stack, so a
// guest stack overflow is reported instead of killing the process silently.
//
// Call it once, early, from the main thread: the alternate stack is set up for the calling thread only.
// Library code never installs it; the frontend decides.
void installCrashHandler();

} // namespace zenith::cpu
