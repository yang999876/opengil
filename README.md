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
.\build\Release\opengil.exe set-model --input I:\Wanderland\genshin-ts\backups\test1.gil --output out.gil --prefab-id 1086324737 --asset-id 20001220
.\build\Release\opengil.exe rename-prefab --input I:\Wanderland\genshin-ts\backups\test1.gil --output renamed.gil --prefab-id 1086324737 --name "Renamed Prefab"
.\build\Release\opengil.exe clone-prefab --input I:\Wanderland\genshin-ts\backups\1073741843.gil --output cloned.gil --source-prefab-id 1077936385 --tab-id 6 --new-name "openGil Clone Test"
.\build\Release\opengil.exe copy-prefab-to-tab --input I:\Wanderland\genshin-ts\backups\1073741843.gil --output copied.gil --source-prefab-id 1077936385 --tab-id 6
.\build\Release\opengil.exe batch --input I:\Wanderland\genshin-ts\backups\test1.gil --output batched.gil --ops tests\fixtures\batch-model-rename.json
.\build\Release\opengil.exe attach-nodegraph --input I:\Wanderland\genshin-ts\backups\1073741843.gil --output attached.gil --prefab-id 1086324737 --nodegraph-id 1073741913
.\build\Release\opengil.exe set-projectile-motion --input I:\Wanderland\genshin-ts\backups\1073741843.gil --output projectile.gil --prefab-id 1077936385 --angle 80 --speed 20 --gravity 20
.\build\Release\opengil.exe create-scene-object --input I:\Wanderland\genshin-ts\backups\1073741843.gil --output scene-object.gil --asset-id 20001220 --object-id 1077938001 --pos-x 1 --pos-y 2 --pos-z 3 --rot-x 4 --rot-y 5 --rot-z 6 --scale-x 1.5 --scale-y 1.5 --scale-z 1.5
.\build\Release\opengil.exe create-prefab --input I:\Wanderland\genshin-ts\backups\1073741843.gil --output prefab.gil --asset-id 20001220 --prefab-id 1077938002 --pos-x 7 --pos-y 8 --pos-z 9 --rot-x 10 --rot-y 11 --rot-z 12 --scale-x 2 --scale-y 2 --scale-z 2
.\build\Release\opengil.exe create-scene-prefab-instance --input prefab.gil --output prefab-instance.gil --prefab-id 1077938002 --asset-id 20001220 --object-id 1077938003 --pos-x 13 --pos-y 14 --pos-z 15 --rot-x 16 --rot-y 17 --rot-z 18 --scale-x 3 --scale-y 3 --scale-z 3
.\build\Release\opengil.exe set-scene-transform --input I:\Wanderland\genshin-ts\backups\1073741843.gil --output scene-transform.gil --object-id 1086324737 --pos-x 7 --pos-y 8 --pos-z 9 --rot-x 10 --rot-y 11 --rot-z 12 --scale-x 2 --scale-y 2 --scale-z 2
.\build\Release\opengil.exe set-preview-transform --input I:\Wanderland\genshin-ts\backups\1073741843.gil --output preview-transform.gil --object-id 1077936362 --pos-x 1 --pos-y 2 --pos-z 3 --rot-x 4 --rot-y 5 --rot-z 6 --scale-x 1.5 --scale-y 1.5 --scale-z 1.5
.\build\Release\opengil.exe custom-vars list --input I:\Wanderland\genshin-ts\backups\test1.gil --prefab-id 1086324737
.\build\Release\opengil.exe custom-vars add --input I:\Wanderland\genshin-ts\backups\test1.gil --output custom.gil --prefab-id 1086324737 --name openGilVar --type str
.\build\Release\opengil.exe custom-vars sync-tab --input I:\Wanderland\genshin-ts\backups\1073741843.gil --output synced.gil --source-prefab-id 1077936340 --tab-id 6
```

All machine-facing command output is JSON on stdout. Logs and diagnostics go to
stderr.
