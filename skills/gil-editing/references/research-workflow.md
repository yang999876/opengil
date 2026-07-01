# Research Workflow

For unknown `.gil` structures, use one-variable before/after research. The goal is to prove one editable behavior before turning it into a reusable op.

1. Start from an empty scene.
2. Add one target object, then save it as `gil1.gil`.
3. Change only one target parameter on that object, then save it as `gil2.gil`.
4. Run `opengil inspect` and `opengil validate` on both files.
5. Run `opengil diff-summary --before gil1.gil --after gil2.gil`.
6. Identify changed top-level fields and exact nested paths.
7. Write a replay script, semantic query, or openGil op in the current project style.
8. Generate a modified `.gil` with openGil and run `validate`.
9. Put the modified `.gil` back into the game and confirm the target behavior changed.
10. Cross-validate with a new save or a different object.
11. Include the test process, original `.gil` files, diff result, and validation notes in the PR.

Recommended research artifact layout:

```text
research/<feature-name>/
  README.md
  gil1.gil
  gil2.gil
  cross-gil1.gil
  cross-gil2.gil
  diff-summary.json
  notes.md
```

