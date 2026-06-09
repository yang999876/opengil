# Pitfalls

- Do not patch only one side of mirrored structures.
- Do not rely on names for destructive writes; names can have encoding issues.
- On Windows command lines, non-ASCII `--tab` values can arrive mojibaked. Use `list-tabs`, then pass `--tab-id`.
- Do not delete prefabs without handling `top10`.
- Do not create prefab ids from global max alone; stay in the source id band and avoid ids in `top4/top5/top8/top27`.
- For object creation, prefer explicit ids when reproducing research cases. Auto allocation is convenient but should be validated against the target workflow.
- If create-prefab or create-scene-prefab-instance cannot find a close template in the current file, provide `--template template.gil` from an observed before/after sample instead of inventing fields.
- When cloning prefabs, preserve the source prefab's internal reference list shape; only replace trailing source decoration ids with newly allocated ids.
- Do not treat `静止球` as an ordinary projectile ball template.
- `attachment add` requires existing attachment wrapper/container branches in both prefab `top4` and matching scene `top8`; if they are missing, use a before/after sample instead of inventing the wrapper.
- Transform writes are full-message replacements; pass all axes if you do not want defaults for omitted fields.
- Some projectile `y = 0` fields are omitted and must be inserted when writing.
