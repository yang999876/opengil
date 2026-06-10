---
name: gil-editing
description: Use when working with Genshin/Miliastra `.gil` files, prefab records, scene or preview instances, category tabs, nodegraph references, decorations, attachment points, custom variables, model asset ids, projectile parameters, or UI primitives. Prefer the openGil `opengil` CLI for inspecting, validating, batching, and safely editing `.gil` files without full JSON IR round-trips.
---

# GIL Editing

Use `opengil` as the primary tool for `.gil` work.

## Core Rules

- Inspect before editing.
- Prefer numeric ids over names for writes.
- Prefer `--tab-id` over `--tab` on Windows command lines when tab names contain non-ASCII text.
- Use `--dry-run` before complex writes.
- Use `batch` for repeated edits so the file is opened and written once.
- Run structural `validate` after every write.
- Do not treat structural `validate` as semantic proof; it does not check mirrored records, duplicate ids, tab mappings, or dangling references.
- Do not dump full JSON IR unless explicitly debugging legacy behavior.
- Do not guess unknown structures. Ask for `before.gil` and `after.gil`, run `diff-summary`, then make a replay-first workflow.

## Tool Pattern

```powershell
opengil inspect --input input.gil
opengil validate --input input.gil
opengil batch --input input.gil --output output.gil --ops ops.json --dry-run
opengil batch --input input.gil --output output.gil --ops ops.json
opengil validate --input output.gil
```

Stdout is machine JSON. Keep stderr as logs only.
`validate` reports structural validity only; use operation-specific summaries and
known semantic paths for semantic confidence until `validate --semantic` exists.

## References

- CLI usage: `references/cli-usage.md`
- Verified operation surfaces: `references/verified-operations.md`
- Known semantic paths: `references/semantic-paths.md`
- Known pitfalls: `references/pitfalls.md`
- Unknown-structure workflow: `references/research-workflow.md`
- Legacy genshin-ts workflows: `references/legacy-workflows.md`
