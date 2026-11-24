# Architecture Decision Records

An ADR records one significant decision: the context, what was decided, and what it costs. Write one when a
choice would be hard to guess from the code, or expensive to reverse. Number them in order and never rewrite
history: to change a decision, add a new ADR that supersedes the old one.

| ADR | Decision |
|---|---|
| [0001](0001-hle-instead-of-static-recompilation.md) | HLE at runtime instead of static recompilation |
| [0002](0002-identity-mapped-guest-memory.md) | Guest addresses are host addresses |
| [0003](0003-imports-bound-through-stubs.md) | Imports are bound by name through stubs, and missing functions are named |
| [0004](0004-programs-and-libraries-loaded-together.md) | A program and its libraries are mapped first, then relocated against one global scope |

## Template

```markdown
# NNNN. Title

Status: accepted | superseded by NNNN

## Context
What forces are at play?

## Decision
What did we decide?

## Consequences
What gets easier, what gets harder?
```
