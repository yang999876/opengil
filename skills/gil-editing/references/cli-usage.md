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
opengil attach-nodegraph --input input.gil --output output.gil --prefab-id 1077936130 --nodegraph-id 1073741913
opengil attach-all-nodegraphs --input input.gil --output output.gil --prefab-id 1077936130
opengil set-projectile-motion --input input.gil --output output.gil --prefab-id 1077936385 --angle 80 --speed 20 --gravity 20
opengil set-projectile-motion --input input.gil --output output.gil --prefab-id 1077936385 --x 3.47 --y 19.70 --gravity 20
opengil custom-vars list --input input.gil --prefab-id 1077936130
opengil custom-vars add --input input.gil --output output.gil --prefab-id 1077936130 --name openGilVar --type str
opengil custom-vars remove --input input.gil --output output.gil --prefab-id 1077936130 --name openGilVar
opengil custom-vars copy-all --input input.gil --output output.gil --from-prefab-id 1077936130 --to-prefab-id 1077936131
opengil custom-vars sync-tab --input input.gil --output output.gil --source-prefab-id 1077936340 --tab-id 6
opengil set-model --input input.gil --prefab-id 1077936130 --asset-id 20001220 --dry-run
opengil rename-prefab --input input.gil --prefab-id 1077936130 --name "Renamed Prefab" --dry-run
opengil attach-nodegraph --input input.gil --prefab-id 1077936130 --nodegraph-id 1073741913 --dry-run
opengil set-projectile-motion --input input.gil --prefab-id 1077936385 --angle 80 --speed 20 --dry-run
opengil custom-vars sync-tab --input input.gil --source-prefab-id 1077936340 --tab-id 6 --dry-run
opengil batch --input input.gil --output output.gil --ops ops.json
opengil batch --input input.gil --ops ops.json --dry-run
```

Custom variable writes edit definitions only, not runtime values. Valid `--type`
values are `entity`, `int`, `bool`, `float`, `str`/`string`, and `vec`/`vec3`.
Use `--tab-id` instead of `--tab` for non-ASCII tab names on Windows shells.

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
