# libnmo

**libnmo** is a C17 library for reading, writing, inspecting, and transforming
Virtools composition files (`.nmo`, `.cmo`, `.vmo`). It implements a complete
serialization pipeline with a layered architecture, symmetric read/write
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
- [Reference Documentation](#reference-documentation)
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
- String conversion: `nmo_type_value_to_string()` / `nmo_type_value_from_string()`
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

Library `nmo_project` (`nmo_project.h`).

- Project plan: declarative scene, object, script, and asset authoring
- Scene authoring and scene lifecycle management
- Script authoring with behavior graph construction
- Project executor with plan replay and validation
- Project manifest JSON serialization

### Lua Scripting

Library `nmo_lua` (`nmo_lua.h`).

- Embedded Lua 5.5 runtime with full standard libraries
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

- `nmo` command-line tool with a group/action interface and 23 groups: file,
  chunk, object, behavior, patch, parameter, script, resource, texture, data,
  scene, entity, material, mesh, animation, type, validate, convert, diff,
  extension, completion, debug, and repl
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
nmo script node add --parent 10 --bb-guid <guid> composition.nmo -o edited.nmo
nmo script io add --behavior 10 --kind input --name In composition.nmo -o edited.nmo

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
#include <stdio.h>

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
    if (nmo_document_load_file(ctx, argv[1], NULL, &document) != NMO_OK) {
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

Dependency direction is downward: a source file may include headers from its own layer
and from layers below it. From lowest to highest:

```
core -> io -> format -> type -> extension -> object -> session -> runtime
     -> document -> chunk -> behavior -> export -> lua -> project
```

`tests/layering_audit.cmake` (run as `test_layering_audit`) enforces this order. It is not
yet fully clean: 43 upward includes exist today and are listed in
`tests/layering_allowlist.txt` as debt. A new upward include fails the test, and so does an
allowlist entry that is no longer needed.

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
| Lua       | `src/lua/`       | `include/lua/`        | Lua 5.5 runtime, module system, bindings for all layers, fold-map parser (library `nmo_lua`) |
| Project   | `src/project/`   | `include/project/`    | Project plan, asset/scene/script authoring, executor, manifest, validator (library `nmo_project`) |

### Libraries

The Lua and Project layers are separate libraries built on top of the core; the core
library does not depend on either of them and contains no Lua code.

| Library       | CMake target   | pkg-config       | Umbrella header | Contents                                  |
|---------------|----------------|------------------|-----------------|-------------------------------------------|
| `libnmo`      | `nmo::nmo`     | `libnmo`         | `nmo.h`         | Everything below the Lua and Project layers |
| `libnmo_lua`  | `nmo::lua`     | `libnmo-lua`     | `nmo_lua.h`     | Lua runtime, bindings, `nmo_behavior_execution_lua_runtime()` |
| `libnmo_project` | `nmo::project` | `libnmo-project` | `nmo_project.h` | Project plans, authoring, executor, manifest reader |

`nmo.h` does not include the Lua or Project headers; include `nmo_lua.h` or `nmo_project.h`
(or the individual `lua/*.h`, `project/*.h` headers) and link the matching library. Static
builds produce one archive per library; with `NMO_BUILD_SHARED=ON` all three are compiled into
the single shared `nmo` library and `nmo::lua` / `nmo::project` are interface targets on it.

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
- **Downward dependencies**: lower layers should not include headers from
  higher layers. `test_layering_audit` blocks new violations; the existing ones
  are tracked in `tests/layering_allowlist.txt`.

---

## Building

### Prerequisites

| Requirement           | Minimum Version | Notes                                      |
|-----------------------|-----------------|--------------------------------------------|
| CMake                 | 3.15            | Build system                               |
| C compiler            | C17             | GCC, Clang, or MSVC                        |
| miniz or zlib         | -               | Bundled miniz included as git submodule    |
| yyjson                | -               | Bundled; required for JSON export          |
| Lua                   | 5.5.1           | Fetched from lua.org at configure time; see below |
| isocline              | -               | Optional REPL line editing (`NMO_USE_ISOCLINE`, default ON). Uses `deps/isocline/` if present, otherwise a pinned upstream archive is fetched at configure time |
| stb                   | -               | Bundled; image decode                      |
| Threads               | POSIX or Win32  | For atomic reference counting              |

Lua is downloaded and SHA-256 verified by CMake `FetchContent` during the
first configure. For offline builds, unpack the official tarball and either
point CMake at it with `-DNMO_LUA_SOURCE_DIR=/path/to/lua-5.5.1` or place it at
`deps/lua/` (so that `deps/lua/src/lua.h` exists). The CMake build description
for Lua lives in `deps/lua-cmake/`.

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

### Release Packages

Each GitHub release has prebuilt packages for Linux (x64), macOS (universal),
Windows MSVC and Windows MinGW. A package holds the `nmo` CLI, the static
library with its headers, CMake and pkg-config files, runtime data, and shell
completions. To build one locally:

```sh
python tools/scripts/package_release.py --platform linux-x64
```

This writes `dist/libnmo-<version>-<platform>.tar.gz` (`.zip` for
`windows-*` platforms) and its `.sha256`.

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
| `NMO_USE_ISOCLINE`         | ON      | Line editing in the REPL (see isocline above)|
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

This installs the `nmo` CLI, the static library and public headers, runtime
data, and shell completions. Consumers can use either CMake or pkg-config:

```cmake
find_package(libnmo CONFIG REQUIRED)
target_link_libraries(app PRIVATE nmo::nmo)                  # core only
target_link_libraries(app PRIVATE nmo::lua nmo::project nmo::nmo)   # with the optional components
```

```sh
cc app.c $(pkg-config --cflags --libs libnmo)
cc app.c $(pkg-config --cflags libnmo-lua libnmo-project) $(pkg-config --static --libs libnmo-lua libnmo-project)
```

---

## Reference Documentation

- [CLI Reference](docs/cli-reference.md): every `nmo` command group, the
  REPL, and shell completions.
- [API Documentation](docs/api.md): C entry points by area.
- [API Tiers](docs/api-tiers.md): which public APIs are stable and what
  each tier promises.

---

## Testing

Tests are organized under `tests/` into `unit`, `integration`, `round_trip`,
`performance`, `fuzz`, `stress`, and `package_consumer` subdirectories, plus
three CMake source audits (`source_encoding_audit`, `schema_io_result_audit`,
`layering_audit`). Run the full suite with `ctest` (see
[Running Tests](#running-tests) above).

Many tests read real Virtools files from `data/`. That corpus is not in the
repository (`data/*` is git-ignored except the JSON tables). Tests that need a
missing file are reported as skipped, not failed.

### Test Framework

Custom lightweight framework in `tests/test_framework.h`:

```c
#include "test_framework.h"
#include "nmo.h"

TEST(chunk, create) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk, create);
TEST_MAIN_END()
```

Macros: `TEST()`, `REGISTER_TEST()`, `ASSERT_EQ()`, `ASSERT_NE()`, `ASSERT_TRUE()`,
`ASSERT_FALSE()`, `ASSERT_NULL()`, `ASSERT_NOT_NULL()`, `ASSERT_STR_EQ()`,
`ASSERT_MEM_EQ()`, and the comparison and range asserts in `tests/test_framework.h`.
`TEST_SKIP()`, `TEST_REQUIRE_FILE()` and `TEST_REQUIRE_FIXTURE()` mark a test as
skipped (exit code 77) when a required file is missing.

### CI Pipeline

The GitHub Actions CI (`.github/workflows/ci.yml`) runs on every push to `main`,
on every pull request, and on `v*` tags:

- **`sanitizers`**: Ubuntu, clang, Debug build with ASan/UBSan, then `ctest`
  without the `performance` label (those tests assert on wall-clock ratios and
  are not meaningful on shared runners)
- **`package`**: one Release build per platform (Linux x64 on Ubuntu 22.04,
  macOS universal, Windows MSVC, Windows MinGW). `tools/scripts/package_release.py`
  runs the tests, installs the package, checks the install from the outside, and
  archives it; the archives are kept as workflow artifacts
- **`publish-release`**: on a `vX.Y.Z` tag, publishes the packages as a GitHub
  release

---

## Supported File Model

### Supported CK Class Types

CKObject, CKBeObject, CKSceneObject, CKRenderObject, CKParameter,
CKParameterIn, CKParameterOut, CKParameterLocal, CKParameterOperation,
CKGroup, CKLevel, CKScene, CKBehavior, CKBehaviorIO, CKBehaviorLink,
CK3dEntity, CK3dObject, CKMesh, CKTexture, CKMaterial, CKLight, CKCamera,
CKCharacter, CKAnimation, CKKeyedAnimation, CKObjectAnimation, CKCurve, CKPatchMesh, CKGrid, CKLayer, CKPlace,
CKSound, CKSynchro, CKSprite, CKSpriteText, CKSprite3D, CK2dEntity,
CKTargetCamera, CKTargetLight, CKKinematicChain, CKRenderContext, CKDataArray.

### Supported Managers

CKInterfaceObjectManager, CKAttributeManager, CKMessageManager.

---

## Contributing

- **Style**: 4-space indent, 100-char line limit, K&R braces
- **Naming**: `nmo_module_function()`, `nmo_type_name_t`, `NMO_ENUM_VALUE`, `NMO_MACRO`
- **Layer rule**: no new upward dependencies (checked by `test_layering_audit`)
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
