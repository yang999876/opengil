# Pitfalls

- Do not patch only one side of mirrored structures.
- Do not rely on names for destructive writes; names can have encoding issues.
- Do not delete prefabs without handling `top10`.
- Do not create prefab ids from global max alone; stay in the source id band and avoid ids in `top4/top5/top8/top27`.
- Do not treat `静止球` as an ordinary projectile ball template.
- Some projectile `y = 0` fields are omitted and must be inserted when writing.

