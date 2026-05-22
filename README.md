# libnmo

**libnmo** is a C17 library for reading, writing, inspecting, and transforming
Virtools composition files (`.nmo`, `.cmo`, `.vmo`). It implements a complete
serialization pipeline with a strict layered architecture, symmetric read/write
operations, and full compatibility with Virtools file format versions 2 through 9.

---

## Table of Contents

- [Features](#features)
- [Quick Start](#quick-start)
  - [CLI](#cli-quick-start)
  - [C API](#c-api-quick-start)
- [Architecture](#architecture)
  - [Layer Stack](#layer-stack)
  - [Key Design Decisions](#key-design-decisions)
- [Building](#building)
  - [Prerequisites](#prerequisites)
  - [Recommended Build (Ninja)](#recommended-build-ninja)
  - [Windows (MSVC)](#windows-msvc)
  - [Build Options](#build-options)
- [CLI Reference](#cli-reference)
  - [File Inspection](#file-inspection)
  - [Object Discovery](#object-discovery)
  - [Chunk Inspection](#chunk-inspection)
  - [Behavior Analysis](#behavior-analysis)
  - [Script Editing](#script-editing)
  - [Scene and Entity](#scene-and-entity)
  - [Mesh, Texture, Material, Animation](#mesh-texture-material-animation)
  - [Type System](#type-system)
  - [Validation](#validation)
  - [Editing](#editing)
  - [Diff and Patch](#diff-and-patch)
  - [Debugging and REPL](#debugging-and-repl)
- [API Documentation](#api-documentation)
  - [Context, Document, and Workspace](#context-document-and-workspace)
  - [Error Handling](#error-handling)
  - [Chunk API](#chunk-api)
  - [Type System](#type-system-api)
  - [Object System](#object-system)
  - [Behavior and Script Layer](#behavior-and-script-layer)
  - [Project and Authoring](#project-and-authoring)
  - [Lua Scripting](#lua-scripting)
  - [Extension System](#extension-system)
- [Testing](#testing)
- [Supported File Model](#supported-file-model)
- [Contributing](#contributing)
- [License](#license)
- [Acknowledgments](#acknowledgments)

---

## Features

### Serialization

- Symmetric read/write operations driven by unified vtable-based schemas
- Two-phase commit save pipeline (Layout/Serialize, then Pack/Commit)
- Optional zlib compression and CRC-32 integrity on save
- Shadow storage for lossless round-trips of unknown chunk tail data
- Reserve-and-patch pattern for forward-reference writes
- Memory-mapped zero-copy IO alongside buffered file IO
- Transactional write with platform-specific atomic commit (POSIX `fsync`,
  Windows `FlushFileBuffers`)

### Type System

- GUID-first type identification with O(1) hash lookups
- Type registry supporting primitives, enums, flags, structs, and manager types
- Type inheritance via parent GUID chaining
- 4D dispatch operation tree (operation × P1 type × P2 type × result type)
- Built-in operations: arithmetic, logic, bitwise, trigonometric, vector
- String conversion: `nmo_type_to_string()` / `nmo_type_from_string()`
- Struct field reflection and introspection

### Object Layer

- CK class schemas and manager schemas with vtable dispatch
- Object repository with dual-index (`nmo_indexed_map_t` + name hash table)
- Object index providing O(1) lookup by class ID, name, or GUID
- ID sanitizer handling the `0x800000` reference marker and negative external IDs
- Reference graph enumeration and runtime kernel

### Behavior and Script System

- Recursive behavior graph traversal and analysis
- Typed parameter chain resolution
- Building Block registry with JSON signatures
- Script walker for behavior graph introspection
- Behavior index for fast parameter and link queries
- Script editing: node add/remove, IO add/remove/link, behavior graph mutations
- Edit plan API with JSON serialization for deterministic replay
- Behavior execution pipeline with dry-run support
- Probe analyzer for graph diagnostic inspection

### Project and Authoring

- Project plan: declarative scene, object, script, and asset authoring
- Scene authoring and scene lifecycle management
- Script authoring with behavior graph construction
- Project executor with plan replay and validation
- Project manifest JSON serialization

### Lua Scripting

- Embedded Lua 5.4 runtime with full standard libraries
- Bindings covering: context, document, session, object, type, behavior, format,
  plan, workspace, and runtime layers
- Fold-map parser for declarative Lua-driven automation
- Lua-based batch edit reports

### Core Infrastructure

- Arena allocation with mark/rewind scope for session-local data
- Hash tables, hash sets, indexed maps, arrays, bit arrays, pools
- GUID generation and comparison
- Portable byte-order conversion and alignment utilities
- Thread-safe context with atomic reference counting
- Ownership tagging with debug-mode assertions
- Custom logging subsystem with severity levels

### CLI and Tooling

- `nmo` command-line tool with group/action interface covering file, chunk,
  object, behavior, script, parameter, scene, entity, mesh, texture, material,
  animation, type, validate, convert, diff, extension, debug, and repl
- Interactive REPL with tab completion and session persistence
- JSON output with stable envelope (`schema_version`, `tool`, `command`)
- Shell completions for Bash, Fish, Zsh, and PowerShell
- DOT graph export for object hierarchies and behavior graphs
- Semantic object diff and comparison
- Performance statistics and benchmarking

### Cross-Platform

- Windows, Linux, macOS
- CI pipeline with three-platform builds on every pull request
- Performance baseline gate in CI

---

## Quick Start

### CLI Quick Start

```sh
# File inspection
nmo file info composition.nmo
nmo file header composition.nmo

# Object discovery
nmo object list --class CK3dEntity composition.nmo
nmo object show 42 composition.nmo
nmo object find --name "Player*" composition.nmo

# Importable object snapshots
nmo -f json object export --id 42 composition.nmo > object-42.json
nmo object import -f json object-42.json composition.nmo -o edited.nmo

# Chunk inspection
nmo chunk list composition.nmo
nmo chunk show 7 composition.nmo

# Behavior analysis
nmo behavior graph 10 composition.nmo
nmo behavior show 10 composition.nmo
nmo behavior find --op-type "SetPosition" composition.nmo
nmo behavior interface show 10 composition.nmo

# Script editing
nmo script graph 10 composition.nmo
nmo script run automation.lua composition.nmo -o edited.nmo
nmo script node --add 10 composition.nmo -o edited.nmo
nmo script io --add 10 composition.nmo -o edited.nmo

# Type system
nmo type list
nmo type show CK3dEntity

# Validation
nmo validate all composition.nmo
nmo validate references composition.nmo

# Debug
nmo debug load-phases composition.nmo
nmo repl start composition.nmo
```

Inside the REPL, CLI-shaped grouped commands read the loaded in-memory session
instead of reopening the original file. Save explicitly with `save <path>`:

```text
object list --class CK3dEntity
object show 42
behavior interface --name "Main Script"
cli -f json object list --top 5
object rename 42 PlayerStart
save edited.nmo
```

### C API Quick Start

```c
#include <nmo.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.nmo>\n", argv[0]);
        return 1;
    }

    nmo_context_t *ctx = nmo_context_create(NULL);
    if (!ctx) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }

    nmo_document_t *document = NULL;
    if (nmo_document_load_file(ctx, argv[1], &document) != NMO_OK) {
        fprintf(stderr, "Failed to load: %s\n", argv[1]);
        nmo_context_release(ctx);
        return 1;
    }

    nmo_workspace_t *workspace = NULL;
    if (nmo_workspace_create(ctx, document, &workspace) != NMO_OK) {
        fprintf(stderr, "Failed to create workspace\n");
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return 1;
    }

    nmo_object_repository_t *repo = nmo_document_get_repository(document);
    printf("Repository: %p\n", (void *)repo);

    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_context_release(ctx);
    return 0;
}
```

Compile:

```sh
cc -o demo demo.c -lnmo
```

---

## Architecture

### Layer Stack

Dependency direction is strict: each layer may only import from layers below it.

```
Project/Lua -> Behavior -> Object -> Extension -> Type -> Format -> IO -> Core
                    \-> Document -> Session -> ...
                    \-> Runtime (Context, Workspace)
```

| Layer     | Source           | Headers               | Responsibility                                                                   |
|-----------|------------------|-----------------------|----------------------------------------------------------------------------------|
| Core      | `src/core/`      | `include/core/`       | Arena, allocator, GUID, hash tables, containers, error, math, logging           |
| IO        | `src/io/`        | `include/io/`         | File, memory, mmap, compressed, checksummed, transactional IO                   |
| Format    | `src/format/`    | `include/format/`     | File header, chunk parser/writer, ID remap, image codec, obj parser             |
| Type      | `src/type/`      | `include/type/`       | GUID-based type registry, operation dispatch, string conversion, reflection      |
| Extension | `src/extension/` | `include/extension/`  | Plugin registry, DLL loading, host ABI, diagnostics, Virtools loader            |
| Object    | `src/object/`    | `include/object/`     | CK class and manager schemas, vtable dispatch, repository, index, shadow storage |
| Session   | `src/session/`   | `include/session/`    | Deserializer, builder, ID sanitizer, reference resolver, runtime kernel, delete |
| Runtime   | `src/runtime/`   | `include/runtime/`    | Context, workspace, workspace edit, session utilities                            |
| Document  | `src/document/`  | `include/document/`   | Document load/save, stats, performance stats, comparison, file state            |
| Chunk     | `src/chunk/`     | `include/chunk/`      | Chunk index and chunk inspection utilities                                       |
| Behavior  | `src/behavior/`  | `include/behavior/`   | Behavior graph traversal, BB registry, parameter chains, script walker, edit plan, behavior execute |
| Export    | `src/export/`    | `include/export/`     | DOT graph, JSON utilities, text export, ANSI, hex dump                          |
| Lua       | `src/lua/`       | `include/lua/`        | Lua 5.4 runtime, module system, bindings for all layers, fold-map parser        |
| Project   | `src/project/`   | `include/project/`    | Project plan, asset/scene/script authoring, executor, manifest, validator       |

### Key Design Decisions

- **DWORD alignment**: all chunk positions and sizes are measured in 4-byte
  DWORDs, not bytes. This matches the Virtools `CKStateChunk` binary layout.
- **Move semantics**: APIs taking `T**` transfer ownership; the callee sets
  `*ptr = NULL` on success.
- **ECS-style state**: combined state buffers with ancestor offsets for
  polymorphic access across the CK class hierarchy.
- **IntList verbatim**: `id_offsets`, `chunk_offsets`, and `manager_offsets`
  stored exactly as Virtools writes them for deterministic remap and iteration.
- **ID sanitization**: bit 31 (`0x80000000`) marks reference-only IDs. Always
  call `nmo_id_sanitize()` before using an ID at runtime.
- **Vtable dispatch**: each object type provides both `serialize` and
  `deserialize` methods through a function pointer table; no legacy bridge
  macros remain.
- **Document/Workspace split**: `nmo_document_t` owns the parsed, immutable
  representation; `nmo_workspace_t` provides mutation and runtime services on
  top of a document. Read-only workflows never need a workspace.
- **No upward dependencies**: lower layers never include headers from higher
  layers, enforced at the architectural level.

---

## Building

### Prerequisites

| Requirement           | Minimum Version | Notes                                      |
|-----------------------|-----------------|--------------------------------------------|
| CMake                 | 3.15            | Build system                               |
| C compiler            | C17             | GCC, Clang, or MSVC                        |
| miniz or zlib         | -               | Bundled miniz included as git submodule    |
| yyjson                | -               | Bundled; required for JSON export          |
| Lua 5.4               | -               | Bundled in `deps/lua/`                     |
| isocline              | -               | Bundled in `deps/isocline/`; REPL readline |
| stb                   | -               | Bundled; image decode                      |
| Threads               | POSIX or Win32  | For atomic reference counting              |

### Recommended Build (Ninja)

```sh
cmake -B cmake-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
```

### Release Build

```sh
cmake -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release
```

### Windows MinGW Release Package

```powershell
pwsh tools/scripts/package_release.ps1 -Version 1.0.0 -BuildDir build_package_release_static -DistDir dist
```

The release script builds with `NMO_MINGW_STATIC_RUNTIME=ON`, runs the test
suite, stages the install tree, verifies shell completions, checks that
`nmo.exe` does not import `libwinpthread`, runs an external static-link smoke
test, and writes `dist/libnmo-<version>-windows-mingw-x64.zip`.

### Windows (MSVC)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Build Options

| Option                     | Default | Description                                  |
|----------------------------|---------|----------------------------------------------|
| `NMO_BUILD_TESTS`          | ON      | Build the test suite (enables ctest)         |
| `NMO_BUILD_TOOLS`          | ON      | Build the `nmo` CLI tool                     |
| `NMO_BUILD_EXAMPLES`       | OFF     | Build example programs                       |
| `NMO_BUILD_SHARED`         | OFF     | Build as shared library (SOVERSION 2)        |
| `NMO_MINGW_STATIC_RUNTIME` | OFF     | Link MinGW CLI executables with `-static`    |
| `NMO_ENABLE_SIMD`          | OFF     | Enable SIMD optimizations                    |
| `NMO_ENABLE_SANITIZERS`    | auto    | ASan/UBSan in Debug (non-Windows by default) |

### Running Tests

```sh
ctest --test-dir cmake-build-debug -j4 --output-on-failure
```

Run individual test binaries:

```sh
./cmake-build-debug/tests/unit/test_arena
./cmake-build-debug/tests/unit/test_chunk
./cmake-build-debug/tests/integration/test_data_roundtrip
./cmake-build-debug/tests/performance/test_load_save_mmap_baseline
```

### Installation

```sh
cmake --install cmake-build-release --prefix /usr/local
```

This installs the library, public headers, and pkg-config files.

---

## CLI Reference

The `nmo` tool uses a group/action command structure. JSON output is available
through the global format option, for example `-f json` or `-f json-pretty`.

### File Inspection

```
nmo file info <file>          # Summary: object count, version, size
nmo file header <file>        # Raw file header fields
```

### Object Discovery

```
nmo object list [--class <name>] <file>    # List objects, optionally filtered by class
nmo object show <id> <file>                # Detailed view of a single object
nmo object find [--name <pattern>] <file>  # Search objects by name glob
```

### Chunk Inspection

```
nmo chunk list <file>          # List all chunks in the file
nmo chunk show <id> <file>     # Inspect a single chunk in detail
```

### Behavior Analysis

```
nmo behavior graph <id> <file>              # Behavior graph structure
nmo behavior show <id> <file>               # Behavior details and parameters
nmo behavior find [--name <pattern> | --op-type <type>] <file>  # Search behaviors
nmo behavior trace --from <io> <id> <file>  # Trace execution paths
nmo behavior interface show <id> <file>     # Interface layout data
```

### Script Editing

```
nmo script graph <id> <file>                # Export script edit graph
nmo script run <script.lua> <file> -o <out> # Run Lua automation script
nmo script node <id> <file> -o <out>        # Script node editing
nmo script io <id> <file> -o <out>          # Script IO editing
```

### Scene and Entity

```
nmo scene list <file>          # List scenes and levels
nmo scene show <id> <file>     # Scene details

nmo entity list <file>         # List 3D entities
nmo entity show <id> <file>    # Entity details and transform
```

### Mesh, Texture, Material, Animation

```
nmo mesh list <file>
nmo mesh show <id> <file>
nmo mesh export --id <id> --out-dir meshes <file>

nmo texture list <file>
nmo texture extract --id <id> --out-dir textures <file>

nmo material list <file>
nmo material show <id> <file>

nmo animation list <file>
nmo animation export --id <id> --out-dir anims <file>
```

### Type System

```
nmo type list                   # List all registered types
nmo type show <name>            # Type details and fields
nmo type class-tree             # CK class inheritance hierarchy
```

### Validation

```
nmo validate all <file>         # Run all validation checks
nmo validate references <file>  # Check reference integrity
```

### Editing

```
nmo object rename <id> "NewName" <file> -o <out>
nmo object delete <id> <file> -o <out>
nmo -f json object export --id <id> <file>               # Importable semantic snapshot
nmo object import -f json <snapshot.json> <file> -o <out>  # Import object snapshot JSON
nmo texture extract --id <id> --out-dir textures <file>
nmo convert copy <file> -o <out>  # Round-trip copy / format conversion
```

`object export` JSON is a semantic snapshot protocol intended for round-trip
with `object import -f json`. Snapshot fields use `name`, `kind`, `type_guid`,
and `value`; arrays carry full `items` and `count` data. Legacy flat field maps
and preview-only `{name,value_str}` exports are not accepted by import.

### Diff and Patch

```
nmo diff objects <file-a> <file-b>    # Compare two files at the object level
nmo patch apply <patch.json> <file> -o <out>  # Apply a patch file
```

### Debugging and REPL

```
nmo debug load-phases <file>    # Show 15-phase load pipeline timing
nmo repl start <file>           # Interactive REPL with tab completion
```

The REPL has two layers:

- Legacy browsing shortcuts such as `list`, `show`, `dump`, `param`, `refs`,
  `trace`, `query`, `eval`, `stats`, `meta`, `verify`, and `export` remain
  optimized for interactive exploration.
- CLI-shaped grouped commands such as `object show`, `object graph`,
  `parameter dump`, `behavior interface`, `resource extract`, `mesh export`,
  and `validate all` reuse the same command registry and family command cores
  as the CLI, but operate on the currently loaded in-memory session.

REPL grouped read commands do not accept an implicit current-file operand. Use
the session already loaded in the REPL; `diff` is the exception, where the REPL
session is the left side and the explicit file operand is the comparison side.
Commands that write external artifacts, such as `resource extract`,
`texture extract`, `mesh export`, `animation export`, and `debug export`, still
require their explicit output path or directory and do not mark the session
dirty.

Use `cli ...` inside the REPL when global CLI options are needed:

```text
cli -f json object list --top 5
cli -o debug.json debug export
cli --strict validate all
```

Supported REPL grouped mutations are limited to `object rename`,
`object delete`, `object create`, `object copy`, and `parameter set`. They mutate
the loaded session and must be persisted with `save <path>`. Other file-writing,
import, replace, convert, and fix-style CLI actions are rejected in the REPL
grouped command path.

### Shell Completions

Completions are provided in `completions/` for Bash, Fish, Zsh, and PowerShell.
They can also be emitted by the CLI:

```sh
nmo completion bash
nmo completion fish
nmo completion zsh
nmo completion powershell
```

---

## API Documentation

### Context, Document, and Workspace

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

### Error Handling

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

### Chunk API

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

### Type System API

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

### Object System

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

### Behavior and Script Layer

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

### Project and Authoring

| Function                              | Purpose                                  | Header                                        |
|---------------------------------------|------------------------------------------|-----------------------------------------------|
| `nmo_project_plan_create()`           | Create a new project plan                | `include/project/nmo_project_plan.h`          |
| `nmo_project_executor_run()`          | Execute a project plan                   | `include/project/nmo_project_executor.h`      |
| `nmo_project_validator_validate()`    | Validate project plan                    | `include/project/nmo_project_validator.h`     |
| `nmo_project_manifest_to_json()`      | Serialize project manifest to JSON       | `include/project/nmo_project_manifest_json.h` |
| `nmo_scene_authoring_create()`        | Create scene via authoring API           | `include/project/nmo_scene_authoring.h`       |
| `nmo_script_authoring_create()`       | Create script via authoring API          | `include/project/nmo_script_authoring.h`      |

### Lua Scripting

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

### Extension System

| Function                          | Purpose                               | Header                                       |
|-----------------------------------|---------------------------------------|----------------------------------------------|
| `nmo_extension_register()`        | Register a static plugin              | `include/extension/nmo_extension_registry.h` |
| `nmo_extension_load()`            | Load a DLL/shared library plugin      | `include/extension/nmo_extension_loader.h`   |
| `nmo_extension_unregister()`      | Unregister plugin before unload       | `include/extension/nmo_extension_registry.h` |
| `nmo_extension_host_get_api()`    | Get host ABI for plugin integration   | `include/extension/nmo_extension_host.h`     |

---

## Testing

Tests are organized under `tests/` into unit, integration, round-trip,
performance, fuzz, stress, and batch subdirectories. Run the full suite with
`ctest` (see [Running Tests](#running-tests) above).

### Test Framework

Custom lightweight framework in `tests/test_framework.h`:

```c
#include "test_framework.h"

TEST(chunk, read_dword) {
    nmo_chunk_t *chunk = nmo_chunk_create(NULL, 4, 1);
    ASSERT_NE(NULL, chunk);
    nmo_chunk_destroy(chunk);
}
```

Macros: `TEST()`, `ASSERT_EQ()`, `ASSERT_NE()`, `ASSERT_TRUE()`, `ASSERT_FALSE()`,
`ASSERT_STREQ()`, `ASSERT_MEMEQ()`.

### CI Pipeline

The GitHub Actions CI (`.github/workflows/ci.yml`) runs on every push to `main`
and on every pull request:

- **Platforms**: Ubuntu Latest, macOS Latest, Windows Latest
- **Build**: Release configuration, parallel compilation
- **Test suite**: full `ctest` run with `--output-on-failure`
- **Round-trip gate**: dedicated round-trip regression test
- **Performance baseline**: enforces maximum load/save/mmap timings via
  `NMO_BENCH_ENFORCE`

---

## Supported File Model

### Supported CK Class Types

CKObject, CKBeObject, CKSceneObject, CKRenderObject, CKParameter,
CKParameterIn, CKParameterOut, CKParameterLocal, CKParameterOperation,
CKGroup, CKLevel, CKScene, CKBehavior, CKBehaviorIO, CKBehaviorLink,
CK3dEntity, CK3dObject, CKMesh, CKTexture, CKMaterial, CKLight, CKCamera,
CKCharacter, CKAnimation, CKCurve, CKPatchMesh, CKGrid, CKLayer, CKPlace,
CKSound, CKSynchro, CKSprite, CKSpriteText, CKSprite3D, CK2dEntity,
CKTargetCamera, CKTargetLight, CKKinematicChain, CKRenderContext, CKDataArray.

### Supported Managers

CKInterfaceObjectManager, CKAttributeManager.

---

## Contributing

- **Style**: 4-space indent, 100-char line limit, K&R braces
- **Naming**: `nmo_module_function()`, `nmo_type_name_t`, `NMO_ENUM_VALUE`, `NMO_MACRO`
- **Layer rule**: no upward dependencies; lower layers never import higher ones
- **Object types**: both `serialize` and `deserialize` vtable methods required
- **API comments**: Doxygen `/** @brief ... @param ... @return ... */` on public APIs
- **Testing**: all tests must pass before submitting

Checklist:

- [ ] All tests pass
- [ ] No upward layer dependencies introduced
- [ ] All error paths handled (`nmo_status_t` checked)
- [ ] cppcheck / clang-tidy clean

---

## License

MIT License.

```
Copyright (c) 2025 libnmo contributors
```

---

## Acknowledgments

This project implements the Virtools file format based on extensive reverse
engineering and documentation efforts by the community. The object schemas,
chunk format, and type system are derived from analysis of the original Virtools
Dev runtime and its CK2 class hierarchy.

---

## Support

- Issues: <https://github.com/doyaGu/libnmo/issues>
- Discussions: GitHub Discussions
