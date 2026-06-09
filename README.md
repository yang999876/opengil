# openGil

openGil is a standalone C++ CLI and agent skill for inspecting and safely editing
verified `.gil` save structures.

The project is intentionally separate from `genshin-ts`. Existing JavaScript
research tools remain useful as oracle implementations while openGil grows into
the agent-facing tool.

## Build

```powershell
cmake -S . -B build
cmake --build build --config Release
```

The CLI target is `opengil`.

## Quick Start

```powershell
.\build\Release\opengil.exe --version
.\build\Release\opengil.exe inspect --input I:\Wanderland\genshin-ts\backups\test1.gil
.\build\Release\opengil.exe list-prefabs --input I:\Wanderland\genshin-ts\backups\test1.gil
```

All machine-facing command output is JSON on stdout. Logs and diagnostics go to
stderr.

