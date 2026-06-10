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
.\build\Release\opengil.exe validate --input .\tests\fixtures\test1.gil
.\build\Release\opengil.exe list-prefabs --input .\tests\fixtures\test1.gil
.\build\Release\opengil.exe get-model --input .\tests\fixtures\test1.gil --prefab-id 1086324737
.\build\Release\opengil.exe set-model --input .\tests\fixtures\test1.gil --prefab-id 1086324737 --asset-id 20001220 --dry-run
.\build\Release\opengil.exe batch --input .\tests\fixtures\test1.gil --ops .\tests\fixtures\batch-model-rename.json --dry-run
```

All machine-facing command output is JSON on stdout. Logs and diagnostics go to
stderr.

`validate` performs structural validation only: it checks the `.gil` envelope
sizes and protobuf wire parsing. It does not prove semantic consistency such as
unique ids, mirrored scene/preview records, tab mappings, or dangling references.

See `skills/gil-editing/references/cli-usage.md` for the full command reference.

For project architecture and feature development notes, see:

- `docs/development.zh.md` Chinese, primary guide
- `docs/development.md` English, shorter reference
