# openGil Development Notes

This document describes the current project shape and the preferred path for
adding new `.gil` capabilities. It is written for future maintainers and agents
working inside this repository.

## Current Status

openGil is a standalone C++20 CLI for inspecting and safely editing verified
`.gil` structures. The project is usable for internal workflows and agent-driven
editing, but it should still be treated as a pre-1.0 tool.

The current compatibility surface is the CLI JSON output, not the C++ library
API. The C++ API may still change while the project is young.

The safest supported workflow is:

```powershell
.\build\Release\opengil.exe inspect --input .\tests\fixtures\test1.gil
.\build\Release\opengil.exe <write-command> --input in.gil --output out.gil --dry-run
.\build\Release\opengil.exe <write-command> --input in.gil --output out.gil
.\build\Release\opengil.exe validate --input out.gil
```

`validate` is structural validation. It checks the `.gil` envelope and protobuf
wire parse. It does not prove semantic consistency such as unique ids, complete
tab mappings, mirrored scene/preview records, or absence of dangling references.

## Architecture

The project has four main layers:

```text
include/opengil/      Public C++ headers for core data and operations
src/core/             File envelope, wire parser, rebuild, sha256, JSON utility
src/semantic/         Read-only semantic queries over verified paths
src/ops/              Structured mutation operations
src/cli/              CLI parsing, file IO policy, JSON stdout formatting
```

Important boundaries:

- `src/core` owns low-level `.gil` bytes, wire parsing, rebuilding, and structural
  validation.
- `src/semantic` returns structured query results only.
- `src/ops` returns structured mutation summaries only.
- `src/cli` owns stdout JSON, command names, exit codes, dry-run/report behavior,
  and write policy.
- Domain headers in `include/opengil` must not expose `*_to_json()` helpers.
- Domain mutation types must not carry pre-rendered JSON strings.

CLI-only JSON formatters live in:

```text
src/cli/json_formatters.hpp
src/cli/json_formatters.cpp
```

This split is intentional. It keeps openGil usable as a future C++ library or
Python binding without forcing JSON output concerns into the domain layer.

## Safety Rules

When adding or changing write behavior:

- Use `parse_owned_fields_or_throw()` on write paths. The lenient
  `parse_owned_fields()` is for read-only scanning.
- Preserve unknown fields, field order, and untouched top-level field bytes.
- Rebuild only the modified subtree/top-level field when practical.
- Explicit user-provided ids must be checked for collisions.
- Write commands must support `--dry-run`.
- In-place writes must go through the CLI write helper; do not open and truncate
  the input file directly from an op.
- Ops should return `bytes`, `payload`, and a structured `summary`.
- CLI handlers should format summaries with `opengil::cli::*_to_json()`.
- After a write operation, tests should at least run structural validation on the
  mutated file.

## Adding A New Operation

Use this route for most new atomic features.

1. Add public types and function declarations in `include/opengil/<area>_ops.hpp`.
   Define a structured summary type with domain fields, not JSON.

2. Implement the mutation in `src/ops/<area>_ops.cpp`.
   Keep command-line parsing and JSON out of this file.

3. Add a formatter in `src/cli/json_formatters.hpp/.cpp`.
   Preserve existing CLI JSON conventions:

   ```json
   {
     "kind": "operationName",
     "changedTopFields": [4, 5]
   }
   ```

4. Add CLI dispatch in `src/cli/main.cpp`.
   The handler should:

   - load input once
   - parse arguments
   - call the op
   - write only if not `--dry-run`
   - produce the standard envelope JSON
   - support `--report` through existing helpers when applicable

5. Add batch support if the operation is useful for repeated agent edits.
   Extend the batch op parser and the batch dispatch in `src/cli/main.cpp`.

6. Add tests.
   Prefer unit tests for structured summaries and mutation behavior. Add CLI
   smoke tests when command JSON compatibility matters.

7. Run the standard checks:

   ```powershell
   cmake --build build --config Release
   ctest --test-dir build -C Release --output-on-failure
   rg "result_json" include src/ops src/semantic
   rg "_to_json" include/opengil
   rg '#include "opengil/json.hpp"' src/ops src/semantic
   ```

The last three searches should return no matches.

## Adding A Read-Only Query

Read-only queries usually belong in `src/semantic` if they describe verified
domain structure.

1. Add a result struct and function declaration in `include/opengil/semantic.hpp`
   or a more specific header if the area already has one.
2. Implement scanning in `src/semantic/semantic.cpp` or the relevant op file.
3. Add CLI formatter in `src/cli/json_formatters.cpp`.
4. Add a CLI handler in `src/cli/main.cpp`.
5. Add a fixture-backed unit or CLI test.

Read-only scanners may use tolerant parsing when useful, but they should not
hide parse failures that would make a write unsafe.

## File Map

Core files:

```text
src/core/gil.cpp       .gil envelope parse/build and structural validation
src/core/wire.cpp      protobuf wire parser/rebuilder and field utilities
src/core/sha256.cpp    sha256 for CLI input/output reports
src/core/json_value.cpp small JSON parser used for batch input
```

Semantic and operation areas:

```text
src/semantic/semantic.cpp              tabs, prefabs, models, nodegraph listing
src/ops/model_ops.cpp                  set-model / set-empty-model
src/ops/prefab_ops.cpp                 rename/delete/clone/copy prefab
src/ops/object_ops.cpp                 create objects and transforms
src/ops/nodegraph_ops.cpp              attach nodegraphs
src/ops/projectile_ops.cpp             projectile motion
src/ops/custom_vars_ops.cpp            custom variable sync operations
src/ops/decoration_ops.cpp             decoration add
src/ops/attachment_ops.cpp             attachment add
src/ops/attachment_from_decoration_ops.cpp derived attachment workflow
src/ops/ui_ops.cpp                     UI primitive list
src/ops/ui_patch_ops.cpp               UI primitive field patches
src/ops/ui_structure_ops.cpp           UI append/retain/copy structure ops
```

CLI files:

```text
src/cli/main.cpp             command parsing, dispatch, write policy, batch
src/cli/json_formatters.cpp  CLI JSON result bodies
```

`src/cli/main.cpp` is intentionally back in one file for now. If it grows again,
split by extracting real ownership boundaries such as argument parsing, write
policy, and command handlers, not by creating a thin forwarding app wrapper.

## Tests

Unit tests live in `tests/unit`. CLI smoke tests are registered in
`CMakeLists.txt` with `add_test`.

Good tests for a write operation usually check:

- expected summary fields
- expected changed top-level fields
- structural validation after mutation
- important semantic fields after mutation
- failure cases such as duplicate explicit ids or missing targets

Golden byte-for-byte tests are useful when the expected output is stable. When
byte-for-byte parity is not practical, compare verified semantic paths instead.

## Agent Skill

The agent-facing skill lives in:

```text
skills/gil-editing/
```

The skill should prefer `opengil` over legacy JSON IR workflows. It should use
ids rather than names for writes, run dry-runs for complex operations, validate
after writes, and use batch for repeated edits.

Unknown structures should still follow the research workflow: collect before and
after samples, diff, identify minimal verified paths, then implement replay-first
behavior before attempting generalized editing.

## Known Limits

- The project is pre-1.0. Public C++ headers may change.
- `validate` is structural, not full semantic validation.
- `.proto` files are documentation/reference only and are not used for lossless
  writeback.
- Python `.pyd`/pybind11 bindings are not part of the current target, but the
  domain/CLI JSON split keeps that path open.
- UI import workflows such as `ui import-geometrize` and `ui import-pixel` are
  intentionally not implemented in this round.

