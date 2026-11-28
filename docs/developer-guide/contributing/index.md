# Contributing

## Pull requests

- Keep one topic per pull request and state what it changes and why.
- Commit messages start with a short summary line. Changes that are not
  trivial add a paragraph on what changed and its impact.
- Unit tests must pass and new behaviour comes with tests; code coverage must
  not decrease. See [Building](../../user-guide/building.md) for the test and coverage builds.
- Documentation changes follow [Writing docs](writing-docs.md).

## Code conventions

- Allman braces. Always brace a block body, including single-line `if`/`else`
  and `case` bodies.
- `and`, `or` and `not` rather than `&&`, `||` and `!`.
- No ternary operator.
- Include guards named `KICKCAT_<PATH>_H`; no `#pragma once`.
- `while (true)` rather than `for (;;)`.
- Avoid `inline`; use `constexpr` for compile-time helpers defined in headers.
- Comments explain a non-obvious reason; they do not restate the code.

## Reporting hardware issues

The stack is tested against the slaves the maintainers own. When a device
misbehaves, open an issue with the KickCAT traces and a network capture of the
failing exchange. Traces are enabled per level with the `DEBUG_*` CMake options
in [`cmake/debug.cmake`](https://github.com/leducp/KickCAT/blob/master/cmake/debug.cmake).
