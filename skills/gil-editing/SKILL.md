---
name: gil-editing
description: Use when working with openGil or Genshin/Miliastra files that use the `.gil` game file extension: inspect, validate, diff before/after samples, edit verified prefab/scene/preview/tab/nodegraph/decoration/attachment/custom variable/model/projectile/UI primitive structures, or research unknown `.gil` fields.
---

# openGil `.gil` File Editing

Use `opengil` as the primary tool for `.gil` file inspection, validation, research, and verified writes.

## Core Rules

- Start with `inspect` and `validate`.
- Use `list-*`, `get-*`, or `ui list` to locate ids before writes.
- Prefer numeric ids over names for writes.
- Prefer `--tab-id` over `--tab` on Windows command lines when tab names contain non-ASCII text.
- Use `--dry-run` before writes that change user data.
- For repeated edits, run each operation with `--dry-run` first, then either write accepted CLI operations in sequence or use the Python binding `GilDocument` API.
- Read the machine JSON envelope from stdout for both success and error results.
- Run structural `validate` after every write.
- Treat `validate` as structural validity, not semantic proof. Use operation summaries, semantic queries, game validation, and cross-save validation for confidence.
- For unknown structures, require before/after `.gil` samples, run `diff-summary`, reproduce the editor change, validate in game, then generalize.

## Tool Pattern

```powershell
opengil inspect --input input.gil
opengil validate --input input.gil
opengil set-model --input input.gil --output output.gil --prefab-id 1077936130 --asset-id 20001220 --dry-run
opengil set-model --input input.gil --output output.gil --prefab-id 1077936130 --asset-id 20001220
opengil validate --input output.gil
```

The CLI returns a JSON envelope on stdout. `validate` reports structural validity only; use operation-specific summaries and known semantic paths for semantic confidence.

## References

- CLI usage: `references/cli-usage.md`
- Full CLI API: `../../docs/cli_api.md`
- Full Python API: `../../docs/python_api.md`
- Verified operation surfaces: `references/verified-operations.md`
- Known semantic paths: `references/semantic-paths.md`
- Known pitfalls: `references/pitfalls.md`
- Unknown-structure workflow: `references/research-workflow.md`
- Legacy genshin-ts workflows: `references/legacy-workflows.md`
