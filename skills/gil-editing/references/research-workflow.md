# Research Workflow

For unknown `.gil` structures:

1. Prepare `before.gil`.
2. Make one minimal editor action.
3. Save `after.gil`.
4. Run `opengil diff-summary --before before.gil --after after.gil`.
5. Identify changed top-level fields and exact nested paths.
6. Build a replay script or operation that reproduces the editor change.
7. Only generalize after replay output matches the editor result or semantic checks.

