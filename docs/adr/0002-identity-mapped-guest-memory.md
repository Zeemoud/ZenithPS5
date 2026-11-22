# 0002. Guest addresses are host addresses

Status: accepted

## Context

Guest code runs natively and dereferences real pointers. Two designs are possible: translate every guest
address to a host address (impossible without instrumenting the code), or make the guest's addresses valid
host addresses.

## Decision

Guest address `X` is host address `X` (identity mapping). The `AddressSpace` maps each region at exactly the
requested address with `mmap(MAP_FIXED_NOREPLACE)` semantics, and tracks regions in its own bookkeeping.

## Consequences

- Guest code needs no instrumentation: a pointer in the guest is a pointer on the host.
- A mapping fails if the host already uses any part of the range (executable, libraries, stacks, a sanitizer's
  reserved regions). It never replaces an existing mapping. This showed up for real: the AddressSanitizer of GCC 13
  reserves `0x5000'0000'0000`-`0x5400'0000'0000` for its allocator, so the main guest stack lives elsewhere.
- Addresses must be chosen with the host's layout in mind: the usual program base and the stubs are low
  addresses, the stack and test addresses are far above everything the host maps.
- Mappings use whole guest pages (16 KiB). Two segments cannot share a page because a page has one protection.
- A fault in guest code is a fault in the host process. The crash handler turns it into a readable report.
