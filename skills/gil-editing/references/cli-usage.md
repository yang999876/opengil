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
opengil set-model --input input.gil --prefab-id 1077936130 --asset-id 20001220 --dry-run
opengil rename-prefab --input input.gil --prefab-id 1077936130 --name "Renamed Prefab" --dry-run
opengil batch --input input.gil --output output.gil --ops ops.json
opengil batch --input input.gil --ops ops.json --dry-run
```

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
      "op": "set-empty-model",
      "prefabId": 1077936130
    }
  ]
}
```

Other planned write commands return `NOT_IMPLEMENTED` until their milestone lands.
