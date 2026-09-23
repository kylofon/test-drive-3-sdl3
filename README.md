# Test Drive III: The Passion — SDL3 port

A faithful C reimplementation of *Test Drive III: The Passion* (Accolade, 1990)
on SDL3, following the TestDrive1987 and TestDrive2 ports. Work in progress. See
[PLAN.md](PLAN.md).

The original game files are not included. Put them in `Game/`.

## Layout
- `Game/` original data (not in git)
- `tools/` Python reverse-engineering tools; `tools/ghidra/` Ghidra scripts
- `port/` RE guide, symbols, subsystem specs (`port/spec/`)
- `td3port/` the SDL3 port (later)
- `_tools` junction to the shared Ghidra/JDK install (not in git)

## License
MIT for the code in this repository (see LICENSE). Test Drive III is © Accolade.
