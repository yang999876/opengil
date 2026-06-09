# CLI Usage

Use `opengil` with JSON stdout.

Read-only commands implemented in the first milestone:

```powershell
opengil inspect --input test.gil
opengil validate --input test.gil
opengil diff-summary --before before.gil --after after.gil
opengil list-tabs --input test.gil
opengil list-prefabs --input test.gil
opengil list-prefabs --input test.gil --tab 球
opengil list-prefab-tabs --input test.gil --prefab-id 1077936130
opengil list-nodegraphs --input test.gil
opengil get-model --input test.gil --prefab-id 1077936130
```

Planned write commands return `NOT_IMPLEMENTED` until their milestone lands.

