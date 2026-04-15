# Contributing

Contributions welcome!

- Open an issue before starting large features so we can agree on design.
- Keep each PR focused; prefer two small PRs over one sprawling one.
- Run `ctest --output-on-failure` before submitting.
- Add a test next to any parser / serializer change.

## Code style

- C11, tabs off, 4-space indent, braces on next line for functions, K&R for statements.
- Keep platform-specific code inside `src/platform/`.
- No dynamic allocation in hot paths where a 4 KiB stack buffer would do.
- Headers are named after their subsystem; one `.h` per `.c`.

## Reverse-engineering contributions

If you discover new byte-level details of `SD_Firmware_Tool.exe` (or any RK upgrade tool), please attach your notes in `docs/` with a clear source. Don't paste proprietary source.
