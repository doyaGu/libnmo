# API Documentation

Entry points by area. See [API Tiers](api-tiers.md) for which of these
are stable for long-lived callers and bindings.

## Context, Document, and Workspace

The primary entry points are `nmo_context_t`, `nmo_document_t`, and
`nmo_workspace_t`. A context owns the global registries (type, extension,
manager). A document holds the parsed file representation. A workspace provides
mutation and runtime services on top of a document.

| Function                          | Purpose                                         | Header                              |
|-----------------------------------|-------------------------------------------------|-------------------------------------|
| `nmo_context_create()`            | Create a library context (owns registries)      | `include/runtime/nmo_context.h`     |
| `nmo_context_release()`           | Release context (atomic refcount)               | `include/runtime/nmo_context.h`     |
| `nmo_context_retain()`            | Retain context (atomic refcount)                | `include/runtime/nmo_context.h`     |
| `nmo_document_create()`           | Create an empty document                        | `include/document/nmo_document.h`   |
| `nmo_document_destroy()`          | Destroy document and release resources          | `include/document/nmo_document.h`   |
| `nmo_document_load_file()`        | Load a file into a new document                 | `include/document/nmo_document_load.h` |
| `nmo_document_save_file()`        | Save document to file (two-phase commit)        | `include/document/nmo_document_save.h` |
| `nmo_document_get_repository()`   | Access the object repository                    | `include/document/nmo_document.h`   |
| `nmo_workspace_create()`          | Create workspace over a document                | `include/runtime/nmo_workspace.h`   |
| `nmo_workspace_destroy()`         | Destroy workspace                               | `include/runtime/nmo_workspace.h`   |
| `nmo_workspace_get_document()`    | Get the underlying document                     | `include/runtime/nmo_workspace.h`   |

## Error Handling

All fallible public APIs return `nmo_status_t`. Success is `NMO_OK` (0).
Pointer-returning constructors return `NULL` on failure and set thread-local
last-error state.

```c
nmo_document_t *doc = NULL;
if (nmo_document_load_file(ctx, "missing.nmo", NULL, &doc) != NMO_OK) {
    printf("Error %d: %s (%s:%d)\n",
           nmo_last_error_code(),
           nmo_last_error_message(),
           nmo_last_error_file(),
           nmo_last_error_line());
}
```

Error codes are defined in `include/core/nmo_error.h`: `NMO_OK`,
`NMO_ERR_NOMEM`, `NMO_ERR_FILE_NOT_FOUND`, `NMO_ERR_TRUNCATED_CHUNK`,
`NMO_ERR_INVALID_SIGNATURE`, `NMO_ERR_UNSUPPORTED_VERSION`,
`NMO_ERR_CHECKSUM_MISMATCH`, and others.

| API                             | Header                     |
|---------------------------------|----------------------------|
| `nmo_last_error_code()`         | `include/core/nmo_error.h` |
| `nmo_last_error_message()`      | `include/core/nmo_error.h` |
| `nmo_last_error_file()`         | `include/core/nmo_error.h` |
| `nmo_last_error_chain_copy()`   | `include/core/nmo_error.h` |
| `NMO_RETURN_ERROR()`            | `include/core/nmo_error.h` |
| `NMO_RETURN_IF_ERROR()`         | `include/core/nmo_error.h` |
| `NMO_ENSURE()`                  | `include/core/nmo_error.h` |

## Chunk API

All chunk positions and sizes are in DWORDs (4 bytes), not bytes.

| Function                      | Purpose                                  | Header                              |
|-------------------------------|------------------------------------------|-------------------------------------|
| `nmo_chunk_create()`          | Create a new chunk (arena-backed)        | `include/format/nmo_chunk.h`        |
| `nmo_chunk_read_dword()`      | Read a DWORD from chunk                  | `include/format/nmo_chunk_api.h`    |
| `nmo_chunk_write_dword()`     | Write a DWORD to chunk                   | `include/format/nmo_chunk_api.h`    |
| `nmo_chunk_reserve_dwords()`  | Reserve a DWORD span for forward references; also `_reserve_u32()`, `_reserve_u64()` | `include/format/nmo_chunk_api.h` |
| `nmo_chunk_patch_dwords()`    | Patch a previously reserved span; also `_patch_u32()`, `_patch_u64()` | `include/format/nmo_chunk_api.h` |
| `nmo_chunk_compress()`        | Compress chunk data                      | `include/format/nmo_chunk_api.h`    |
| `nmo_chunk_decompress()`      | Decompress chunk data                    | `include/format/nmo_chunk_api.h`    |
| `nmo_chunk_index_collect_entries()` | Collect chunk index entries from a session | `include/chunk/nmo_chunk_index.h` |
| `nmo_chunk_index_build_map()` / `nmo_chunk_index_lookup()` | Build and query a chunk-pointer index | `include/chunk/nmo_chunk_index.h` |
| `nmo_inspector_dump_chunk()`  | Dump chunk structure                     | `include/chunk/nmo_chunk_inspect.h` |
| `nmo_inspector_validate_chunk()` | Validate chunk structure              | `include/chunk/nmo_chunk_inspect.h` |

Reserve returns a `nmo_chunk_patch_token_t`; pass it to the matching `patch` call once the value is known.

## Type System API

| Function                                  | Purpose                              | Header                              |
|-------------------------------------------|--------------------------------------|-------------------------------------|
| `nmo_type_query_find_by_guid()`           | O(1) type lookup by GUID             | `include/type/nmo_type_query.h`     |
| `nmo_type_registry_register_enum()`       | Register enum type                   | `include/type/nmo_dynamic_types.h`  |
| `nmo_type_registry_register_flags()`      | Register bitfield flags type         | `include/type/nmo_dynamic_types.h`  |
| `nmo_field_resolve_count()`               | Resolve reflected pointer-array count | `include/type/nmo_reflection.h`    |
| `nmo_operation_registry_execute()`        | Execute a typed operation            | `include/type/nmo_operation_system.h` |
| `nmo_type_value_to_string()`              | Convert typed value to string        | `include/type/nmo_type_string.h`    |
| `nmo_type_value_from_string()`            | Parse string to typed value          | `include/type/nmo_type_string.h`    |

Repeated fields stored as raw pointers must declare explicit count metadata
(`count_field_name` plus optional `count_multiplier`) through the reflection
schema. Consumers do not infer count fields from naming conventions.

## Object System

| Function                                | Purpose                                | Header                                       |
|-----------------------------------------|----------------------------------------|----------------------------------------------|
| `nmo_object_repository_add()`           | Add object (transfers ownership)       | `include/object/nmo_object_repository.h`     |
| `nmo_object_repository_find_by_id()`    | Lookup by object ID                    | `include/object/nmo_object_repository.h`     |
| `nmo_object_repository_take()`          | Take object (transfers ownership out)  | `include/object/nmo_object_repository.h`     |
| `nmo_object_repository_find_by_class()` | All objects of a class                 | `include/object/nmo_object_repository.h`     |
| `nmo_object_index_get_by_class()`       | O(1) lookup by class ID                | `include/object/nmo_object_index.h`          |
| `nmo_object_index_find_by_name()`       | O(1) lookup by name                    | `include/object/nmo_object_index.h`          |
| `nmo_object_index_find_by_guid()`       | O(1) lookup by GUID                    | `include/object/nmo_object_index.h`          |
| `nmo_object_edit_rename()`              | Rename an object (inside a workspace edit) | `include/object/nmo_object_edit.h`       |
| `nmo_object_edit_create()`              | Create a new object (inside a workspace edit) | `include/object/nmo_object_edit.h`    |
| `nmo_runtime_preview_delete()`          | Preview an object deletion                 | `include/session/nmo_runtime_kernel.h`   |
| `nmo_runtime_execute_delete()`          | Execute an object deletion request     | `include/session/nmo_runtime_kernel.h`       |

The `nmo_object_edit_*` functions take a `nmo_workspace_edit_t`. Open one with
`nmo_workspace_edit_begin()` and finish it with `nmo_workspace_edit_commit()`
(`include/runtime/nmo_workspace.h`); `nmo_workspace_edit_rollback()` discards it.

## Behavior and Script Layer

| Function                            | Purpose                                    | Header                                     |
|-------------------------------------|--------------------------------------------|--------------------------------------------|
| `nmo_behavior_graph_build()`        | Build a behavior graph (free with `nmo_behavior_graph_free()`) | `include/behavior/nmo_behavior_analyze.h` |
| `nmo_behavior_walk()`               | Depth-first walk of a behavior tree        | `include/behavior/nmo_behavior_analyze.h`  |
| `nmo_behavior_index_create()`       | Build parameter and link index             | `include/behavior/nmo_behavior_analyze.h`  |
| `nmo_behavior_analyze_dump_text()`  | Text dump of a full behavior tree          | `include/behavior/nmo_behavior_analyze.h`  |
| `nmo_behavior_query_collect_scripts()` | Discover all scripts in a document      | `include/behavior/nmo_behavior_query.h`    |
| `nmo_behavior_param_value_to_string()` | Decode a parameter value to text        | `include/behavior/nmo_behavior_view.h`     |
| `nmo_behavior_registry_find()`      | Look up Building Block prototype by GUID   | `include/behavior/nmo_behavior_registry.h` |
| `nmo_edit_plan_create()`            | Create a new behavior edit plan            | `include/behavior/nmo_edit_plan.h`         |
| `nmo_edit_plan_json_write()` / `nmo_edit_plan_json_read()` | Serialize / parse an edit plan as JSON | `include/behavior/nmo_edit_plan_json.h` |
| `nmo_edit_plan_manifest_json_write()` | Write a plan with its input and output paths | `include/behavior/nmo_edit_plan_json.h` |
| `nmo_behavior_execute()`            | Load a file, run an action, validate, save (or dry-run) | `include/behavior/nmo_behavior_execute.h` |
| `nmo_script_edit_begin()`           | Start a script edit transaction (finish with `nmo_script_edit_commit()`) | `include/behavior/nmo_script_edit.h` |
| `nmo_script_edit_add_node()`        | Add a node to a behavior                   | `include/behavior/nmo_script_edit.h`       |
| `nmo_script_edit_add_io()`          | Add an IO to a behavior                    | `include/behavior/nmo_script_edit.h`       |
| `nmo_probe_analyze_selector()`      | Analyze a behavior graph and select probe candidates | `include/behavior/nmo_probe_analyzer.h` |

`nmo_behavior_execute()` is an edit pipeline, not a behavior runtime: it does not step CKBehavior
graphs. Its action callback receives a `nmo_behavior_execution_t` that exposes the workspace and
the script edit transaction. Optional components attach their own per-execution state with
`nmo_behavior_execution_set_attachment()` / `_get_attachment()`; the Lua component uses this for
the runtime returned by `nmo_behavior_execution_lua_runtime()`.

## Project and Authoring

These functions live in the `nmo_project` library (CMake `nmo::project`, pkg-config
`libnmo-project`); include `nmo_project.h` and link it in addition to `nmo`.

| Function                              | Purpose                                  | Header                                        |
|---------------------------------------|------------------------------------------|-----------------------------------------------|
| `nmo_project_plan_create()`           | Create a new project plan                | `include/project/nmo_project_plan.h`          |
| `nmo_project_validate_plan()`         | Validate project plan                    | `include/project/nmo_project_validator.h`     |
| `nmo_project_executor_execute_dry_run()` | Execute a plan without writing a file | `include/project/nmo_project_executor.h`      |
| `nmo_project_executor_execute_to_file()` | Execute a plan and save the result    | `include/project/nmo_project_executor.h`      |
| `nmo_project_manifest_json_read()`    | Parse a project manifest JSON into a plan | `include/project/nmo_project_manifest_json.h` |
| `nmo_project_plan_add_scene()`        | Add a scene to a plan                    | `include/project/nmo_scene_authoring.h`       |
| `nmo_project_plan_add_object_script()` | Add a script to an object in a plan     | `include/project/nmo_script_authoring.h`      |

## Lua Scripting

A standalone runtime, with the binding groups you choose registered into it:

These functions live in the `nmo_lua` library (CMake `nmo::lua`, pkg-config `libnmo-lua`);
include `nmo_lua.h` and link it in addition to `nmo`.

```c
#include <nmo_lua.h>

nmo_lua_runtime_t *rt = nmo_lua_runtime_create();
nmo_lua_register_core_bindings(rt);
nmo_lua_register_type_bindings(rt);
// ... and any other nmo_lua_register_*_bindings() groups you need
nmo_lua_runtime_execute_string(rt, script_text);   // script_text: Lua source
nmo_lua_runtime_destroy(rt);
```

To run a script against a document with load, validate and save handled for you (this is what
`nmo script run` does), use `nmo_behavior_execute()` and fetch the runtime inside the action
callback with `nmo_behavior_execution_lua_runtime()` (declared in `lua/nmo_lua_behavior.h`; the
runtime is created on first use).

| Function                                  | Header                           |
|-------------------------------------------|----------------------------------|
| `nmo_lua_runtime_create()`                | `include/lua/nmo_lua_runtime.h`  |
| `nmo_lua_runtime_destroy()`               | `include/lua/nmo_lua_runtime.h`  |
| `nmo_lua_runtime_execute_string()`        | `include/lua/nmo_lua_runtime.h`  |
| `nmo_lua_runtime_register_module()`       | `include/lua/nmo_lua_runtime.h`  |
| `nmo_behavior_execution_lua_runtime()`    | `include/lua/nmo_lua_behavior.h` |
| `nmo_lua_register_core_bindings()` and the other `nmo_lua_register_*_bindings()` (context, document, workspace, session, runtime, object, type, behavior, plan, format, platform) | `include/lua/nmo_lua_bindings.h` |
| `nmo_lua_handle_scope_create()` / `_retain()` / `_release()` / `_invalidate()` | `include/lua/nmo_lua_handles.h`  |

## Extension System

| Function                          | Purpose                               | Header                                       |
|-----------------------------------|---------------------------------------|----------------------------------------------|
| `nmo_extension_registry_register_static()` | Register static plugins          | `include/extension/nmo_extension_registry.h` |
| `nmo_extension_registry_load_library()` | Load a DLL/shared library plugin    | `include/extension/nmo_extension_registry.h` |
| `nmo_extension_registry_unload_by_guid()` | Unload a plugin by GUID           | `include/extension/nmo_extension_registry.h` |
| `nmo_extension_host_get_api()`    | Get host ABI for plugin integration   | `include/extension/nmo_extension_host.h`     |
