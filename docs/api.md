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
if (nmo_document_load_file(ctx, "missing.nmo", &doc) != NMO_OK) {
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
| `nmo_chunk_create()`          | Create a new chunk                       | `include/format/nmo_chunk.h`        |
| `nmo_chunk_read_dword()`      | Read a DWORD from chunk                  | `include/format/nmo_chunk_parser.h` |
| `nmo_chunk_write_dword()`     | Write a DWORD to chunk                   | `include/format/nmo_chunk_writer.h` |
| `nmo_chunk_reserve_dword()`   | Reserve space for forward references     | `include/format/nmo_chunk_writer.h` |
| `nmo_chunk_patch_dword()`     | Patch a previously reserved DWORD        | `include/format/nmo_chunk_writer.h` |
| `nmo_chunk_compress()`        | Compress chunk data                      | `include/format/nmo_chunk.h`        |
| `nmo_chunk_decompress()`      | Decompress chunk data                    | `include/format/nmo_chunk.h`        |
| `nmo_chunk_index_build()`     | Build chunk index from document          | `include/chunk/nmo_chunk_index.h`   |
| `nmo_chunk_inspect()`         | Inspect chunk structure                  | `include/chunk/nmo_chunk_inspect.h` |

## Type System API

| Function                                  | Purpose                              | Header                              |
|-------------------------------------------|--------------------------------------|-------------------------------------|
| `nmo_type_registry_lookup_by_guid()`      | O(1) type lookup by GUID             | `include/type/nmo_type_system.h`    |
| `nmo_type_registry_register_enum()`       | Register enum type                   | `include/type/nmo_dynamic_types.h`  |
| `nmo_type_registry_register_flags()`      | Register bitfield flags type         | `include/type/nmo_dynamic_types.h`  |
| `nmo_field_resolve_count()`               | Resolve reflected pointer-array count | `include/type/nmo_reflection.h`    |
| `nmo_operation_registry_dispatch()`       | Dispatch typed operation             | `include/type/nmo_operations.h`     |
| `nmo_type_to_string()`                    | Convert typed value to string        | `include/type/nmo_type_string.h`    |
| `nmo_type_from_string()`                  | Parse string to typed value          | `include/type/nmo_type_string.h`    |

Repeated fields stored as raw pointers must declare explicit count metadata
(`count_field_name` plus optional `count_multiplier`) through the reflection
schema. Consumers do not infer count fields from naming conventions.

## Object System

| Function                                | Purpose                                | Header                                       |
|-----------------------------------------|----------------------------------------|----------------------------------------------|
| `nmo_object_repository_add()`           | Add object (transfers ownership)       | `include/object/nmo_object_repository.h`     |
| `nmo_object_repository_find_by_id()`    | Lookup by object ID                    | `include/object/nmo_object_repository.h`     |
| `nmo_object_repository_take()`          | Take object (transfers ownership out)  | `include/object/nmo_object_repository.h`     |
| `nmo_object_index_find_by_class()`      | O(1) lookup by class ID                | `include/object/nmo_object_index.h`          |
| `nmo_object_index_find_by_name()`       | O(1) lookup by name                    | `include/object/nmo_object_index.h`          |
| `nmo_object_index_find_by_guid()`       | O(1) lookup by GUID                    | `include/object/nmo_object_index.h`          |
| `nmo_object_edit_rename()`              | Rename an object in a workspace        | `include/object/nmo_object_edit.h`           |
| `nmo_object_edit_delete()`              | Delete object with cascade             | `include/object/nmo_object_edit.h`           |
| `nmo_object_edit_create()`              | Create a new object                    | `include/object/nmo_object_edit.h`           |

## Behavior and Script Layer

| Function                            | Purpose                                    | Header                                     |
|-------------------------------------|--------------------------------------------|--------------------------------------------|
| `nmo_behavior_graph_traverse()`     | Recursive graph traversal                  | `include/behavior/nmo_behavior_analyze.h`  |
| `nmo_behavior_index_create()`       | Build parameter and link index             | `include/behavior/nmo_behavior_query.h`    |
| `nmo_bb_registry_lookup()`          | Look up Building Block by GUID             | `include/behavior/nmo_behavior_registry.h` |
| `nmo_script_walker_walk()`          | Walk behavior script graph                 | `include/behavior/nmo_behavior_analyze.h`  |
| `nmo_edit_plan_create()`            | Create a new behavior edit plan            | `include/behavior/nmo_edit_plan.h`         |
| `nmo_edit_plan_to_json()`           | Serialize edit plan to JSON                | `include/behavior/nmo_edit_plan_json.h`    |
| `nmo_behavior_execute()`            | Execute behavior edit plan against file    | `include/behavior/nmo_behavior_execute.h`  |
| `nmo_script_edit_node_add()`        | Add node to script graph                   | `include/behavior/nmo_script_edit.h`       |
| `nmo_script_edit_io_add()`          | Add IO to script                           | `include/behavior/nmo_script_edit.h`       |
| `nmo_probe_analyzer_run()`          | Run probe analysis on behavior graph       | `include/behavior/nmo_probe_analyzer.h`    |

## Project and Authoring

| Function                              | Purpose                                  | Header                                        |
|---------------------------------------|------------------------------------------|-----------------------------------------------|
| `nmo_project_plan_create()`           | Create a new project plan                | `include/project/nmo_project_plan.h`          |
| `nmo_project_executor_run()`          | Execute a project plan                   | `include/project/nmo_project_executor.h`      |
| `nmo_project_validator_validate()`    | Validate project plan                    | `include/project/nmo_project_validator.h`     |
| `nmo_project_manifest_to_json()`      | Serialize project manifest to JSON       | `include/project/nmo_project_manifest_json.h` |
| `nmo_scene_authoring_create()`        | Create scene via authoring API           | `include/project/nmo_scene_authoring.h`       |
| `nmo_script_authoring_create()`       | Create script via authoring API          | `include/project/nmo_script_authoring.h`      |

## Lua Scripting

```c
#include <lua/nmo_lua_module.h>
#include <lua/nmo_lua_runtime.h>
#include <lua/nmo_lua_bindings.h>

nmo_lua_runtime_t *rt = nmo_lua_runtime_create();
// Register all nmo bindings into the Lua state
nmo_lua_bindings_open(rt, ctx, document);
// Run a Lua script
nmo_lua_runtime_exec_file(rt, "automation.lua");
nmo_lua_runtime_destroy(rt);
```

| Function                      | Header                           |
|-------------------------------|----------------------------------|
| `nmo_lua_runtime_create()`    | `include/lua/nmo_lua_runtime.h`  |
| `nmo_lua_runtime_destroy()`   | `include/lua/nmo_lua_runtime.h`  |
| `nmo_lua_bindings_open()`     | `include/lua/nmo_lua_bindings.h` |
| `nmo_lua_handles_register()`  | `include/lua/nmo_lua_handles.h`  |

## Extension System

| Function                          | Purpose                               | Header                                       |
|-----------------------------------|---------------------------------------|----------------------------------------------|
| `nmo_extension_register()`        | Register a static plugin              | `include/extension/nmo_extension_registry.h` |
| `nmo_extension_load()`            | Load a DLL/shared library plugin      | `include/extension/nmo_extension_loader.h`   |
| `nmo_extension_unregister()`      | Unregister plugin before unload       | `include/extension/nmo_extension_registry.h` |
| `nmo_extension_host_get_api()`    | Get host ABI for plugin integration   | `include/extension/nmo_extension_host.h`     |
