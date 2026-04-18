# libnmo

**libnmo** is a C17 library for reading, writing, inspecting, and transforming
Virtools composition files (`.nmo`, `.cmo`, `.vmo`).  It implements a complete
serialization pipeline with a strict seven-layer architecture, symmetric
read/write operations, and full compatibility with Virtools file format versions
2 through 9.

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
  - [Type System](#type-system)
  - [Validation](#validation)
  - [Editing](#editing)
  - [Debugging and REPL](#debugging-and-repl)
- [API Documentation](#api-documentation)
  - [Context and Session](#context-and-session)
  - [Error Handling](#error-handling)
  - [Chunk API](#chunk-api)
  - [Type System](#type-system-api)
  - [Object System](#object-system)
  - [Behavior Layer](#behavior-layer)
  - [DSL Query Language](#dsl-query-language)
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
- 4D dispatch operation tree (operation x P1 type x P2 type x result type)
- 50+ built-in operations: arithmetic, logic, bitwise, trigonometric, vector
- String conversion: `nmo_type_to_string()` / `nmo_type_from_string()`
- Struct field reflection and introspection

### Object Layer

- 23 CK class schemas and 2 manager schemas with vtable dispatch
- Object repository with dual-index (`nmo_indexed_map_t` + name hash table)
- Object index providing O(1) lookup by class ID, name, or GUID (50-200x faster
  than linear scan)
- ID sanitizer handling the `0x800000` reference marker and negative external IDs
- Reference graph enumeration and runtime kernel

### Behavior System

- Recursive behavior graph traversal and analysis
- Typed parameter chain resolution
- Building Block registry with 601+ entries and 7638 JSON signatures
- Script walker for behavior graph introspection
- Behavior index for fast parameter and link queries

### Core Infrastructure

- Arena allocation with mark/rewind scope for session-local data
- Hash tables, hash sets, indexed maps, arrays, bit arrays, pools
- GUID generation and comparison
- Portable byte-order conversion and alignment utilities
- Thread-safe context with atomic reference counting
- Ownership tagging with debug-mode assertions
- Custom logging subsystem with severity levels

### CLI and Tooling

- `nmo` command-line tool with group/action interface (v3)
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
nmo behavior search --op-type "SetPosition" composition.nmo

# Type system
nmo type list composition.nmo
nmo type show CK3dEntity composition.nmo
nmo type class-tree composition.nmo

# Validation
nmo validate all composition.nmo
nmo validate references composition.nmo

# Debug
nmo debug load-phases composition.nmo
nmo repl start composition.nmo
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

    nmo_session_t *session = nmo_session_load(ctx, argv[1]);
    if (!session) {
        fprintf(stderr, "Failed to load: %s\n", argv[1]);
        nmo_context_release(ctx);
        return 1;
    }

    nmo_file_info_t info = nmo_session_get_file_info(session);
    printf("Object count: %u\n", info.object_count);

    nmo_session_destroy(session);
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
App -> Session -> Object -> Extension -> Type -> Format -> IO -> Core
```

| Layer     | Source          | Headers                   | Responsibility                                                           | Size   |
|-----------|-----------------|---------------------------|--------------------------------------------------------------------------|--------|
| Core      | `src/core/`     | `include/core/`           | Arena, allocator, GUID, hash tables, containers, error, math, logging   | 18 .c  |
| IO        | `src/io/`       | `include/io/`             | File, memory, mmap, compressed, checksummed, transactional IO           | 7 .c   |
| Format    | `src/format/`   | `include/format/`         | File header, chunk parser/writer, ID remap, image codec, obj parser     | 29 .c  |
| Type      | `src/type/`     | `include/type/`           | GUID-based type registry, operation dispatch, string conversion, reflection | 17 .c  |
| Extension | `src/extension/`| `include/extension/`      | Plugin registry, DLL loading, host ABI, diagnostics, Virtools loader    | 6 .c   |
| Object    | `src/object/`   | `include/object/`         | 23 CK class + 2 manager schemas, vtable dispatch, repository, index, shadow storage | 58 .c |
| Session   | `src/session/`  | `include/session/`        | Deserializer, builder, ID sanitizer, reference resolver, runtime kernel, delete | 11 .c  |
| Behavior  | `src/behavior/` | `include/behavior/`       | Behavior graph traversal, BB registry, parameter chains, script walker  | 5 .c   |
| DSL       | `src/dsl/`      | `include/dsl/`            | Lexer, AST, parser, evaluator, sequence engine                           | 6 .c   |
| App       | `src/app/`      | `include/app/`            | Context, session lifecycle, load/save pipeline, inspector, stats, JSON  | 25 .c  |

**Totals**: ~186 source files, 177 public headers, ~90 files across the library
layers (excluding tools, tests, and examples).

### Key Design Decisions

- **DWORD alignment**: all chunk positions and sizes are measured in 4-byte
  DWORDs, not bytes.  This matches the Virtools `CKStateChunk` binary layout.
- **Move semantics**: APIs taking `T**` transfer ownership; the callee sets
  `*ptr = NULL` on success.
- **ECS-style state**: combined state buffers with ancestor offsets for
  polymorphic access across the CK class hierarchy.
- **IntList verbatim**: `id_offsets`, `chunk_offsets`, and `manager_offsets`
  stored exactly as Virtools writes them for deterministic remap and iteration.
- **ID sanitization**: bit 31 (`0x80000000`) marks reference-only IDs.  Always
  call `nmo_id_sanitize()` before using an ID at runtime.
- **Vtable dispatch**: each object type provides both `serialize` and
  `deserialize` methods through a function pointer table; no legacy bridge
  macros remain.
- **No upward dependencies**: lower layers never include headers from higher
  layers, enforced at the architectural level.

---

## Building

### Prerequisites

| Requirement         | Minimum Version | Notes                                   |
|---------------------|-----------------|-----------------------------------------|
| CMake               | 3.15            | Build system                            |
| C compiler          | C17             | GCC, Clang, or MSVC                     |
| miniz or zlib       | -               | Bundled miniz included as git submodule |
| yyjson              | -               | Bundled; required for JSON export       |
| Threads             | POSIX or Win32  | For atomic reference counting           |

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

| Option                   | Default | Description                                  |
|--------------------------|---------|----------------------------------------------|
| `NMO_BUILD_TESTS`        | ON      | Build the test suite (enables ctest)         |
| `NMO_BUILD_TOOLS`        | ON      | Build the `nmo` CLI tool                     |
| `NMO_BUILD_EXAMPLES`     | OFF     | Build example programs                       |
| `NMO_BUILD_SHARED`       | OFF     | Build as shared library (SOVERSION 2)        |
| `NMO_MINGW_STATIC_RUNTIME` | OFF   | Link MinGW CLI executables with `-static`    |
| `NMO_ENABLE_SIMD`        | OFF     | Enable SIMD optimizations                    |
| `NMO_ENABLE_SANITIZERS`  | auto    | ASan/UBSan in Debug (non-Windows by default) |

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

The `nmo` tool uses a group/action command structure.  JSON output is available
for all commands via the `--json` flag.

### File Inspection

```
nmo file info <file>          # Summary: object count, version, size
nmo file header <file>        # Raw file header fields
```

### Object Discovery

```
nmo object list [--class <name>] <file>   # List objects, optionally filtered by class
nmo object show <id> <file>               # Detailed view of a single object
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
nmo behavior search [--op-type <type>] <file> # Search behaviors by operation
nmo behavior link <id> <file>               # Link structure
nmo behavior interface <id> <file>          # Interface data
```

### Type System

```
nmo type list <file>            # List all registered types
nmo type show <name> <file>     # Type details and fields
nmo type class-tree <file>      # CK class inheritance hierarchy
```

### Validation

```
nmo validate all <file>         # Run all validation checks
nmo validate references <file>  # Check reference integrity
```

### Editing

```
nmo object rename <id> --name "NewName" <file>
nmo object delete <id> <file>
nmo -f json object export --id <id> <file>              # Importable semantic snapshot
nmo object import -f json <snapshot.json> <file> -o <out> # Import object snapshot JSON
nmo texture extract <id> <file>
nmo convert <input> <output>    # Format conversion (.nmo/.cmo/.vmo)
```

`object export` JSON is a semantic snapshot protocol intended for round-trip
with `object import -f json`. Snapshot fields use `name`, `kind`, `type_guid`,
and `value`; arrays carry full `items` and `count` data. Legacy flat field maps
and preview-only `{name,value_str}` exports are not accepted by import.

### Debugging and REPL

```
nmo debug load-phases <file>    # Show 15-phase load pipeline timing
nmo repl start <file>           # Interactive REPL with tab completion
```

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

### Context and Session

| Function                     | Purpose                                        | Header                         |
|------------------------------|------------------------------------------------|--------------------------------|
| `nmo_context_create()`       | Create a library context (owns registries)     | `include/session/nmo_context.h`|
| `nmo_context_release()`      | Release context (atomic refcount)              | `include/session/nmo_context.h`|
| `nmo_context_retain()`       | Retain context (atomic refcount)               | `include/session/nmo_context.h`|
| `nmo_session_load()`         | Load a file into a new session                 | `include/app/nmo_load.h`       |
| `nmo_session_destroy()`      | Destroy session and release resources          | `include/session/nmo_session.h`|
| `nmo_session_save()`         | Save session to file (two-phase commit)        | `include/app/nmo_save.h`       |
| `nmo_session_get_file_info()`| Query file metadata                            | `include/session/nmo_session.h`|

### Error Handling

All fallible public APIs return `nmo_status_t`.  Success is `NMO_OK` (0).
Pointer-returning constructors return `NULL` on failure and set thread-local
last-error state.

```c
nmo_session_t *s = nmo_session_load(ctx, "missing.nmo");
if (!s) {
    printf("Error %d: %s (%s:%d)\n",
           nmo_last_error_code(),
           nmo_last_error_message(),
           nmo_last_error_file(),
           nmo_last_error_line());
}
```

Error codes include: `NMO_OK`, `NMO_ERR_NOMEM`, `NMO_ERR_FILE_NOT_FOUND`,
`NMO_ERR_TRUNCATED_CHUNK`, `NMO_ERR_INVALID_SIGNATURE`,
`NMO_ERR_UNSUPPORTED_VERSION`, `NMO_ERR_CHECKSUM_MISMATCH`, and 20 more.

| API                             | Header                 |
|---------------------------------|------------------------|
| `nmo_last_error_code()`         | `include/core/nmo_error.h` |
| `nmo_last_error_message()`      | `include/core/nmo_error.h` |
| `nmo_last_error_file()`         | `include/core/nmo_error.h` |
| `nmo_last_error_chain_copy()`   | `include/core/nmo_error.h` |
| `NMO_RETURN_ERROR()`            | `include/core/nmo_error.h` |
| `NMO_RETURN_IF_ERROR()`         | `include/core/nmo_error.h` |
| `NMO_ENSURE()`                  | `include/core/nmo_error.h` |

### Chunk API

All chunk positions and sizes are in DWORDs (4 bytes), not bytes.

| Function                      | Purpose                                  | Header                          |
|-------------------------------|------------------------------------------|---------------------------------|
| `nmo_chunk_create()`          | Create a new chunk                       | `include/format/nmo_chunk.h`    |
| `nmo_chunk_read_dword()`      | Read a DWORD from chunk                  | `include/format/nmo_chunk_parser.h` |
| `nmo_chunk_write_dword()`     | Write a DWORD to chunk                   | `include/format/nmo_chunk_writer.h` |
| `nmo_chunk_reserve_dword()`   | Reserve space for forward references     | `include/format/nmo_chunk_writer.h` |
| `nmo_chunk_patch_dword()`     | Patch a previously reserved DWORD        | `include/format/nmo_chunk_writer.h` |
| `nmo_chunk_compress()`        | Compress chunk data                      | `include/format/nmo_chunk.h`    |
| `nmo_chunk_decompress()`      | Decompress chunk data                    | `include/format/nmo_chunk.h`    |

### Type System API

| Function                                  | Purpose                              | Header                              |
|-------------------------------------------|--------------------------------------|-------------------------------------|
| `nmo_type_registry_lookup_by_guid()`      | O(1) type lookup by GUID             | `include/type/nmo_type_system.h`    |
| `nmo_type_registry_register_enum()`       | Register enum type                   | `include/type/nmo_dynamic_types.h`  |
| `nmo_type_registry_register_flags()`      | Register bitfield flags type         | `include/type/nmo_dynamic_types.h`  |
| `nmo_field_resolve_count()`               | Resolve reflected pointer-array count | `include/type/nmo_reflection.h`     |
| `nmo_operation_registry_dispatch()`       | Dispatch typed operation             | `include/type/nmo_operations.h`     |
| `nmo_type_to_string()`                    | Convert typed value to string        | `include/type/nmo_type_string.h`    |
| `nmo_type_from_string()`                  | Parse string to typed value          | `include/type/nmo_type_string.h`    |

Repeated fields stored as raw pointers must declare explicit count metadata
(`count_field_name` plus optional `count_multiplier`) through the reflection
schema. Consumers do not infer count fields from naming conventions.

### Object System

| Function                                | Purpose                                | Header                               |
|-----------------------------------------|----------------------------------------|--------------------------------------|
| `nmo_object_repository_add()`           | Add object (transfers ownership)       | `include/object/nmo_object_repository.h` |
| `nmo_object_repository_find_by_id()`    | Lookup by object ID                    | `include/object/nmo_object_repository.h` |
| `nmo_object_repository_take()`          | Take object (transfers ownership out)  | `include/object/nmo_object_repository.h` |
| `nmo_object_index_find_by_class()`      | O(1) lookup by class ID                | `include/object/nmo_object_index.h`  |
| `nmo_object_index_find_by_name()`       | O(1) lookup by name                    | `include/object/nmo_object_index.h`  |
| `nmo_object_index_find_by_guid()`       | O(1) lookup by GUID                    | `include/object/nmo_object_index.h`  |
| `nmo_object_import_json()`              | Import object export snapshot JSON     | `include/app/nmo_object_import.h`    |

### Behavior Layer

| Function                          | Purpose                                   | Header                              |
|-----------------------------------|-------------------------------------------|-------------------------------------|
| `nmo_behavior_graph_traverse()`   | Recursive graph traversal                  | `include/behavior/nmo_behavior_graph.h` |
| `nmo_behavior_index_create()`     | Build parameter and link index             | `include/behavior/nmo_behavior_index.h`  |
| `nmo_bb_registry_lookup()`        | Look up Building Block by GUID             | `include/behavior/nmo_bb_registry.h`     |
| `nmo_script_walker_walk()`        | Walk behavior script graph                 | `include/behavior/nmo_script_walker.h`   |

### DSL Query Language

The DSL subsystem provides a compile-once, evaluate-many query and mutation
language for inspecting and transforming loaded sessions.

```c
#include <dsl/nmo_dsl.h>

nmo_dsl_program_t *prog = nmo_dsl_compile("select CK3dEntity where name ~ 'Player*'");
nmo_dsl_result_t result = nmo_dsl_eval(prog, session);
// result contains matching objects
nmo_dsl_program_destroy(prog);
```

| Function                 | Header                   |
|--------------------------|--------------------------|
| `nmo_dsl_compile()`      | `include/dsl/nmo_dsl.h`  |
| `nmo_dsl_eval()`         | `include/dsl/nmo_dsl.h`  |
| `nmo_dsl_program_destroy()` | `include/dsl/nmo_dsl.h` |

### Extension System

| Function                          | Purpose                               | Header                                    |
|-----------------------------------|---------------------------------------|-------------------------------------------|
| `nmo_extension_register()`        | Register a static plugin              | `include/extension/nmo_extension_registry.h` |
| `nmo_extension_load()`            | Load a DLL/shared library plugin      | `include/extension/nmo_extension_loader.h`   |
| `nmo_extension_unregister()`      | Unregister plugin before unload       | `include/extension/nmo_extension_registry.h` |
| `nmo_extension_host_get_api()`    | Get host ABI for plugin integration   | `include/extension/nmo_extension_host.h`     |

---

## Testing

**Current test status: 138/138 passing**

### Test Categories

| Category       | Directory               | Count | Description                                           |
|----------------|--------------------------|-------|-------------------------------------------------------|
| Unit           | `tests/unit/`           | ~65   | Isolated function tests per module                    |
| Integration    | `tests/integration/`    | ~28   | Full workflow tests with real files                   |
| Round-trip     | `tests/round_trip/`     | ~1    | Load-save-reload DOM comparison framework             |
| Performance    | `tests/performance/`    | ~6    | Load/save/mmap benchmarks with CI enforcement         |
| Fuzz           | `tests/fuzz/`           | ~1    | Truncation injector over real .nmo files              |
| Stress         | `tests/stress/`         | ~1    | Memory pressure and boundary condition tests          |
| Batch          | `tests/batch_*.c`       | ~1    | Oracle-based batch interface testing                  |

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

### Supported CK Class Types (23)

| Class                | Schema File                          |
|----------------------|--------------------------------------|
| CKObject             | `ckobject_schemas.c`                 |
| CKBeObject           | `ckbeobject_schemas.c`               |
| CKSceneObject        | `cksceneobject_schemas.c`            |
| CKRenderObject       | `ckrenderobject_schemas.c`           |
| CKParameter          | `ckparameter_schemas.c`              |
| CKParameterIn        | `ckparameterin_schemas.c`            |
| CKParameterOut       | `ckparameterout_schemas.c`           |
| CKParameterLocal     | `ckparameterlocal_schemas.c`         |
| CKParameterOperation | `ckparameteroperation_schemas.c`     |
| CKGroup              | `ckgroup_schemas.c`                  |
| CKLevel              | `cklevel_schemas.c`                  |
| CKScene              | `ckscene_schemas.c`                  |
| CKBehavior           | `ckbehavior_schemas.c`               |
| CKBehaviorIO         | `ckbehaviorio_schemas.c`             |
| CKBehaviorLink       | `ckbehaviorlink_schemas.c`           |
| CK3dEntity           | `ck3dentity_schemas.c`               |
| CK3dObject           | `ck3dobject_schemas.c`               |
| CKMesh               | `ckmesh_schemas.c`                   |
| CKTexture            | `cktexture_schemas.c`                |
| CKMaterial           | `ckmaterial_schemas.c`               |
| CKLight              | `cklight_schemas.c`                  |
| CKCamera             | `ckcamera_schemas.c`                 |
| CKCharacter          | `ckcharacter_schemas.c`              |

Plus additional types: CKAnimation, CKCurve, CKPatchMesh, CKGrid, CKLayer,
CKPlace, CKSound, CKSynchro, CKSprite, CKSpriteText, CKSprite3D, CK2dEntity,
CKTargetCamera, CKTargetLight, CKKinematicChain, CKRenderContext, CKDataArray.

### Supported Managers (2)

| Manager                     | Schema File                                |
|-----------------------------|--------------------------------------------|
| CKInterfaceObjectManager    | `ckinterfaceobjectmanager_schemas.c`       |
| CKAttributeManager          | `ckattributemanager_schemas.c`             |

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
engineering and documentation efforts by the community.  The object schemas,
chunk format, and type system are derived from analysis of the original Virtools
Dev runtime and its CK2 class hierarchy.

---

## Support

- Issues: <https://github.com/doyaGu/libnmo/issues>
- Discussions: GitHub Discussions
