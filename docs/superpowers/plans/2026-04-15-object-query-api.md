# Library-Level Object Query API Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote object filtering/query behavior from CLI-only helpers into a reusable library API, then make CLI/REPL object iteration call the library API.

**Architecture:** Add a focused object-layer query module that depends on object repository, object metadata, optional type registry inheritance checks, and an optional caller-provided predicate. Wildcard and lightweight regex matching move from CLI core to this module. DSL stays outside the object layer by using the predicate hook.

**Tech Stack:** C17, `nmo_object_repository_t`, `nmo_object_t`, `nmo_type_registry_t`, `nmo_arena_t`, existing unit test framework, CMake/Ninja.

---

## Scope

In scope:

- New public header `include/object/nmo_object_query.h`.
- New implementation `src/object/object_query.c`.
- Unit test `tests/unit/test_object_query.c`.
- `tools/nmo_cmd_core.c` migration so `nmo_core_iter_objects()` delegates matching to the library API.
- Compatibility bridge for `nmo_object_repository_iter_filtered()`.

Out of scope:

- CLI write-command runner refactor.
- Runtime/ref graph API collapse.
- Public header tier split.
- CLI UX changes.
- Making the object layer depend directly on DSL.

---

## Public API

Add `include/object/nmo_object_query.h`:

```c
typedef enum nmo_object_query_name_mode {
    NMO_OBJECT_QUERY_NAME_NONE = 0,
    NMO_OBJECT_QUERY_NAME_EXACT,
    NMO_OBJECT_QUERY_NAME_SUBSTRING,
    NMO_OBJECT_QUERY_NAME_WILDCARD,
    NMO_OBJECT_QUERY_NAME_REGEX
} nmo_object_query_name_mode_t;

typedef bool (*nmo_object_query_predicate_fn)(
    const nmo_object_t *object,
    void *user_data);

typedef struct nmo_object_query {
    nmo_object_id_t object_id;
    nmo_class_id_t class_id;
    bool include_derived_classes;
    const char *name;
    nmo_object_query_name_mode_t name_mode;
    bool name_case_insensitive;
    nmo_object_query_predicate_fn predicate;
    void *predicate_user_data;
} nmo_object_query_t;

typedef struct nmo_object_query_result {
    size_t total;
    size_t matched;
    size_t visited;
    bool stopped_early;
} nmo_object_query_result_t;

typedef bool (*nmo_object_query_visitor_fn)(
    size_t match_index,
    nmo_object_t *object,
    void *user_data);

NMO_API nmo_status_t nmo_object_query_matches(
    const nmo_object_t *object,
    const nmo_object_query_t *query,
    const nmo_type_registry_t *registry,
    bool *out_matches);

NMO_API nmo_status_t nmo_object_query_iterate(
    nmo_object_repository_t *repository,
    const nmo_object_query_t *query,
    const nmo_type_registry_t *registry,
    nmo_object_query_visitor_fn visitor,
    void *user_data,
    nmo_object_query_result_t *out_result);

NMO_API nmo_status_t nmo_object_query_collect(
    nmo_object_repository_t *repository,
    const nmo_object_query_t *query,
    const nmo_type_registry_t *registry,
    nmo_arena_t *arena,
    nmo_object_t ***out_objects,
    size_t *out_count,
    nmo_object_query_result_t *out_result);
```

Rules:

- `query == NULL` matches all objects.
- `registry == NULL` is valid unless `include_derived_classes` is true with nonzero `class_id`.
- Built-in filters run before `predicate`.
- `collect` allocates only the result pointer array from the provided arena.

---

## Task 1: Add Failing Tests

**Files:**

- Create `tests/unit/test_object_query.c`
- Modify `tests/unit/CMakeLists.txt`

Steps:

- [ ] Write tests that create an in-memory repository with `CKObject`, `CK3dEntity`, `CKCamera`, and `CKMesh` objects.
- [ ] Cover object-id filtering: only the selected ID matches.
- [ ] Cover exact class filtering: `CK3dEntity` matches only the entity object.
- [ ] Cover derived class filtering: `CK3dEntity` matches both `CK3dEntity` and `CKCamera`.
- [ ] Cover name matching modes: substring, wildcard, regex, and case-insensitive behavior.
- [ ] Cover predicate filtering by excluding `CKMesh`.
- [ ] Cover visitor stop behavior: visitor returning false sets `stopped_early` and `visited == 1`.
- [ ] Register `add_unit_test(test_object_query)` in `tests/unit/CMakeLists.txt`.
- [ ] Run `cmake --build build_check --target test_object_query`.
- [ ] Expected RED: build fails because `object/nmo_object_query.h` does not exist.
- [ ] Commit tests only: `git commit -m "Add object query API tests"`.

---

## Task 2: Add API Skeleton

**Files:**

- Create `include/object/nmo_object_query.h`
- Create `src/object/object_query.c`
- Modify `CMakeLists.txt`
- Modify `include/nmo.h`

Steps:

- [ ] Add the public API exactly as listed above.
- [ ] Add `src/object/object_query.c` with compiling stubs returning `NMO_ERR_INVALID_ARGUMENT` for invalid args and `NMO_ERR_INVALID_STATE` for unimplemented iterate/collect behavior.
- [ ] Add `src/object/object_query.c` next to `src/object/object_repository.c` in `NMO_SOURCES`.
- [ ] Include `object/nmo_object_query.h` from `include/nmo.h`.
- [ ] Run `cmake --build build_check --target test_object_query`.
- [ ] Run `ctest --test-dir build_check --output-on-failure -R "^test_object_query$"`.
- [ ] Expected RED: build succeeds, tests fail on behavior assertions.
- [ ] Commit skeleton: `git commit -m "Add object query API skeleton"`.

---

## Task 3: Implement Library Query Semantics

**Files:**

- Modify `src/object/object_query.c`

Steps:

- [ ] Add local string helpers for exact match and substring match with optional ASCII case folding.
- [ ] Move wildcard matching semantics from repository filtering into `object_query.c`; preserve `*` and `?`.
- [ ] Move lightweight regex semantics from `tools/nmo_cmd_core.c` into `object_query.c`; preserve `.`, `*`, `[]`, `[^]`, `^`, `$`, and backslash escapes.
- [ ] Implement `nmo_object_query_matches()` in this order: object ID, class, name, predicate.
- [ ] Implement derived-class checks with `nmo_type_registry_is_class_derived_from()`.
- [ ] Return `NMO_ERR_INVALID_ARGUMENT` when derived matching is requested without a registry.
- [ ] Implement `nmo_object_query_iterate()` over repository objects using the same repository traversal pattern as `nmo_object_repository_iter_filtered()`.
- [ ] Implement result counters: `total`, `matched`, `visited`, `stopped_early`.
- [ ] Implement `nmo_object_query_collect()` as two-pass count-then-fill using arena allocation.
- [ ] Run `cmake --build build_check --target test_object_query`.
- [ ] Run `ctest --test-dir build_check --output-on-failure -R "^test_object_query$"`.
- [ ] Run `ctest --test-dir build_check --output-on-failure -R "test_object_repository|test_object_index|test_public_api_smoke"`.
- [ ] Commit implementation: `git commit -m "Add library object query API"`.

---

## Task 4: Migrate CLI Core Adapter

**Files:**

- Modify `tools/nmo_cmd_core.h`
- Modify `tools/nmo_cmd_core.c`

Steps:

- [ ] Keep `nmo_core_object_filter_t` unchanged so object command call sites do not move in this task.
- [ ] Add a file-local bridge from `nmo_core_object_filter_t` to `nmo_object_query_t`.
- [ ] Preserve current CLI semantics:
  - wildcard is case-insensitive
  - substring is case-sensitive
  - regex uses `regex_icase`
  - DSL predicate runs after built-in filters
- [ ] Add a file-local DSL predicate bridge using `nmo_core_dsl_setup_ctx()`, `nmo_dsl_eval_expr()`, and `nmo_core_dsl_is_truthy()`.
- [ ] Rewrite `nmo_core_iter_objects()` to call `nmo_object_query_iterate()`.
- [ ] Bridge `nmo_object_query_result_t` back to `nmo_core_iter_result_t`.
- [ ] Remove duplicated regex helper code from `tools/nmo_cmd_core.c`.
- [ ] Remove `nmo_core_regex_match()` declaration from `tools/nmo_cmd_core.h` if `rg "nmo_core_regex_match" tools src include tests` shows no remaining external call sites.
- [ ] Run `cmake --build build_check`.
- [ ] Run `ctest --test-dir build_check --output-on-failure -R "test_object_query|test_batch_rename|test_public_api_smoke"`.
- [ ] If `Test-Path data/Ballance/Camera.nmo` is true, run `ctest --test-dir build_check --output-on-failure -R "test_cli_commands"`.
- [ ] If the fixture is absent, record the skipped CLI fixture test in the final response.
- [ ] Commit migration: `git commit -m "Route CLI object iteration through object query API"`.

---

## Task 5: Bridge Legacy Repository Filter API

**Files:**

- Modify `include/object/nmo_object_repository.h`
- Modify `src/object/object_repository.c`
- Test `tests/unit/test_object_repository.c`

Steps:

- [ ] Keep `nmo_object_filter_t` and `nmo_object_repository_iter_filtered()` for source compatibility.
- [ ] Update the doc comment to mark it as a legacy narrow filter and recommend `nmo_object_query_t` for new code.
- [ ] Reimplement `nmo_object_repository_iter_filtered()` by building a `nmo_object_query_t`.
- [ ] Preserve old behavior:
  - class ID is exact only
  - `name_pattern` maps to wildcard
  - `name_substring` maps to substring
  - `case_insensitive` applies to wildcard and substring
- [ ] Run `ctest --test-dir build_check --output-on-failure -R "test_object_repository|test_object_query"`.
- [ ] Commit compatibility bridge: `git commit -m "Bridge repository filters to object query API"`.

---

## Task 6: Final Verification

Steps:

- [ ] Run `cmake --build build_check`.
- [ ] Run `ctest --test-dir build_check --output-on-failure -R "test_object_query|test_object_repository|test_object_index|test_public_api_smoke|test_batch_rename"`.
- [ ] Run `Test-Path data/Ballance/Camera.nmo`.
- [ ] If fixture exists, run `ctest --test-dir build_check --output-on-failure -R "test_cli_commands"`.
- [ ] Run `git diff --check`.
- [ ] Run `rg -n "nmo_core_regex_match|regex_match_here|regex_class_match" tools src include tests`.
- [ ] Expected: regex implementation remains only in `src/object/object_query.c` unless tests reference it.
- [ ] Run `rg -n "nmo_object_repository_iter_filtered" tools src include tests`.
- [ ] Expected: old API remains only as compatibility entry point and tests, not in new CLI object command logic.
- [ ] Commit final polish only if Task 6 required fixes.

---

## Success Criteria

- Library users can query objects without depending on `tools/`.
- `nmo_core_iter_objects()` delegates matching to the object query API.
- Existing CLI list/find behavior remains unchanged.
- Old repository filter API remains source-compatible.
- Focused tests pass.
- Fixture-backed CLI tests are either passing or explicitly reported as skipped because fixtures are absent.

## Rollback Plan

If CLI migration causes broad regressions:

- Revert only the CLI migration commit.
- Keep the library query API if `test_object_query`, `test_object_repository`, and `test_public_api_smoke` still pass.
- Re-plan command migration command-by-command instead of through `nmo_core_iter_objects()`.
