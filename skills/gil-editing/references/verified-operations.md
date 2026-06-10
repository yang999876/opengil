# Verified Operation Surfaces

- Clone existing prefab to existing tab: `top4 + top6`, clone `top27.field1` prefab-side decoration records when present, and update decoration reference lists in the cloned prefab entry.
- Copy prefab to tab: same write surface as clone existing prefab; default name is source prefab name plus `-copy`.
- Create scene object: copy a reusable `top5.field1` template, patch object id at `5.1.1`, asset refs at `5.1.2.1` and `5.1.8`, replace transform at `5.1.6.11`, append scene mapping `200 -> objectId` to category `3` in `top6`.
- Create prefab: copy a reusable `top4.field1` template, patch prefab id at `4.1.1`, asset id at `4.1.2`, replace transform at `4.1.7.11`, append prefab mapping `100 -> prefabId` to categories `6` and `3` in `top6`.
- Create scene prefab instance: copy a reusable `top5.field1` template, patch object id at `5.1.1`, prefab ref at `5.1.2.1`, asset id at `5.1.8`, replace transform at `5.1.6.11`, append scene mapping `200 -> objectId` to category `3` in `top6`.
- Delete prefab: remove `top4.field1` by prefab id, strip recursive `top6.field5` mappings for removed prefab/decorations, prune `top10` records that directly reference removed ids, and remove prefab-owned `top27.field1` records. Do not assume `top8` should change.
- Add decoration: append prefab-side `top27.field1`, append scene mirror `top27.field2` for each matching `top8` scene/preview entry, update prefab reference list `4.1.6.50`, and update scene reference list `8.1.5.50`.
- Add attachment point: append/upsert `21.1` and `32.501` records in prefab `4.1.7/4.1.8` and matching scene `8.1.6/8.1.7`; do not edit `top27/top10/top6`.
- Attach nodegraph: append reference payload to prefab `4.1.7.13`, scene `5.1.6.13`, preview `8.1.6.13`; do not edit `top10`.
- Custom variable definitions only: prefab `4.1.8`, scene `5.1.7`, preview `8.1.7`. Do not write runtime values in v1. Sync add/remove/copy across prefab and linked scene/preview entries.
- Model asset id: prefab `4.1.2`, scene `5.1.8`, preview `8.1.8`; empty model also needs `tag 20 / field 29`.
- Rename prefab: prefab `4.1.6.11.1` when present, with fallbacks for older observed name paths. This changes `top4` only.
- Scene transform: replace full transform at `5.1.6.11`.
- Preview transform: replace full transform at `8.1.6.11`.
- UI primitives: list and edit `top9.field502` entries. Append/append-many clone
  an observed primitive entry from a template file, patch entry/controller ids,
  append the controller child id list, and insert the new entries before the
  observed sentinel entry when present. Retain removes unkept primitive entries
  and rewrites the controller child id list. Patch operations update primitive
  type/color/name/layer/transform in place. Copy-transform-from-template keeps
  the target entry id and controller id, but uses the template primitive body.
- Batch writes: apply supported atomic ops in memory and write once at the end.
- Projectile motion: prefab-space component only; identify component kind `1 = 11` and display name `projectile motion`; set/insert velocity `21.1.12.1.1/2`, optionally set/insert gravity `21.1.12.2`.
