# HANDOFF

**Date**: 2026-04-26
**Repository**: `C:\Users\kakut\Works\Virtools\libnmo`
**Branch**: `main`
**Current focus**: nmo Unified Edit Kernel / Script Editor refactor
**Working mode**: current checkout only, no git worktrees unless explicitly re-authorized.

## Operating Rules

- Work in the current checkout.
- Do not use git worktrees in this repository unless explicitly re-authorized.
- Stage exact files or hunks only.
- Do not broad-stage the repository.
- Documentation may be staged/committed only when the user explicitly requests documentation changes.
- Commit messages must use the repo's imperative sentence-case style.
- Ballance is an acceptance fixture, not a hardcoded core mode.
- Do not use `Ballance.dll` for nmo/libnmo work unless specifically requested for Player smoke.

## Current Dirty Tree

The tree is not clean. Treat unrelated dirty/untracked paths as user-local state.

Known unrelated tracked modifications:

- `README.md`
- `docs/superpowers/plans/2026-04-20-behavior-graph-rewrite-cli.md`

Many unrelated untracked docs/temp files and generated outputs exist. Do not broad-stage.

This `HANDOFF.md` is intentionally updated because the user explicitly requested documentation updates.

## Recent Commits

Recent route-progress commits on `main`:

```text
05a591b0 Reject invalid semantic replace targets
7a6f8db3 Assert shared semantic validation
8ce673a2 Deduplicate semantic edit risks
fdba99d5 Extract semantic validator
304bc601 Align Lua behavior edit reports
d9479962 Regenerate command completions
911c89f5 Use registered blocks in interface tests
89021724 Add Ballance script edit acceptance checks
33bb8b5b Assert patch manifest strictness
71e160f1 Assert building block materialization fidelity
0f8d02d0 Assert debug probe validation
b04d6fd5 Route Lua behavior writes through edit plans
0861d5df Remove Lua report operation count
70b4916c Add data cell debug probe
33bf880a Expose Lua parameter handle writes
```

## Verified Status

Latest focused route regression passed:

```powershell
ctest --test-dir build -R "^(test_semantic_validator|test_edit_plan|test_cli_behavior_rewrite|test_cli_patch_apply|test_cli_script_run|test_cli_commands|test_repl_read_commands)$" --output-on-failure
```

Result:

- `7/7` passed

Latest full verification passed:

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

Result:

- `187/187` passed

## What Is Now In Place

### EditPlan / Executor

- Script write paths are largely expressed as `nmo_edit_plan_t` operations.
- Executor reports Schema v2 style fields:
  - `ok`
  - `dry_run`
  - `operations`
  - `changed_objects`
  - `created_objects`
  - `deleted_objects`
  - `semantic_risks`
  - `validation`
  - `diff`
  - `output_path`
- Patch v2 and Lua/script-run use operation handles for important post-add-node references.
- `nmo_edit_report_merge_semantic_risks` deduplicates semantic risks by severity/code/object id.

### Semantic Validator

- `include/behavior/nmo_semantic_validator.h` and `src/behavior/semantic_validator.c` exist.
- `nmo_behavior_edit_collect_semantic_risks` is retained as a compatibility wrapper.
- `behavior_rewrite.c` no longer owns the boundary delay/shared-parameter/message-flow scanner.
- Fold candidates and fold dry-run are covered by shared semantic validation tests.
- Current risk codes:
  - `dangling_boundary`
  - `activation_delay`
  - `shared_parameter`
  - `message_flow`
- Message-flow detection is based on behavior flags / BB registry metadata, not display names.
- Invalid replace targets in `nmo_semantic_validate_edit_plan` now fail instead of silently returning clean results.

### Lua

- Lua behavior writes are routed through edit plans for the migrated behavior transaction API.
- `behavior.commit` and `behavior.execute` reports are aligned with Schema v2 style Lua reports.
- Lua `nmo.plan` exposes:
  - `connect_parameter_to_handle`
  - `set_parameter_value_from_handle`
  - `set_parameter_bytes_from_handle`
- `nmo._executor` used by `script run` exposes:
  - `connect_parameter_to_handle`
  - `set_parameter_value_from_handle`
  - `set_parameter_bytes_from_handle`
- Lua plan report no longer emits legacy `operation_count`; tests use `#report.operations`.

### Patch v2

Patch manifest v2 supports handle-based references for:

- `connect_parameter`
  - `target_operation`
  - `target_handle`
- `set_parameter_value`
  - `parameter_operation`
  - `parameter_handle`
- `set_parameter_bytes`
  - `parameter_operation`
  - `parameter_handle`

For handle writes to `input_param:*`, executor materializes an input parameter source before writing.

### Debug Probe

`nmo debug probe` is table-driven and uses EditPlan templates for:

- `2d-text`
- `console`
- `debug-output`
- `message-logger`
- `parameter-logger`
- `data-cell-logger`
- `control-marker`

`parameter-logger` connects a source parameter through `connect_parameter_to_handle`.

`data-cell-logger` accepts:

```text
--dataarray <id> --row <n> --col <n>
```

It currently tags the target cell in Output To Console text via EditPlan. It does not yet instrument the actual data-array write site.

### Ballance Acceptance

- Ballance script edit dry-run/validation acceptance tests are present.
- Ballance remains a fixture for generic nmo behavior editing, not a special core mode.
- Player runtime smoke is still optional/manual and not part of normal `ctest`.

## Remaining Gaps

### 1. Executor postflight semantic validation is not fully centralized

The standalone validator exists, but `nmo_edit_executor_execute` still mainly receives semantic risks through fold/replace rewrite reports. `nmo_semantic_validate_edit_plan` is not yet the single mandatory postflight semantic validation stage for every executor run.

Recommended next step:

- call the semantic validator from the executor postflight path
- merge validator risks into `nmo_edit_report_t`
- keep dedupe active so fold/replace risks do not double-report
- decide whether semantic validator failures should make `report.ok=false` or remain warnings per severity

### 2. `nmo_semantic_validate_edit_plan` coverage is still narrow

Current edit-plan semantic coverage is mainly fold/replace oriented.

Still needed:

- remove node/io/parameter/operation reference risk checks
- rewire link/operation dependency checks
- targetable BB consistency checks
- settings/default consistency checks
- scene/current-scene sensitivity checks
- interface chunk semantic attribution

### 3. Fold/replace still depend on rewrite-private internals

Fold/replace are `EditOp`s, but much of analyze/apply/risk/impact still lives in `behavior_rewrite.c`.

Recommended next step:

- keep public call path through `NMO_EDIT_OP_FOLD` / `NMO_EDIT_OP_REPLACE_BB`
- peel internal analyze/apply/impact collection into executor-facing primitives
- reduce separate rewrite scope paths where possible

### 4. Patch v2 still lacks full EditPlan roundtrip

Current patch v2 parse/apply is useful, but not complete as a stable serialization contract.

Still needed:

- `EditPlan -> JSON -> EditPlan` roundtrip API
- stable manifest serialization tests
- semantic diff beyond summary counts
- strict invalid-manifest diagnostics for every op variant

### 5. C API `nmo_behavior_execute` still uses the old result type

`include/behavior/nmo_behavior_execute.h` still aliases:

```c
typedef nmo_script_edit_report_t nmo_behavior_execute_result_t;
```

This keeps an older report surface alive outside the Schema v2 `nmo_edit_report_t` path.

### 6. Debug probe is still template-level

Current probes create diagnostic BBs, but do not yet deeply instrument runtime semantics.

Still needed:

- message logger that auto-targets send/wait message nodes
- parameter write logger that finds actual writer sites
- data-cell logger that instruments actual data array write points
- optional 2D Text screen probe with stronger target/font/settings checks

### 7. Ballance Player smoke is not automated

Manual/focused tests are good, but runtime acceptance should become repeatable.

Needed scenarios:

- loading
- menu
- level start
- message wait/send
- data array `end=-1`
- generated diagnostic `base.cmo`
- Player smoke using:
  - `C:\Users\kakut\Games\Ballance\Bin\Player.exe`

## Suggested Next Step

Highest-value next implementation task:

```text
Make executor postflight semantic validation mandatory and extend nmo_semantic_validate_edit_plan beyond fold/replace.
```

Reason:

- it makes the newly extracted validator operational instead of just available
- it keeps CLI, Lua, patch, and probe reports semantically consistent
- it is the cleanest next move toward one `EditPlan -> Executor -> Validator -> Report` path
