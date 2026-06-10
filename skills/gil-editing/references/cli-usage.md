# CLI Usage

Use `opengil` with JSON stdout.

Read-only commands:

```powershell
opengil inspect --input test.gil
opengil validate --input test.gil
opengil diff-summary --before before.gil --after after.gil
opengil list-tabs --input test.gil
opengil list-prefabs --input test.gil
opengil list-prefabs --input test.gil --tab TabName
opengil list-prefab-tabs --input test.gil --prefab-id 1077936130
opengil list-nodegraphs --input test.gil
opengil get-model --input test.gil --prefab-id 1077936130
```

Implemented write commands:

```powershell
opengil set-model --input input.gil --output output.gil --prefab-id 1077936130 --asset-id 20001220
opengil set-empty-model --input input.gil --output output.gil --prefab-id 1077936130
opengil rename-prefab --input input.gil --output output.gil --prefab-id 1077936130 --name "Renamed Prefab"
opengil delete-prefab --input input.gil --output output.gil --prefab-id 1077936130
opengil clone-prefab --input input.gil --output output.gil --source-prefab-id 1077936385 --tab-id 6 --new-name "Cloned Prefab"
opengil copy-prefab-to-tab --input input.gil --output output.gil --source-prefab-id 1077936385 --tab-id 6
opengil copy-prefab-to-tab --input input.gil --output output.gil --source-prefab-id 1077936385 --tab-id 6 --name "Copied Prefab"
opengil attach-nodegraph --input input.gil --output output.gil --prefab-id 1077936130 --nodegraph-id 1073741913
opengil attach-all-nodegraphs --input input.gil --output output.gil --prefab-id 1077936130
opengil set-projectile-motion --input input.gil --output output.gil --prefab-id 1077936385 --angle 80 --speed 20 --gravity 20
opengil set-projectile-motion --input input.gil --output output.gil --prefab-id 1077936385 --x 3.47 --y 19.70 --gravity 20
opengil create-scene-object --input input.gil --output output.gil --asset-id 20001220 --object-id 1077938001 --pos-x 1 --pos-y 2 --pos-z 3 --rot-x 4 --rot-y 5 --rot-z 6 --scale-x 1.5 --scale-y 1.5 --scale-z 1.5
opengil create-prefab --input input.gil --output output.gil --asset-id 20001220 --prefab-id 1077938002 --pos-x 7 --pos-y 8 --pos-z 9 --rot-x 10 --rot-y 11 --rot-z 12 --scale-x 2 --scale-y 2 --scale-z 2
opengil create-prefab --input input.gil --output output.gil --asset-id 20001220 --prefab-id 1077938002 --template template.gil
opengil create-scene-prefab-instance --input input.gil --output output.gil --prefab-id 1077938002 --asset-id 20001220 --object-id 1077938003 --pos-x 13 --pos-y 14 --pos-z 15 --rot-x 16 --rot-y 17 --rot-z 18 --scale-x 3 --scale-y 3 --scale-z 3
opengil decoration add --input input.gil --output output.gil --prefab-id 1077936385 --asset-id 20001220 --name Deco --pos-x 0 --pos-y 1.9 --pos-z 0 --scale-x 0.3 --scale-y 0.04 --scale-z 0.3
opengil attachment add --input input.gil --output output.gil --prefab-id 1077936385 --name Hand --display-name "Hand Point" --pos-x 0.48 --pos-y 1.52 --rot-x -37.9 --rot-y 81.9
opengil set-scene-transform --input input.gil --output output.gil --object-id 1086324737 --pos-x 7 --pos-y 8 --pos-z 9 --rot-x 10 --rot-y 11 --rot-z 12 --scale-x 2 --scale-y 2 --scale-z 2
opengil set-preview-transform --input input.gil --output output.gil --object-id 1077936362 --pos-x 1 --pos-y 2 --pos-z 3 --rot-x 4 --rot-y 5 --rot-z 6 --scale-x 1.5 --scale-y 1.5 --scale-z 1.5
opengil custom-vars list --input input.gil --prefab-id 1077936130
opengil custom-vars add --input input.gil --output output.gil --prefab-id 1077936130 --name openGilVar --type str
opengil custom-vars remove --input input.gil --output output.gil --prefab-id 1077936130 --name openGilVar
opengil custom-vars copy-all --input input.gil --output output.gil --from-prefab-id 1077936130 --to-prefab-id 1077936131
opengil custom-vars sync-tab --input input.gil --output output.gil --source-prefab-id 1077936340 --tab-id 6
opengil ui list --input input.gil
opengil ui list --input input.gil --controller-entry-id 1073741855
opengil ui append --input input.gil --output output.gil --template template.gil --target-controller-entry-id 1073741855 --template-primitive-index 0
opengil ui append-many --input input.gil --output output.gil --template template.gil --target-controller-entry-id 1073741855 --template-primitive-index 0 --count 3
opengil ui retain --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-indexes 0,2,3
opengil ui set-type --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --type-id 100001
opengil ui set-color --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --color -65536
opengil ui set-transform --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --pos-x 10 --pos-y 20 --width 80 --height 80 --scale-x 1 --scale-y 1 --scale-z 1 --rot-z 0
opengil ui set-layer --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --layer 9
opengil ui set-name --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --name ButtonA
opengil ui copy-transform-from-template --input input.gil --output output.gil --template template.gil --controller-entry-id 1073741855 --primitive-index 0 --template-primitive-index 1
opengil set-model --input input.gil --prefab-id 1077936130 --asset-id 20001220 --dry-run
opengil rename-prefab --input input.gil --prefab-id 1077936130 --name "Renamed Prefab" --dry-run
opengil delete-prefab --input input.gil --prefab-id 1077936130 --dry-run
opengil clone-prefab --input input.gil --source-prefab-id 1077936385 --tab-id 6 --new-name "Cloned Prefab" --dry-run
opengil copy-prefab-to-tab --input input.gil --source-prefab-id 1077936385 --tab-id 6 --dry-run
opengil attach-nodegraph --input input.gil --prefab-id 1077936130 --nodegraph-id 1073741913 --dry-run
opengil set-projectile-motion --input input.gil --prefab-id 1077936385 --angle 80 --speed 20 --dry-run
opengil create-prefab --input input.gil --asset-id 20001220 --prefab-id 1077938002 --dry-run
opengil create-scene-prefab-instance --input input.gil --prefab-id 1077938002 --asset-id 20001220 --object-id 1077938003 --dry-run
opengil decoration add --input input.gil --prefab-id 1077936385 --asset-id 20001220 --name Deco --dry-run
opengil attachment add --input input.gil --prefab-id 1077936385 --name Hand --display-name "Hand Point" --dry-run
opengil set-scene-transform --input input.gil --object-id 1086324737 --pos-x 7 --pos-y 8 --pos-z 9 --dry-run
opengil set-preview-transform --input input.gil --object-id 1077936362 --pos-x 1 --pos-y 2 --pos-z 3 --dry-run
opengil custom-vars sync-tab --input input.gil --source-prefab-id 1077936340 --tab-id 6 --dry-run
opengil ui append-many --input input.gil --template template.gil --target-controller-entry-id 1073741855 --count 3 --dry-run
opengil ui retain --input input.gil --controller-entry-id 1073741855 --primitive-indexes 0,2 --dry-run
opengil batch --input input.gil --output output.gil --ops ops.json
opengil batch --input input.gil --ops ops.json --dry-run
```

Custom variable writes edit definitions only, not runtime values. Valid `--type`
values are `entity`, `int`, `bool`, `float`, `str`/`string`, and `vec`/`vec3`.
Use `--tab-id` instead of `--tab` for non-ASCII tab names on Windows shells.

`clone-prefab` clones a prefab definition into `top4`, appends a `100 -> prefabId`
mapping to the target child tab in `top6`, appends the prefab to the unclassified
prefab mapping, offsets the preview position, and clones prefab-side `top27`
decoration records when the source has them. Prefer `--tab-id` over `--tab`.
`copy-prefab-to-tab` uses the same core behavior, but `--name` is optional and
defaults to the source prefab name plus `-copy`.

`delete-prefab` removes the prefab entry from `top4`, strips `top6` mappings,
removes prefab-owned `top27.field1` decoration records, and prunes `top10`
records that directly reference the removed prefab or decoration ids. It does
not default to deleting `top8`.

Transform writes replace the full transform message at `5.1.6.11` or `8.1.6.11`.
Unspecified position/rotation values default to `0`; unspecified scale values
default to `1`. Pass all nine values when preserving existing axes matters.

Object creation commands copy a reusable template entry, patch ids/assets, replace
the full transform, append to `top4` or `top5`, and append `top6` mappings. Pass
`--template template.gil` for prefab or scene-prefab-instance creation when the
target asset needs a better observed template than the current file contains.

`decoration add` appends prefab-side `top27.field1` decoration records, appends
scene mirror `top27.field2` records for matching preview/scene entries in `top8`,
and updates decoration reference lists in `top4` and `top8`. Pass a stable
`--name`; the CLI does not invent editor display names for you.

`attachment add` writes custom attachment point definitions and indexes into
prefab `top4` and matching `top8` scene entries. Use `--object-id` to target one
scene entry; omit it to update all scene entries that reference the prefab.

UI primitive commands edit `top9` entries and controller child id lists. Start
with `ui list`; when the listed primitives use a controller entry other than the
default, pass that id through `--controller-entry-id` or
`--target-controller-entry-id`. `copy-transform-from-template` replaces the
target primitive body with the template primitive while preserving the target
entry id and controller id.

Batch `ops.json` may be either an array or an object with an `ops` array:

```json
{
  "ops": [
    {
      "op": "set-model",
      "prefabId": 1077936130,
      "assetId": 20001220
    },
    {
      "op": "rename-prefab",
      "prefabId": 1077936130,
      "name": "Renamed Prefab"
    },
    {
      "op": "delete-prefab",
      "prefabId": 1077936130
    },
    {
      "op": "attach-nodegraph",
      "prefabId": 1077936130,
      "nodegraphId": 1073741913
    },
    {
      "op": "set-projectile-motion",
      "prefabId": 1077936385,
      "angleDeg": 80,
      "speed": 20,
      "gravity": 20
    },
    {
      "op": "set-empty-model",
      "prefabId": 1077936130
    },
    {
      "op": "set-scene-transform",
      "objectId": 1086324737,
      "posX": 7,
      "posY": 8,
      "posZ": 9,
      "rotX": 10,
      "rotY": 11,
      "rotZ": 12,
      "scaleX": 2,
      "scaleY": 2,
      "scaleZ": 2
    },
    {
      "op": "create-prefab",
      "assetId": 20001220,
      "prefabId": 1077938002,
      "posX": 7,
      "posY": 8,
      "posZ": 9,
      "rotX": 10,
      "rotY": 11,
      "rotZ": 12,
      "scaleX": 2,
      "scaleY": 2,
      "scaleZ": 2
    },
    {
      "op": "create-scene-prefab-instance",
      "prefabId": 1077938002,
      "assetId": 20001220,
      "objectId": 1077938003
    },
    {
      "op": "decoration.add",
      "prefabId": 1077936385,
      "assetId": 20001220,
      "name": "Deco",
      "posY": 1.9,
      "scaleX": 0.3,
      "scaleY": 0.04,
      "scaleZ": 0.3
    },
    {
      "op": "attachment.add",
      "prefabId": 1077936385,
      "name": "Hand",
      "displayName": "Hand Point",
      "posX": 0.48,
      "posY": 1.52,
      "rotX": -37.9,
      "rotY": 81.9
    },
    {
      "op": "clone-prefab",
      "sourcePrefabId": 1077936385,
      "tabId": 6,
      "newName": "Cloned Prefab"
    },
    {
      "op": "copy-prefab-to-tab",
      "sourcePrefabId": 1077936385,
      "tabId": 6
    },
    {
      "op": "custom-vars.add",
      "prefabId": 1077936130,
      "name": "openGilVar",
      "type": "str"
    },
    {
      "op": "custom-vars.copy-all",
      "sourcePrefabId": 1077936130,
      "targetPrefabId": 1077936131
    },
    {
      "op": "custom-vars.sync-tab",
      "sourcePrefabId": 1077936340,
      "tabId": 6
    }
  ]
}
```

Other planned write commands return `NOT_IMPLEMENTED` until their milestone lands.
