# Verified Operation Surfaces

- Copy existing prefab to existing tab: `top4 + top6`, clone `top27.field1` prefab-side decoration records when present.
- Delete prefab: `top4 + top6 + top10 + top27`.
- Add decoration: `top4 + top8 + top27`.
- Add attachment point: `top4 + top8`.
- Attach nodegraph: append reference payload to prefab `4.1.7.13`, scene `5.1.6.13`, preview `8.1.6.13`; do not edit `top10`.
- Custom variable definitions only: prefab `4.1.8`, scene `5.1.7`, preview `8.1.7`. Do not write runtime values in v1. Sync add/remove/copy across prefab and linked scene/preview entries.
- Model asset id: prefab `4.1.2`, scene `5.1.8`, preview `8.1.8`; empty model also needs `tag 20 / field 29`.
- Rename prefab: prefab `4.1.6.11.1` when present, with fallbacks for older observed name paths. This changes `top4` only.
- Batch writes: apply supported atomic ops in memory and write once at the end.
- Projectile motion: prefab-space component only; identify component kind `1 = 11` and display name `projectile motion`; set/insert velocity `21.1.12.1.1/2`, optionally set/insert gravity `21.1.12.2`.
