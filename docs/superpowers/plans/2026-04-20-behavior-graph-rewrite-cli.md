# Behavior Graph Rewrite CLI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Upgrade `nmo-cli` from field-level behavior editing to safe, repeatable behavior graph rewrite operations that can support Ballance `base.cmo` high-level BB conversion.

**Architecture:** Extend the existing behavior graph code instead of adding a separate tool. First expose complete graph boundary information, then add a small graph rewrite API in `src/behavior`, then bind it to `nmo behavior replace-bb`, `nmo behavior fold`, and `nmo patch apply`. `replace-bb` is intentionally leaf-only: it may replace an existing single building-block node, but it must reject script/graph behaviors with sub-behaviors, sub-behavior links, or parameter operations. Converting a script/graph into a high-level BB is a fold operation because Virtools saves and executes function behaviors differently from graph behaviors. Every write path must use dry-run, semantic validation, and roundtrip validation before writing output.

**Tech Stack:** C, libnmo session/object/runtime APIs, existing `tools/commands/nmo_cmd_behavior_*` CLI modules, yyjson, CTest integration tests, real `data/Ballance/base.cmo` fixtures.

---

## Context

The current `nmo-cli` can mutate raw object fields, for example changing a `CKBehavior` `block_guid`, `flags`, `priority`, and `save_flags`. That is not enough for complex Virtools behavior script changes. Ballance conversion needs graph-level edits:

- replace one BB while preserving control-flow links and parameter bindings;
- fold a subgraph into one high-level BB while preserving external boundary links;
- inspect exact input/output/parameter ownership before editing;
- dry-run and diff changes before saving;
- replay the same conversion from a patch file.

The immediate Ballance symptom is black screen after a partial high-level BB replacement. The CMO now loads, so the remaining failure is likely semantic: missing output links, lost parameter bindings, incorrect script activation order, or an incomplete high-level BB replacement. Do not continue manual `object set-field` chains as the primary approach.

## Script Model Constraints

These constraints come from `docs/spec/01-behavior-graph-model.md`, `docs/spec/03-file-format.md`, and `docs/spec/04-execution-semantics.md` and must govern all rewrite tasks:

- Virtools visual scripts are a dual-flow graph: control flow is `CKBehaviorLink` between `CKBehaviorIO` ports, and data flow is `CKParameterIn` source chains plus `CKParameterOut.destination_ids`.
- SDK naming for `CKBehaviorLink` is reversed: stored `in_io` is the source IO and stored `out_io` is the target IO.
- Function behaviors (`CKBEHAVIOR_USEFUNCTION` / building blocks) execute a C++ callback. Graph/script behaviors execute their `sub_behaviors`, `sub_behavior_links`, and `operations`.
- `CKBehavior::Save()` writes graph data only when `IsUsingFunction() == FALSE`. Therefore changing a graph/script parent into a building block is not a conservative field edit; it changes the save and execution model and can orphan or bypass the subgraph.
- `replace-bb` must only replace an existing leaf BB. A behavior with any sub-behavior, sub-behavior link, or parameter operation requires `fold` or another graph-aware operation.
- Semantic validation must compare preserved edge sets and parameter source/destination relationships, not just counts.
- Interface Chunk data is not just visual decoration. It has different persistence rules for building blocks vs graph behaviors, so rewrite operations must either preserve a still-valid Interface Chunk or intentionally canonicalize it.

## Repository Rules

- Repository root: `C:\Users\kakut\Works\Virtools\libnmo`
- Do not use git worktrees unless the user explicitly re-authorizes them.
- Work in the current checkout.
- Stage exact files or hunks only.
- Documentation changes are explicitly authorized for this plan, but implementation commits should avoid unrelated documentation.
- Commit messages must be imperative and capitalized, with no Conventional Commit prefixes.

## Existing Entry Points

Read these before implementation:

- `include/behavior/nmo_behavior_graph.h`
- `src/behavior/behavior_graph.c`
- `include/behavior/nmo_behavior_index.h`
- `src/behavior/behavior_index.c`
- `tools/commands/nmo_cmd_behavior.c`
- `tools/commands/nmo_cmd_behavior_graph.c`
- `tools/commands/nmo_cmd_behavior_link.c`
- `tools/commands/nmo_cmd_behavior_interface.c`
- `tools/commands/nmo_cmd_behavior_internal.h`
- `tools/commands/nmo_cmd_object_write.c`
- `tools/nmo_cmd_ctx.h`
- `tools/nmo_cmd_ctx.c`
- `tests/integration/test_cli_behavior_graph.c`
- `tests/integration/test_cli_write_commands.c`
- `tests/integration/write_semantic_probe.h`
- `tests/integration/write_semantic_probe.c`

Useful current behavior:

- `nmo behavior graph` already builds recursive graph output.
- Graph edges already expose behavior links, IO owners, activation delay, parameter source/destination edges, and operation edges.
- `nmo behavior dump --flows --values` already has some readable flow output.
- Interface editing commands already model dry-run/write command shape and should be used as CLI style reference.

## Target CLI Surface

Use `behavior` as the command group, not a new top-level `script` group, to match the existing CLI.

Read-only commands:

```powershell
nmo behavior graph-boundary <behavior-id> <file> [-f json]
nmo behavior refs <behavior-id> <file> [-f json]
nmo behavior graph --boundary <behavior-id> <file> [-f json]
```

Write commands:

```powershell
nmo behavior replace-bb <behavior-id> `
  --guid 42414C02-10000002 `
  --name "Ballance Load NMO Range" `
  [--version 65536] `
  [--preserve-links] `
  [--preserve-params] `
  [--dry-run] `
  <input.cmo> -o <output.cmo>

nmo behavior fold `
  --parent <behavior-id> `
  --nodes <id,id,id> `
  [--anchor <behavior-id>] `
  --guid 42414C07-10000007 `
  --name "Ballance Base Event Router" `
  [--version 65536] `
  [--preserve-boundary] `
  [--map-input old:new] `
  [--map-output old:new] `
  [--map-param old:new] `
  [--interface preserve|canonicalize|remove] `
  [--dry-run-report text|json] `
  [--dry-run] `
  <input.cmo> -o <output.cmo>

nmo patch apply <patch.json> [--dry-run]
nmo patch diff <patch.json>
```

The first milestone only needs `graph-boundary`, leaf-only `replace-bb`, and JSON patch apply for leaf `replace_bb` operations. Ballance high-level conversion for behavior ids `80`, `149`, `363`, and `2562` must wait for `fold` because those objects are scripts/graphs, not leaf BBs. `Event_handler` also requires fold, not simple replace.

## File Structure

Create:

- `include/behavior/nmo_behavior_boundary.h`  
  Public read-only API for graph boundary analysis.

- `src/behavior/behavior_boundary.c`  
  Computes incoming/outgoing control links, parameter source/destination crossings, internal nodes, and external nodes.

- `include/behavior/nmo_behavior_rewrite.h`  
  Public rewrite API for replacing BBs and later folding subgraphs.

- `src/behavior/behavior_rewrite.c`  
  Implements validated graph edits against session object state.

- `tools/commands/nmo_cmd_behavior_rewrite.h`

- `tools/commands/nmo_cmd_behavior_rewrite.c`  
  CLI command handlers for `graph-boundary`, `refs`, `replace-bb`, and later `fold`.

- `tools/commands/nmo_cmd_patch.h`

- `tools/commands/nmo_cmd_patch.c`  
  Patch file parser and dispatcher.

- `tests/integration/test_cli_behavior_rewrite.c`

- `tests/integration/test_cli_patch_apply.c`

Modify:

- `CMakeLists.txt`  
  Add new library sources and integration tests.

- `tools/CMakeLists.txt`  
  Add new CLI command files if the tool target lists command sources there.

- `tools/commands/nmo_cmd_behavior.c`  
  Dispatch new behavior subcommands.

- `tools/nmo_command_registry.c`

- `tools/nmo_command_registry.h`  
  Register `patch` top-level command if command registry requires explicit registration.

- `tools/nmo_cli_main.c` or `tools/nmo_cli_dispatch.c`  
  Only if top-level command registration is centralized there.

- `tests/integration/CMakeLists.txt`  
  Add the new integration test targets.

## Data Contracts

Boundary JSON must be stable and should look like this:

```json
{
  "behavior_id": 4692,
  "internal_nodes": [4692, 4701, 4702],
  "control_in": [
    {
      "link_id": 9001,
      "source_owner_id": 100,
      "source_io_id": 101,
      "target_owner_id": 4692,
      "target_io_id": 4700,
      "activation_delay": 0,
      "initial_activation_delay": 0
    }
  ],
  "control_out": [
    {
      "link_id": 9002,
      "source_owner_id": 4692,
      "source_io_id": 4703,
      "target_owner_id": 200,
      "target_io_id": 201,
      "activation_delay": 0,
      "initial_activation_delay": 0
    }
  ],
  "parameter_in": [
    {
      "source_parameter_id": 300,
      "target_parameter_id": 301,
      "source_owner_id": 100,
      "target_owner_id": 4692,
      "type_guid": "00000000-00000000",
      "shared": true
    }
  ],
  "parameter_out": []
}
```

Patch JSON v1 must be intentionally small:

```json
{
  "version": 1,
  "input": "C:/Users/kakut/Games/Ballance/base.cmo",
  "output": "C:/Users/kakut/Games/Ballance/base_ballance_bb_fixed.cmo",
  "operations": [
    {
      "op": "replace_bb",
      "behavior_id": 343,
      "name": "Ballance Load NMO Range",
      "guid": "42414C02-10000002",
      "version": 65536,
      "preserve_links": true,
      "preserve_params": true
    }
  ]
}
```

Patch JSON v1 only accepts `replace_bb` for existing leaf BBs. Script/graph conversions, including Ballance behavior ids `80`, `149`, `363`, and `2562`, must be represented by `fold`; `replace_bb` must reject them.

The v1 patch schema must reserve this complete `fold` operation shape:

```json
{
  "op": "fold",
  "parent": 4692,
  "nodes": [4701, 4702, 4703],
  "anchor": 4701,
  "name": "Ballance Base Event Router",
  "guid": "42414C07-10000007",
  "version": 65536,
  "preserve_boundary": true,
  "inputs": [
    { "old_index": 0, "old_io_id": 4704, "new_index": 0, "new_io_id": 4704, "label": "In" }
  ],
  "outputs": [
    { "old_index": 13, "old_io_id": 4713, "new_index": 0, "new_io_id": 4713, "label": "Load Menu Level" },
    { "old_index": 11, "old_io_id": 4711, "new_index": 1, "new_io_id": 4711, "label": "Exit To System" },
    { "old_index": 6, "old_io_id": 4706, "new_index": 2, "new_io_id": 4706, "label": "Load Level" }
  ],
  "parameters": [
    { "old_parameter_id": 300, "new_parameter_id": 301, "label": "Level" }
  ],
  "interface": "preserve"
}
```

Fold patch rules:

- `parent`, `nodes`, `name`, `guid`, and `preserve_boundary` are required.
- `anchor` is optional only when the selected node set has exactly one behavior. Otherwise it must be explicit.
- `inputs` and `outputs` map boundary-visible `CKBehaviorIO` endpoints. `old_index:new_index` CLI shorthand means index-to-index mapping; patch JSON must also include resolved object ids once known.
- `parameters` maps boundary-visible parameter object ids. Do not use parameter array index as the durable patch identity.
- `inputs`, `outputs`, and `parameters` must be explicit whenever more than one boundary edge exists in that category.
- `interface` accepts `preserve`, `canonicalize`, or `remove`; default is `preserve`, and write must reject if preserve is impossible.
- Unknown fields are errors. Add `--allow-unknown-fields` later only if needed.

## Task 1: Boundary API

**Files:**

- Create: `include/behavior/nmo_behavior_boundary.h`
- Create: `src/behavior/behavior_boundary.c`
- Modify: `CMakeLists.txt`
- Test: `tests/integration/test_cli_behavior_rewrite.c`

- [ ] **Step 1: Write failing tests for boundary JSON**

Add `tests/integration/test_cli_behavior_rewrite.c` with a smoke test that runs:

```powershell
nmo -f json behavior graph-boundary 237 data/Ballance/base.cmo
```

Expected assertions:

- command is `behavior.graph-boundary`;
- `data.behavior_id == 237`;
- `data.internal_nodes` is an array and contains `237`;
- `data.control_in`, `data.control_out`, `data.parameter_in`, and `data.parameter_out` exist as arrays.

Use the existing helpers in `tests/integration/test_cli_behavior_graph.c` as the copy source for `run_cli_capture`, `run_json_command`, and JSON helper functions.

- [ ] **Step 2: Run the test and verify it fails**

Run:

```powershell
ctest --test-dir build -R test_cli_behavior_rewrite --output-on-failure
```

Expected: failure because `behavior graph-boundary` is not implemented or the test target is not registered yet.

- [ ] **Step 3: Implement boundary structs**

In `include/behavior/nmo_behavior_boundary.h`, define:

```c
typedef struct nmo_behavior_boundary_control_edge {
    nmo_object_id_t link_id;
    nmo_object_id_t source_owner_id;
    nmo_object_id_t source_io_id;
    nmo_object_id_t target_owner_id;
    nmo_object_id_t target_io_id;
    int32_t activation_delay;
    int32_t initial_activation_delay;
} nmo_behavior_boundary_control_edge_t;

typedef struct nmo_behavior_boundary_parameter_edge {
    nmo_object_id_t source_parameter_id;
    nmo_object_id_t target_parameter_id;
    nmo_object_id_t source_owner_id;
    nmo_object_id_t target_owner_id;
    nmo_guid_t type_guid;
    bool shared;
} nmo_behavior_boundary_parameter_edge_t;

typedef struct nmo_behavior_boundary {
    nmo_object_id_t behavior_id;
    nmo_object_id_t *internal_nodes;
    size_t internal_node_count;
    nmo_behavior_boundary_control_edge_t *control_in;
    size_t control_in_count;
    nmo_behavior_boundary_control_edge_t *control_out;
    size_t control_out_count;
    nmo_behavior_boundary_parameter_edge_t *parameter_in;
    size_t parameter_in_count;
    nmo_behavior_boundary_parameter_edge_t *parameter_out;
    size_t parameter_out_count;
    size_t broken_links;
    size_t missing_nodes;
} nmo_behavior_boundary_t;
```

Expose:

```c
NMO_API bool nmo_behavior_boundary_build(nmo_context_t *ctx,
                                         nmo_session_t *session,
                                         nmo_object_id_t behavior_id,
                                         uint32_t max_depth,
                                         nmo_behavior_boundary_t *out_boundary);

NMO_API void nmo_behavior_boundary_free(nmo_behavior_boundary_t *boundary);
```

- [ ] **Step 4: Implement boundary builder**

In `src/behavior/behavior_boundary.c`, use `nmo_behavior_graph_build()` as the data source. Treat `graph.nodes` as the internal node set. Classify each graph edge:

- `behavior_link`: crossing into internal set is `control_in`; crossing out is `control_out`;
- `param_source` / `param_dest`: crossing into internal set is `parameter_in`; crossing out is `parameter_out`;
- ignore internal-to-internal edges for boundary output;
- include broken/missing counts from `nmo_behavior_graph_t`.

When owner information is missing, use `0` and keep the edge rather than silently dropping it. Boundary analysis is diagnostic; rewrite validation can reject incomplete data later.

- [ ] **Step 5: Add CMake source and test target**

Add `src/behavior/behavior_boundary.c` to the same library target that already includes `src/behavior/behavior_graph.c`. Add `tests/integration/test_cli_behavior_rewrite.c` to integration tests.

- [ ] **Step 6: Implement CLI JSON output**

Add `tools/commands/nmo_cmd_behavior_rewrite.c` and dispatch from `nmo_cmd_behavior_in_session()` and non-session command handling as needed.

The JSON output should include exact arrays from the Data Contracts section. Text output can be minimal:

```text
Behavior #237 boundary
Internal nodes: 12
Control in: 1
Control out: 2
Parameter in: 3
Parameter out: 0
```

- [ ] **Step 7: Run and commit**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build -R test_cli_behavior_rewrite --output-on-failure
```

Commit exact files:

```powershell
git add include/behavior/nmo_behavior_boundary.h src/behavior/behavior_boundary.c tools/commands/nmo_cmd_behavior_rewrite.h tools/commands/nmo_cmd_behavior_rewrite.c tools/commands/nmo_cmd_behavior.c CMakeLists.txt tests/integration/CMakeLists.txt tests/integration/test_cli_behavior_rewrite.c
git commit -m "Add behavior graph boundary export"
```

## Task 2: Safe Leaf BB Replacement API

**Files:**

- Create: `include/behavior/nmo_behavior_rewrite.h`
- Create: `src/behavior/behavior_rewrite.c`
- Modify: `CMakeLists.txt`
- Modify: `tools/commands/nmo_cmd_behavior_rewrite.c`
- Test: `tests/integration/test_cli_behavior_rewrite.c`

- [ ] **Step 1: Write failing tests for dry-run leaf replace**

Add a test that runs:

```powershell
nmo -f json behavior replace-bb 343 --guid 42414C02-10000002 --name "Ballance Load NMO Range" --preserve-links --preserve-params --dry-run data/Ballance/base.cmo
```

Expected:

- exit code success;
- command is `behavior.replace-bb`;
- `data.dry_run == true`;
- `data.changed == true`;
- no output file is created;
- JSON includes `before.guid`, `after.guid`, `before.flags`, `after.flags`, and `eligibility`;
- `eligibility.leaf == true`;
- `eligibility.sub_behaviors == 0`;
- `eligibility.sub_behavior_links == 0`;
- `eligibility.operations == 0`;
- JSON includes exact preserved boundary edge counts: `preserved.control_in`, `preserved.control_out`, `preserved.parameter_in`, and `preserved.parameter_out`.

- [ ] **Step 2: Write failing tests for rejecting graph/script replacement**

Add a test that runs:

```powershell
nmo -f json behavior replace-bb 363 --guid 42414C02-10000002 --name "Ballance Load NMO Range" --preserve-links --preserve-params --dry-run data/Ballance/base.cmo
```

Expected:

- exit code is non-success;
- JSON or stderr diagnostic says behavior `363` is not leaf-replaceable;
- diagnostic includes the target's `sub_behaviors`, `sub_behavior_links`, and `operations` counts;
- no output file is created;
- this proves `replace-bb` never folds a graph/script parent.

- [ ] **Step 3: Write failing tests for saved replace**

Use a temporary output file:

```powershell
nmo behavior replace-bb 343 --guid 42414C02-10000002 --name "Ballance Load NMO Range" --preserve-links --preserve-params data/Ballance/base.cmo -o test_replace_bb.cmo
nmo validate all test_replace_bb.cmo
nmo object show 343 test_replace_bb.cmo
```

Expected:

- output file exists;
- validate succeeds;
- object show reports `CKBEHAVIOR_BUILDINGBLOCK`;
- `block_guid` is `42414C02-10000002`;
- behavior input/output object ids are unchanged from original.

- [ ] **Step 4: Run tests and verify they fail**

Run:

```powershell
ctest --test-dir build -R test_cli_behavior_rewrite --output-on-failure
```

Expected: failure because `replace-bb` does not exist.

- [ ] **Step 5: Define rewrite API**

In `include/behavior/nmo_behavior_rewrite.h`:

```c
typedef struct nmo_behavior_replace_bb_desc {
    nmo_object_id_t behavior_id;
    nmo_guid_t block_guid;
    const char *name;
    uint32_t block_version;
    bool preserve_links;
    bool preserve_params;
} nmo_behavior_replace_bb_desc_t;

typedef struct nmo_behavior_rewrite_report {
    bool changed;
    bool eligible_leaf;
    nmo_object_id_t behavior_id;
    uint32_t before_flags;
    uint32_t after_flags;
    nmo_guid_t before_guid;
    nmo_guid_t after_guid;
    size_t sub_behavior_count;
    size_t sub_behavior_link_count;
    size_t operation_count;
    size_t preserved_inputs;
    size_t preserved_outputs;
    size_t preserved_in_parameters;
    size_t preserved_out_parameters;
    size_t preserved_local_parameters;
    size_t preserved_control_in;
    size_t preserved_control_out;
    size_t preserved_parameter_in;
    size_t preserved_parameter_out;
    const char *diagnostic_code;
    const char *diagnostic_message;
    size_t diagnostics_count;
} nmo_behavior_rewrite_report_t;

NMO_API nmo_status_t nmo_behavior_replace_bb(nmo_context_t *ctx,
                                             nmo_session_t *session,
                                             const nmo_behavior_replace_bb_desc_t *desc,
                                             nmo_behavior_rewrite_report_t *report);
```

The report must expose a stable diagnostic code and human-readable diagnostic string. Callers must be able to explain rejection reasons without inferring them only from `diagnostics_count`.

- [ ] **Step 6: Implement minimal leaf replace**

In `src/behavior/behavior_rewrite.c`:

- resolve object by id;
- require object derives from `CKBehavior`;
- get `nmo_behavior_state_t`;
- require the current behavior already has `CKBEHAVIOR_BUILDINGBLOCK`;
- require the current behavior does not have `CKBEHAVIOR_SCRIPT`;
- require `sub_behaviors.count == 0`;
- require `sub_behavior_links.count == 0`;
- require `operations.count == 0`;
- return `NMO_ERR_INVALID_STATE` plus a diagnostic if the target is a script, graph, or non-leaf behavior;
- preserve existing input, output, in-parameter, out-parameter arrays by leaving them untouched;
- preserve existing local parameter arrays by leaving them untouched;
- set:
  - `flags |= CKBEHAVIOR_BUILDINGBLOCK | CKBEHAVIOR_USEFUNCTION`;
  - clear graph-only flags only if existing code has a known mask for graph/script flags;
  - `priority = 0`;
  - `block_guid = desc->block_guid`;
  - `block_version = desc->block_version ? desc->block_version : 65536`;
  - `save_flags = 6656` only if current BB write path uses that value for CK2-compatible BBs;
- update object name if `desc->name` is non-empty and existing object rename API is available.

If sub-behaviors, sub-behavior links, or operations exist, reject the command. Cleanup and graph collapsing belong to `fold`, not to leaf BB replacement.

- [ ] **Step 7: Add semantic validation**

Before returning success:

- build boundary before and after replacement;
- when `preserve_links` is true, assert exact control boundary edge sets are unchanged, including `link_id`, source owner/io, target owner/io, activation delay, and initial activation delay;
- when `preserve_params` is true, assert exact parameter boundary edge sets are unchanged, including source parameter id, target parameter id, source owner id, target owner id, type GUID, and shared flag;
- assert all original input and output ids still exist;
- assert all original input, output, and local parameter ids still exist;
- assert each original `CKParameterIn.source_id` and shared flag match;
- assert each original `CKParameterOut.destination_ids` set matches;
- return `NMO_ERR_INVALID_STATE` if validation fails.

- [ ] **Step 8: Add CLI implementation**

Parse:

- `--guid`;
- `--name`;
- `--version`;
- `--preserve-links`;
- `--preserve-params`;
- `--dry-run`;
- `-o` / `--output`.

Rules:

- write commands require `-o` unless `--dry-run`;
- reject non-leaf targets before writing output;
- rejection diagnostics must include child, link, and operation counts;
- dry-run never writes;
- JSON output includes report;
- text output says what changed and where saved.

- [ ] **Step 9: Run and commit**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build -R test_cli_behavior_rewrite --output-on-failure
ctest --test-dir build -R "test_real_file_roundtrip|test_object_layer_acceptance|test_cli_write_commands" --output-on-failure
```

Commit exact files:

```powershell
git add include/behavior/nmo_behavior_rewrite.h src/behavior/behavior_rewrite.c tools/commands/nmo_cmd_behavior_rewrite.h tools/commands/nmo_cmd_behavior_rewrite.c CMakeLists.txt tests/integration/test_cli_behavior_rewrite.c
git commit -m "Add safe behavior BB replacement"
```

## Task 3: Patch Apply v1

**Files:**

- Create: `tools/commands/nmo_cmd_patch.h`
- Create: `tools/commands/nmo_cmd_patch.c`
- Modify: `tools/nmo_command_registry.c`
- Modify: `tools/nmo_command_registry.h`
- Modify: `tools/nmo_cli_dispatch.c` or the actual top-level dispatcher if different
- Modify: `tools/CMakeLists.txt`
- Test: `tests/integration/test_cli_patch_apply.c`

- [ ] **Step 1: Write failing patch apply test**

Create a temporary JSON patch in the test:

```json
{
  "version": 1,
  "input": "data/Ballance/base.cmo",
  "output": "test_patch_replace_bb.cmo",
  "operations": [
    {
      "op": "replace_bb",
      "behavior_id": 343,
      "name": "Ballance Load NMO Range",
      "guid": "42414C02-10000002",
      "version": 65536,
      "preserve_links": true,
      "preserve_params": true
    }
  ]
}
```

Run:

```powershell
nmo patch apply test_patch_replace_bb.json
```

Expected:

- output file exists;
- validate succeeds;
- behavior 343 has the target GUID;
- JSON dry-run mode reports the operation without writing.

Also create a negative patch fixture that attempts `replace_bb` on behavior `363`. Expected: apply fails before saving because `363` is a graph/script behavior with children/links, and the diagnostic names the non-leaf counts.

- [ ] **Step 2: Run test and verify failure**

Run:

```powershell
ctest --test-dir build -R test_cli_patch_apply --output-on-failure
```

Expected: failure because `patch` command is not registered.

- [ ] **Step 3: Implement strict patch parser**

Use yyjson. Validate:

- root is object;
- `version == 1`;
- `input` and `output` are non-empty strings;
- `operations` is a non-empty array;
- each op has `op`;
- only leaf `replace_bb` is accepted in v1;
- `behavior_id`, `guid`, and `name` are required for `replace_bb`;
- reject unknown fields with a message naming the field.

- [ ] **Step 4: Implement apply**

Flow:

1. load input once into a session;
2. apply operations in array order through the rewrite API;
3. collect per-operation reports;
4. if `--dry-run`, print report and do not save;
5. otherwise save to `output`;
6. run in-process validation if available, or invoke the same validation path used by `nmo validate all`.

If any `replace_bb` operation targets a script, graph, or non-leaf behavior, reject the patch and do not save output.

- [ ] **Step 5: Implement patch diff**

`nmo patch diff <patch.json>` should dry-run operations and print only planned changes:

```text
replace_bb #343: guid 7BD977D7-26396C0C -> 42414C02-10000002, name -> Ballance Load NMO Range
```

No save.

- [ ] **Step 6: Run and commit**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build -R "test_cli_patch_apply|test_cli_behavior_rewrite" --output-on-failure
ctest --test-dir build -R "test_cli_write_commands|test_real_file_roundtrip|test_object_layer_acceptance" --output-on-failure
```

Commit exact files:

```powershell
git add tools/commands/nmo_cmd_patch.h tools/commands/nmo_cmd_patch.c tools/nmo_command_registry.c tools/nmo_command_registry.h tools/nmo_cli_dispatch.c tools/CMakeLists.txt tests/integration/CMakeLists.txt tests/integration/test_cli_patch_apply.c
git commit -m "Add behavior rewrite patch apply"
```

## Task 4: Ballance Fold Patch Fixture

**Files:**

- Create later: `examples/ballance-base-fold-v1.json`
- Test: existing CLI and validate commands

- [ ] **Step 1: Defer repository fixture until fold exists**

Do not create a repository fixture that uses `replace_bb` for Ballance behavior ids `80`, `149`, `363`, or `2562`. CLI inspection shows these are script/graph behaviors, not leaf BBs:

- `80` is `Delete_All_Loaded_NMO`, a script;
- `149` is `Delete_Loaded_NMO`, a script;
- `363` is `Loading_Manager`, a script;
- `2562` is `fill logic_ScriptArray`, a graph.

Those conversions require `fold` operations. A pre-fold fixture may live outside the repository only as a diagnostic input, and patch apply must reject it if it contains `replace_bb` for those ids.

- [ ] **Step 2: Create fixture after fold semantics are implemented**

After Task 5 implements `fold`, create a fixture shaped like this:

```json
{
  "version": 1,
  "input": "C:/Users/kakut/Games/Ballance/base.cmo",
  "output": "C:/Users/kakut/Games/Ballance/base_ballance_bb_fixed.cmo",
  "operations": [
    {
      "op": "fold",
      "parent": 4692,
      "nodes": [2364, 2208],
      "anchor": 2364,
      "name": "Ballance Event Handler",
      "guid": "42414C07-10000007",
      "version": 65536,
      "preserve_boundary": true,
      "outputs": [
        { "old_index": 0, "old_io_id": 2367, "new_index": 0, "new_io_id": 2367, "label": "Out" }
      ],
      "interface": "preserve"
    }
  ]
}
```

The final node list must be generated from `fold-candidates`, not guessed. The example above is a child-region fold shape, not approval to use those exact ids for the final Ballance conversion. A graph/script object must not be represented as `parent=<same id>, nodes=[<same id>]`; self-folding a graph root requires a future, separate operation.

- [ ] **Step 3: Dry-run fold patch**

Run:

```powershell
nmo patch diff examples/ballance-base-fold-v1.json
nmo patch apply examples/ballance-base-fold-v1.json --dry-run
```

Expected:

- fold operations reported with moved links, preserved parameters, and nodes to delete;
- no output file overwritten in dry-run.

- [ ] **Step 4: Apply patch and validate**

Run:

```powershell
nmo patch apply examples/ballance-base-fold-v1.json
nmo validate all C:\Users\kakut\Games\Ballance\base_ballance_bb_fixed.cmo
```

Expected:

- output file saved;
- validate result is `VALID`;
- object show confirms fold-created or fold-transformed high-level BBs for the intended Ballance conversions;
- graph-boundary comparison explains every external edge preserved or intentionally rewired.

- [ ] **Step 5: Commit fixture**

Commit exact file only if the user agrees that an example patch with local absolute Ballance paths belongs in the repository. If not, keep it uncommitted or write it under `C:\Users\kakut\Games\Ballance`.

Suggested commit if approved:

```powershell
git add examples/ballance-base-fold-v1.json
git commit -m "Add Ballance base fold patch"
```

## Task 5: Complete Subgraph Fold Write

**Files:**

- Modify: `include/behavior/nmo_behavior_boundary.h`
- Modify: `include/behavior/nmo_behavior_rewrite.h`
- Modify: `src/behavior/behavior_boundary.c`
- Modify: `src/behavior/behavior_rewrite.c`
- Modify: `tools/commands/nmo_cmd_behavior_rewrite.c`
- Modify: `tools/commands/nmo_cmd_patch.c`
- Test: `tests/integration/test_cli_behavior_rewrite.c`

Fold is a first-class graph rewrite operation. Do not implement it as a wrapper around `replace-bb`. `replace-bb` is for leaf BBs only; `fold` is for replacing a script/graph region with one high-level BB while preserving the region's external behavior boundary.

First implementation constraint: Task 5 supports only **child-region fold**. `parent` is the containing graph/script behavior. `nodes` is a non-empty set of child behavior ids contained by `parent`; `nodes` must not include `parent`. `anchor` must be one behavior in `nodes` unless `nodes` has exactly one behavior. Folding a graph/script object itself into a high-level BB is a separate future operation and must not be encoded as `parent=<same id>, nodes=[<same id>]`.

Compatibility constraint: the existing implementation already exposes `nmo_behavior_fold_analyze()` and `nmo_behavior_fold()`. Add `nmo_behavior_fold_apply()` as the new write entry point, then keep `nmo_behavior_fold()` as a compatibility wrapper until callers migrate.

- [ ] **Step 1: Write failing tests for fold-candidates**

Add tests that run:

```powershell
nmo -f json behavior fold-candidates --parent 4692 data/Ballance/base.cmo
```

Expected JSON:

- command is `behavior.fold-candidates`;
- `data.parent_id == 4692`;
- `data.parent_type` is `Script`, `Graph`, or `BB`;
- `data.candidates` is an array;
- each candidate has `nodes`, `boundary.control_in`, `boundary.control_out`, `boundary.parameter_in`, `boundary.parameter_out`, and `interface`;
- no output file is created.

This command is read-only and must work before `fold` write exists.

- [ ] **Step 2: Implement fold-candidates**

`fold-candidates` should group child behavior nodes by connected component inside the selected parent behavior. It must use real graph data from `nmo_behavior_graph_build()` and the boundary API from Task 1, not name matching.

Candidate output must include:

- parent behavior id, name, type, flags, and class id;
- whether the parent is a script, graph, or existing BB;
- complete candidate behavior ids;
- internal behavior links;
- exact boundary edge sets, not only counts;
- input/output names and indices when they can be resolved from `CKBehaviorIO`;
- parameter names, ids, type GUIDs, and shared flags;
- interface chunk availability, node positions, and whether the interface can be preserved.

Candidate output should include warnings, not hard errors, for ambiguous semantic labels. For example, if `Event_handler` has 11 outgoing message paths but labels cannot be recovered, emit a warning requiring explicit `--map-output`.

- [ ] **Step 3: Write failing tests for fold dry-run rejection**

Add a small synthetic fixture in the integration test or use an existing fixture with a graph behavior. Run:

```powershell
nmo -f json behavior fold --parent <parent-id> --nodes <node-a,node-b> --guid 42414C07-10000007 --name "Folded BB" --preserve-boundary --dry-run <fixture.cmo>
```

Expected when mappings are ambiguous:

- exit code is argument/validation error;
- JSON is still emitted when `-f json` is selected;
- JSON contains `ok == false`;
- JSON contains `data.rejected == true`;
- JSON contains `data.rejections[]` with at least one reason naming the missing mapping category, such as `outputs require explicit mapping`;
- no output file is written.

This test prevents silent guessing.

- [ ] **Step 4: Define fold descriptors and report structures**

Extend `include/behavior/nmo_behavior_rewrite.h` with explicit fold types:

```c
typedef enum nmo_behavior_fold_map_kind {
    NMO_BEHAVIOR_FOLD_MAP_INPUT = 0,
    NMO_BEHAVIOR_FOLD_MAP_OUTPUT = 1,
    NMO_BEHAVIOR_FOLD_MAP_PARAMETER = 2,
} nmo_behavior_fold_map_kind_t;

typedef struct nmo_behavior_fold_map {
    nmo_behavior_fold_map_kind_t kind;
    uint32_t old_index;
    uint32_t new_index;
    nmo_object_id_t old_id;
    nmo_object_id_t new_id;
    const char *label;
} nmo_behavior_fold_map_t;

typedef enum nmo_behavior_fold_interface_mode {
    NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE = 0,
    NMO_BEHAVIOR_FOLD_INTERFACE_CANONICALIZE = 1,
    NMO_BEHAVIOR_FOLD_INTERFACE_REMOVE = 2,
} nmo_behavior_fold_interface_mode_t;

typedef struct nmo_behavior_fold_desc {
    nmo_object_id_t parent_id;
    const nmo_object_id_t *node_ids;
    size_t node_count;
    nmo_object_id_t anchor_id;
    nmo_guid_t block_guid;
    const char *name;
    uint32_t block_version;
    bool preserve_boundary;
    const nmo_behavior_fold_map_t *input_maps;
    size_t input_map_count;
    const nmo_behavior_fold_map_t *output_maps;
    size_t output_map_count;
    const nmo_behavior_fold_map_t *parameter_maps;
    size_t parameter_map_count;
    nmo_behavior_fold_interface_mode_t interface_mode;
} nmo_behavior_fold_desc_t;
```

Extend the current fold report rather than replacing its detailed arrays. Counts are summaries only; dry-run JSON must be built from object-id arrays and map plans.

```c
typedef struct nmo_behavior_fold_rejection {
    const char *code;
    const char *message;
    nmo_object_id_t object_id;
} nmo_behavior_fold_rejection_t;

typedef struct nmo_behavior_fold_map_plan {
    nmo_behavior_fold_map_kind_t kind;
    uint32_t old_index;
    uint32_t new_index;
    nmo_object_id_t old_id;
    nmo_object_id_t new_id;
    nmo_object_id_t edge_id;
    nmo_object_id_t old_owner_id;
    nmo_object_id_t new_owner_id;
    nmo_guid_t type_guid;
    const char *label;
} nmo_behavior_fold_map_plan_t;

typedef struct nmo_behavior_fold_report {
    bool analysis_only;
    bool dry_run;
    bool rejected;
    bool can_write;
    bool changed;
    nmo_object_id_t parent_id;
    nmo_object_id_t anchor_id;
    nmo_object_id_t *selected_nodes;
    size_t selected_node_count;
    nmo_object_id_t *nodes_to_delete;
    size_t nodes_to_delete_count;
    nmo_behavior_boundary_t boundary;
    nmo_behavior_fold_map_plan_t *input_plans;
    size_t input_plan_count;
    nmo_behavior_fold_map_plan_t *output_plans;
    size_t output_plan_count;
    nmo_behavior_fold_map_plan_t *parameter_plans;
    size_t parameter_plan_count;
    nmo_behavior_fold_rejection_t *rejections;
    size_t rejection_count;
    size_t control_links_moved;
    size_t parameter_edges_moved;
    size_t behavior_links_deleted;
    size_t behaviors_deleted;
    size_t parameters_deleted;
    size_t operations_deleted;
    size_t interface_items_preserved;
    size_t interface_items_removed;
} nmo_behavior_fold_report_t;
```

Expose:

```c
NMO_API nmo_status_t nmo_behavior_fold_analyze(nmo_context_t *ctx,
                                               nmo_session_t *session,
                                               const nmo_behavior_fold_desc_t *desc,
                                               nmo_behavior_fold_report_t *report);

NMO_API nmo_status_t nmo_behavior_fold_apply(nmo_context_t *ctx,
                                             nmo_session_t *session,
                                             const nmo_behavior_fold_desc_t *desc,
                                             nmo_behavior_fold_report_t *report);

NMO_API nmo_status_t nmo_behavior_fold(nmo_context_t *ctx,
                                       nmo_session_t *session,
                                       const nmo_behavior_fold_desc_t *desc,
                                       nmo_behavior_fold_report_t *report);
```

`fold_analyze` must perform the same validation as write but make no mutations.

- [ ] **Step 5: Implement preflight validation**

Before mutation, reject if any rule fails:

- `parent_id` is missing or not a behavior;
- selected node set is empty;
- selected node set includes `parent_id`;
- every selected node must be a direct or recursive child of `parent_id`;
- selected nodes must form a closed internal region except for computed boundary edges;
- a selected non-anchor behavior is referenced by any external object outside the fold boundary;
- any `CKBehaviorLink` endpoint cannot be resolved to an owner;
- more than one incoming control edge exists without explicit input maps;
- more than one outgoing control edge exists without explicit output maps;
- more than one boundary parameter edge exists without explicit parameter maps;
- any boundary parameter type cannot be matched by explicit parameter maps;
- `--preserve-boundary` is absent for graph/script folds;
- `interface=preserve` is requested but interface data references objects that would be deleted and cannot be moved to the anchor;
- the anchor is not in the selected node set;
- the anchor is not a behavior;
- the anchor is already a leaf BB with no selected internal graph unless this is a no-op dry-run.

The error message must name the failing rule and the relevant object id.

- [ ] **Step 6: Choose and transform the anchor**

`fold` should reuse an anchor behavior by default instead of creating a new object. Reason:

- preserves one original object id;
- keeps interface position and user-visible layout more stable;
- reduces link and parameter remapping;
- makes diffs easier to audit.

Anchor selection:

- if `--anchor` is supplied, use it;
- if node set contains one behavior, use it;
- otherwise reject and require `--anchor`.

Transform the anchor into the high-level BB:

- set name to `desc->name`;
- set `flags` to include `CKBEHAVIOR_BUILDINGBLOCK` and `CKBEHAVIOR_USEFUNCTION`;
- clear script/graph-only bits only through a named helper with comments explaining CK2 semantics;
- set `priority = 0`;
- set `block_guid = desc->block_guid`;
- set `block_version = desc->block_version ? desc->block_version : 65536`;
- set CK2-compatible BB save flags;
- preserve or rebuild input/output/parameter arrays according to the mapping plan.

- [ ] **Step 7: Build replacement signature**

First implementation mode is `--preserve-boundary`; do not implement `--canonical-signature` yet.

For `--preserve-boundary`:

- reuse boundary-visible `CKBehaviorIO` objects where possible;
- reuse boundary-visible input/output parameter objects where possible;
- create missing anchor IO/parameter objects only when an explicit map requires a new endpoint;
- preserve names and type GUIDs on reused objects;
- record all reused and created endpoint ids in the report.

Do not trust the target BB prototype to auto-create a matching signature offline. Virtools creates BB instance IO/parameters at runtime/design time; offline CMO rewrite must make the saved object graph internally consistent by itself.

- [ ] **Step 8: Rewire control links**

For every boundary control link:

- incoming edge: rewrite target IO to mapped anchor input IO;
- outgoing edge: rewrite source IO to mapped anchor output IO;
- preserve `activation_delay` and `initial_activation_delay`;
- preserve link object id;
- update parent `sub_behavior_links` only if ownership requires it.

Use an inline comment in code:

```c
/* CK2/SDK naming is counterintuitive: link in_io_id is the source IO,
 * and link out_io_id is the target IO. Keep graph edge direction
 * source owner -> target owner.
 */
```

This is mandatory because an inverted rewrite will still validate structurally but break gameplay.

- [ ] **Step 9: Rewire parameter boundary edges**

For every boundary parameter crossing:

- external source to internal target becomes external source to mapped anchor input parameter;
- internal source to external target becomes mapped anchor output parameter to external target;
- preserve type GUIDs;
- preserve `is_shared`;
- preserve destination list order where possible;
- remove old destination ids only after the new destination id is inserted;
- reject if a parameter map is missing or type-incompatible.

Do not infer parameter maps by index when more than one parameter boundary edge exists.

- [ ] **Step 10: Delete internal-only closure**

After rewiring, compute the deletion closure. Delete only objects that are wholly internal:

- selected non-anchor behaviors;
- internal-only `CKBehaviorLink` objects;
- internal-only `CKBehaviorIO` objects;
- internal-only `CKParameterIn`, `CKParameterOut`, `CKParameterLocal`, and `CKParameter` objects;
- internal-only `CKParameterOperation` objects;
- interface entries that reference deleted objects when `interface=remove` or `interface=canonicalize`.

Never delete:

- external link endpoints;
- boundary-visible parameters;
- objects still referenced by `source_id` or `destination_ids`;
- objects referenced by behavior arrays outside the selected parent;
- objects referenced by interface data that is being preserved.

Before deletion, run a reference scan and list all would-delete ids in dry-run. After deletion, run the same scan and reject write if any dangling reference remains.

- [ ] **Step 11: Implement dry-run report**

`fold --dry-run` is not optional validation; it is the primary safety surface.

Text report must include:

- selected parent, anchor, target name, target GUID;
- selected nodes;
- boundary control links to move;
- boundary parameter edges to move;
- endpoint maps used;
- internal links/behaviors/params/operations to delete;
- interface action;
- rejection reasons.

JSON report must include the same information as arrays with object ids. Tests should assert the JSON report, not text formatting.

- [ ] **Step 12: Implement write path**

Write flow:

1. build before boundary;
2. run preflight validation;
3. transform anchor;
4. rewire control links;
5. rewire parameter edges;
6. update parent behavior arrays;
7. update or clean interface data according to `interface`;
8. delete internal-only closure;
9. rebuild behavior acceleration/indexes;
10. build after boundary;
11. compare before/after external boundary equivalence;
12. run object-layer reference validation;
13. save only after all checks pass.

If any step fails, return an error and leave the session either unmodified or explicitly documented as requiring reload. Prefer transactional mutation using cloned state or a rollback list if the existing session edit API supports it.

- [ ] **Step 13: Add CLI and patch apply support**

Add CLI parsing for:

- `--parent`;
- `--nodes`;
- `--anchor`;
- `--guid`;
- `--name`;
- `--version`;
- `--preserve-boundary`;
- repeated `--map-input old:new`;
- repeated `--map-output old:new`;
- repeated `--map-param old:new`;
- `--interface preserve|canonicalize|remove`;
- `--dry-run-report text|json`;
- `--dry-run`;
- `-o` / `--output`.

Rules:

- write requires `-o` unless `--dry-run`;
- graph/script folds require `--preserve-boundary`;
- ambiguous boundaries require explicit maps;
- patch apply must support `op: "fold"` through the same API and same validation;
- patch diff must show fold dry-run report and never write.

- [ ] **Step 14: Test Event_handler candidate and dry-run**

Find the actual Ballance `Event_handler` id using CLI, then run:

```powershell
nmo -f json behavior fold-candidates --parent <event-handler-id> C:\Users\kakut\Games\Ballance\base.cmo
nmo -f json behavior fold --parent <event-handler-id> --nodes <candidate-node-list> --anchor <anchor-id> --guid 42414C07-10000007 --name "Ballance Base Event Router" --preserve-boundary --dry-run C:\Users\kakut\Games\Ballance\base.cmo
```

Expected:

- fold-candidates lists the message router component;
- dry-run either succeeds with a complete 11-output mapping or rejects with explicit missing `--map-output` reasons;
- no file is written in dry-run.

Do not apply an `Event_handler` fold until the dry-run report accounts for every outgoing event path.

- [ ] **Step 15: Run and commit**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build -R "test_cli_behavior_rewrite|test_cli_patch_apply|test_cli_behavior_graph" --output-on-failure
ctest --test-dir build -R "test_cli_write_commands|test_real_file_roundtrip|test_object_layer_acceptance|test_runtime_copy_behavior_graph" --output-on-failure
```

Commit exact files:

```powershell
git add include/behavior/nmo_behavior_boundary.h include/behavior/nmo_behavior_rewrite.h src/behavior/behavior_boundary.c src/behavior/behavior_rewrite.c tools/commands/nmo_cmd_behavior_rewrite.c tools/commands/nmo_cmd_patch.c tests/integration/test_cli_behavior_rewrite.c
git commit -m "Add behavior subgraph fold rewrite"
```

## Task 6: Runtime Verification Loop

**Files:**

- No required repository source files.
- Optional local scripts outside commit scope.

- [ ] **Step 1: Validate generated CMO**

Run:

```powershell
nmo validate all C:\Users\kakut\Games\Ballance\base_ballance_bb_fixed.cmo
```

Expected: `VALID`.

- [ ] **Step 2: Compare original and rewritten behavior graph boundaries**

Run:

```powershell
nmo -f json behavior graph-boundary 363 C:\Users\kakut\Games\Ballance\base.cmo
nmo -f json behavior graph-boundary 363 C:\Users\kakut\Games\Ballance\base_ballance_bb_fixed.cmo
```

Expected:

- exact control boundary edge sets match after applying the fold's declared input/output remaps;
- exact parameter boundary edge sets match after applying the fold's declared parameter remaps;
- any edge removed, created, or retargeted is explained by the fold dry-run report;
- count equality alone is not sufficient evidence.

- [ ] **Step 3: Player smoke test**

Use:

```powershell
C:\Users\kakut\Games\Ballance\Bin\Player.exe --root-path C:\Users\kakut\Games\Ballance --cmo C:\Users\kakut\Games\Ballance\base_ballance_bb_fixed.cmo --log C:\Users\kakut\Games\Ballance\Bin\codex-ballance-rewrite.log --verbose --width 800 --height 600 --skip-opening
```

Expected:

- log includes `Loading game composition`;
- log includes `Game is playing`;
- log does not include `Chunk Read error`;
- if screen is black, inspect Ballance BB diagnostic lines before changing more CMO structure.

## Verification Commands

Use these after each milestone:

```powershell
cmake --build build --config Debug
ctest --test-dir build -R "test_cli_behavior_rewrite|test_cli_patch_apply|test_cli_behavior_graph" --output-on-failure
ctest --test-dir build -R "test_cli_write_commands|test_real_file_roundtrip|test_object_layer_acceptance|test_param_value" --output-on-failure
```

For release build parity:

```powershell
cmake --build build --config Release
ctest --test-dir build -R "test_cli_behavior_rewrite|test_cli_patch_apply|test_cli_behavior_graph" -C Release --output-on-failure
```

For Ballance:

```powershell
nmo patch apply examples/ballance-base-fold-v1.json
nmo validate all C:\Users\kakut\Games\Ballance\base_ballance_bb_fixed.cmo
```

## Commit Breakdown

Keep commits fine-grained:

1. `Add behavior graph boundary export`
2. `Add safe behavior BB replacement`
3. `Add behavior rewrite patch apply`
4. `Add Ballance base fold patch` only after fold exists and only if the user approves the path-bearing fixture
5. `Report behavior fold candidates`
6. `Add behavior subgraph fold rewrite`

## Risks

- CK2 naming for behavior link IO is easy to invert. Existing code notes that SDK naming is backwards: `in_io` is source and `out_io` is target. Preserve that convention.
- Replacing a graph/script parent by setting `block_guid` and `USEFUNCTION` changes both save semantics and execution semantics. It can drop, orphan, or bypass graph data. `replace-bb` must reject this; `fold` is the only planned path for graph/script to high-level BB conversion.
- `Event_handler` requires fold, not simple replace.
- Interface chunk edits can be a semantic failure, not only a visual failure. Validate runtime first; preserve interface data only when it is still consistent with the new behavior kind, otherwise canonicalize it deliberately and report the change.
- Patch apply must be strict. Silent unknown fields will hide mistakes in Ballance conversion patches.

## New Session Handoff Prompt

Paste this into a new session:

```text
We are in C:\Users\kakut\Works\Virtools\libnmo. Follow AGENTS.md: do not use git worktrees, work in current checkout, stage exact files/hunks only, no docs unless explicitly requested, imperative capitalized commit subjects with no conventional prefixes.

Goal: implement the plan in C:\Users\kakut\Works\Virtools\libnmo\docs\superpowers\plans\2026-04-20-behavior-graph-rewrite-cli.md. This upgrades nmo-cli from field-level behavior edits to safe behavior graph rewrite operations for Ballance base.cmo conversion.

Important prior context:
- CK2 checksum and CKParameter none-state marker bugs were already fixed and committed in libnmo:
  - 599bd08 Match CK2 file checksum
  - 60f98d5 Preserve CKParameter none state markers
- C:\Users\kakut\Games\Ballance\base_roundtrip_fixed.cmo validates and loads in Player without Chunk Read error.
- Current manual Ballance rewrite candidate is C:\Users\kakut\Games\Ballance\base_ballance_bb_fixed.cmo. It validates and loads, but the user reports black screen, so semantic graph rewrite/debugging is needed.
- Current manual replacement changed behavior ids 80, 149, 363, and 2562 to Ballance high-level BB GUIDs. That direct replacement is now considered unsafe because those ids are scripts/graphs; they must be handled by `fold`. Event_handler also requires fold.
- Use C:\Users\kakut\Games\Ballance, not C:\Users\kakut\Works\Ballance\Ballance.
- Player test path: C:\Users\kakut\Games\Ballance\Bin\Player.exe.

Start by reading the plan file. Implement Task 1 first using TDD:
1. add a failing integration test for `nmo -f json behavior graph-boundary 237 data/Ballance/base.cmo`;
2. verify the test fails;
3. add `include/behavior/nmo_behavior_boundary.h` and `src/behavior/behavior_boundary.c`;
4. add `tools/commands/nmo_cmd_behavior_rewrite.*` and dispatch `behavior graph-boundary`;
5. run targeted CTest;
6. commit exact files with subject `Add behavior graph boundary export`.

Do not continue manual CMO rewriting until graph-boundary, safe leaf replace-bb, and graph/script fold are implemented and tested. Do not use `replace-bb` for behavior ids `80`, `149`, `363`, or `2562`.
```
