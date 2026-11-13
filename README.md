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
- Optional zlib compression and the Adler-32 file checksum on save
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
- A platform layer (`src/platform/platform.h`) through which the library and the CLI reach the
  operating system: aligned memory, clocks, UTF-8 paths, file mapping, atomic file replacement,
  directories, terminals and shared libraries, implemented once for POSIX and once for Windows

### Rendering

- `nmo_render` draws the frames of the behavior engine (`nmo_engine_render_frame()`) as CK2's
  Direct3D 7 rasterizer does, through a backend interface with three backends: a software
  rasterizer (built by default, no GPU or window needed), OpenGL 2.1, and SDL_GPU (Metal or Vulkan)
- `nmo_view` shows the frames in an SDL3 window with either GPU backend and plays the engine's
  sounds (`nmo behavior run --window`); `--headless` draws shot frames in memory instead

### CLI and Tooling

- `nmo` command-line tool with a group/action interface and 23 groups: file,
  chunk, object, behavior, patch, parameter, script, resource, texture, data,
  scene, entity, material, mesh, animation, type, validate, convert, diff,
  extension, completion, debug, and repl
- Interactive REPL with tab completion and session persistence
- JSON output with stable envelope (`schema_version`, `tool`, `command`)
- Shell completions for Bash, Fish, Zsh, and PowerShell
- DOT graph export for object hierarchies and behavior graphs
- glTF 2.0 export of a file's 3D scene (`nmo scene export`, `nmo_export_gltf()`):
  entities, meshes, materials and embedded textures
- Semantic object diff and comparison
- Performance statistics and benchmarking

### Cross-Platform

- Windows (MSVC and MinGW), Linux and macOS. The operating system is reached only through
  `src/platform/`; a port to another system implements `src/platform/platform.h` and nothing else
- Paths are UTF-8 everywhere. On Windows the library opens every file through the
  wide-character API, and `nmo.exe` runs with the UTF-8 code page (Windows 10 1903 and later), so
  its command line and the `io.open` of Lua scripts take UTF-8 too
- CI builds and tests every release platform, runs ASan/UBSan, MemorySanitizer and
  pattern-initialized builds, and builds the OpenGL and SDL_GPU backends and the viewer on all
  four platforms

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

### Python Quick Start

`bindings/python` holds the `nmo` package, a ctypes binding over the shared
library. The release packages hold only the static library, so build libnmo
with `-DNMO_BUILD_SHARED=ON` and point `NMO_LIBRARY` at it (and `NMO_DATA_DIR`
at `data/` for the building block and plugin tables). CI runs the binding's
tests against a shared build on Linux, macOS, Windows MSVC and Windows MinGW.

```python
import nmo

with nmo.open("Level_01.NMO") as doc:
    print(doc.info.file_version, len(doc))
    for obj in doc.objects("CK3dObject"):
        print(obj.id, obj.name, obj.fields["world_matrix"])
    doc.export_gltf("level.glb")
```

`Object.set()` writes fields and `Document.save()` saves; `nmo.open(path,
profile="objects")` loads the object table only and reads an object's state
when `Object.load_state()` asks for it.

---

## Architecture

### Layer Stack

Dependency direction is downward: a source file may include headers from its own layer
and from layers below it. From lowest to highest:

```
platform -> core -> io -> format -> type -> extension -> object -> export -> session -> runtime
         -> document -> chunk -> behavior -> engine -> bb -> render -> view -> edit -> lua -> project
```

`tests/layering_audit.cmake` (run as `test_layering_audit`) enforces this order, for direct
includes and for relative includes of another layer's private headers
(`"../runtime/runtime_internal.h"`). No source file includes a header of a higher layer:
`tests/layering_allowlist.txt`, where such an include would be listed as debt, is empty. A new
upward include fails the test, and so does an allowlist entry that is no longer needed.
The same test keeps the block packs apart: the engine's headers include no `bb/` header, and a
pack (`src/bb/<pack>/`) includes neither another pack's files nor its public header.
`tests/layering_calls_audit.py` (`test_layering_calls_audit`) checks the same order on the
functions a file calls, which an include check cannot see when a header pulls in another layer.
It also keeps the system and the graphics APIs at the edges:
- no source outside `src/platform/`, in `src/` or in `tools/`, includes an operating system's
  headers, asks for its extensions (`_POSIX_C_SOURCE` and the like) or branches on which system
  it is built for;
- nothing in `src/` opens, removes or renames a file by path but through the platform layer
  (`nmo_platform_fopen()` and the like), since the C library takes the ANSI code page on Windows;
- graphics API headers appear only in `src/render/<backend>/`, and SDL only in
  `src/render/gpu/` and `src/view/`.

| Layer     | Source           | Headers               | Responsibility                                                                   |
|-----------|------------------|-----------------------|----------------------------------------------------------------------------------|
| Platform  | `src/platform/`  | (internal)            | What the library takes from the operating system: memory, time, errors, UTF-8 paths and files, mapping, atomic replacement, directories, terminals, shared libraries |
| Core      | `src/core/`      | `include/core/`       | Arena, allocator, GUID, hash tables, containers, error, math, logging           |
| IO        | `src/io/`        | `include/io/`         | File, memory, mmap and transactional IO                                          |
| Format    | `src/format/`    | `include/format/`     | File header, chunks, ID remap, image codec, obj parser, file state types, load and save phase timing |
| Type      | `src/type/`      | `include/type/`       | GUID-based type registry, operation dispatch, string conversion, reflection, ancestor state lookup |
| Extension | `src/extension/` | `include/extension/`  | Plugin registry, DLL loading, host ABI, diagnostics, Virtools loader, building block registry |
| Object    | `src/object/`    | `include/object/`     | Context, CK class and manager schemas, vtable dispatch, repository, index, shadow storage |
| Export    | `src/export/`    | `include/export/`     | DOT graph, JSON utilities, text export, ANSI, hex dump                          |
| Session   | `src/session/`   | `include/session/`    | Session state, load and save pipelines, deserializer, serializer, ID sanitizer, reference resolver, runtime kernel, delete |
| Runtime   | `src/runtime/`   | `include/runtime/`    | Document and workspace objects, workspace edit, object queries, hierarchy, imports and reference graphs, summaries, session utilities |
| Document  | `src/document/`  | `include/document/`   | Document load and save entry points, stats, comparison, file state, object diff |
| Chunk     | `src/chunk/`     | `include/chunk/`      | Chunk index and chunk inspection utilities                                       |
| Behavior  | `src/behavior/`  | `include/behavior/`   | Behavior graph traversal, behavior acceleration and interface views, parameter chains, script walker, script edit graph |
| Engine    | `src/engine/`    | `include/engine/`     | Behavior engine: the CK2 runtime the graphs run in (behavior, message, render context, scenes, attributes, arrays) and the manager hooks block packs add theirs through; it registers no blocks (library `nmo_engine`) |
| Block packs | `src/bb/`      | `include/bb/`         | Building block ports, a pack per set of DLLs with the managers they keep: `virtools` (the stock DLLs, font manager), `physics` (physics_RT, the physics manager and the provider API), `terratools` (the TT_* DLLs); each reaches the engine only through `src/bb/bb_sdk.h` (libraries `nmo_bb_virtools`, `nmo_bb_physics`, `nmo_bb_terratools`) |
| Render    | `src/render/`    | `include/render/`     | The engine's frames drawn as Direct3D 7 draws them: the front end, the backend interface (`src/render/render_backend.h`) and the software, OpenGL and SDL_GPU backends (libraries `nmo_render`, `nmo_render_soft`, `nmo_render_gl`, `nmo_render_gpu`) |
| View      | `src/view/`      | `include/engine/nmo_view.h` | An SDL3 window showing the frames with OpenGL or SDL_GPU, its input and the engine's sounds (library `nmo_view`, `NMO_VIEWER`) |
| Edit      | `src/edit/`      | `include/edit/`       | Edit plans and their JSON form, script edits, behavior rewrites (replace, fold), semantic validator, probe analyzer, behavior execute (library `nmo_edit`) |
| Lua       | `src/lua/`       | `include/lua/`        | Lua 5.5 runtime, module system, bindings for all layers, fold-map parser (library `nmo_lua`) |
| Project   | `src/project/`   | `include/project/`    | Project plan, asset/scene/script authoring, executor, manifest, validator (library `nmo_project`) |

### Libraries

The Engine, Block pack, Edit, Lua and Project layers are separate libraries built on top of the
core; the core library does not depend on any of them and contains no engine, edit or Lua code.
The block packs build on the Engine library, which knows none of them: a host creates an engine
and registers the packs it wants (`nmo_bb_virtools_register()`, `nmo_bb_physics_register()`,
`nmo_bb_terratools_register()`), as a player loads building block DLLs. Lua and Project build on
the Edit library.

| Library       | CMake target   | pkg-config       | Umbrella header | Contents                                  |
|---------------|----------------|------------------|-----------------|-------------------------------------------|
| `libnmo`      | `nmo::nmo`     | `libnmo`         | `nmo.h`         | Reading, writing, inspecting, and workspace edits: everything below the Edit layer |
| `libnmo_engine` | `nmo::engine` | `libnmo-engine` | `nmo_engine.h`  | Behavior engine and its block API (`engine/nmo_engine_block.h`), render frames (`engine/nmo_engine_render.h`) |
| `libnmo_bb_virtools`, `libnmo_bb_physics`, `libnmo_bb_terratools` | `nmo::bb_virtools`, `nmo::bb_physics`, `nmo::bb_terratools` | `libnmo-bb` | `bb/nmo_bb_<pack>.h` | The block packs, each with its register function |
| `libnmo_edit` | `nmo::edit`    | `libnmo-edit`    | `nmo_edit.h`    | Edit plans, script edits, behavior rewrites, semantic validator, probe analyzer, behavior execute |
| `libnmo_lua`  | `nmo::lua`     | `libnmo-lua`     | `nmo_lua.h`     | Lua runtime, bindings, `nmo_behavior_execution_lua_runtime()` |
| `libnmo_project` | `nmo::project` | `libnmo-project` | `nmo_project.h` | Project plans, authoring, executor, manifest reader |

`nmo.h` does not include the Edit, Lua or Project headers; include `nmo_edit.h`, `nmo_lua.h` or
`nmo_project.h` (or the individual `edit/*.h`, `lua/*.h`, `project/*.h` headers) and link the
matching library. Static builds produce one archive per library; with `NMO_BUILD_SHARED=ON`
all of them are compiled into the single shared `nmo` library and `nmo::engine`, the
`nmo::bb_*` packs, `nmo::edit`, `nmo::lua` and `nmo::project` are interface targets on it.

Optional hosts build on the engine and the packs: `nmo_physics_ivp` (with
`NMO_IVP_SOURCE_DIR` set to a libivp source tree) simulates the physics pack's objects, the
renderer draws the engine's frames, and `nmo_view` (with `NMO_VIEWER=ON`) shows them in an SDL3
window (`nmo behavior run --window`). The renderer's libraries are built in the tree for the CLI
and the viewer and are not installed:

| Library          | Option           | Needs        | What it is                                                       |
|------------------|------------------|--------------|------------------------------------------------------------------|
| `nmo_render`     | `NMO_RENDER`     | -            | The front end: the engine's frames turned into a backend's draws |
| `nmo_render_soft`| `NMO_RENDER`     | -            | A software rasterizer drawing into memory (`nmo_render_soft_pixels()`) |
| `nmo_render_gl`  | `NMO_RENDER_GL`  | OpenGL       | The OpenGL 2.1 backend, in the context current in the host; the functions past 1.1 come from the host's loader (`nmo_render_create_gl(engine, loader, &render)`, where SDL's is `SDL_GL_GetProcAddress`) |
| `nmo_render_gpu` | `NMO_RENDER_GPU` | SDL3         | The SDL_GPU backend: a device on Metal or Vulkan, in a window or offscreen |
| `nmo_view`       | `NMO_VIEWER`     | SDL3, OpenGL | The SDL3 window, drawing with either GPU backend (implies both)  |

The IVP provider uses libivp's build options directly. A fresh build with a
current libivp defaults to reference-compatible internal state:

```bash
cmake -S . -B build -DNMO_IVP_SOURCE_DIR=/path/to/libivp
cmake --build build
```

To make physics independent of incidental container and allocator history,
configure with `-DIVP_STABLE_INTERNAL_STATE=ON`. This changes some trajectories
from the reference. `IVP_EDIT_LOCALITY=ON` also requires that option.

CMake preserves options in existing build directories. When libivp migrates a
cache containing the old `IVP_REFERENCE_INTERNAL_STATE` option, it preserves
the old behavior by setting the inverse value of `IVP_STABLE_INTERNAL_STATE`.
To switch an existing build to the new reference-compatible default, reconfigure
it with `-DIVP_STABLE_INTERNAL_STATE=OFF`. The configuration output reports
the selected stable-internal-state value.

Tooling for one game lives under `apps/`, outside the libraries, which know nothing of it:
`apps/ballance/` holds the Ballance level scripts (`scripts/`) and the data they read (`data/`);
[docs/ballance-level-builder.md](docs/ballance-level-builder.md) describes them.

[`apps/ballance-editor/`](apps/ballance-editor/README.md) is a separate application
that consumes the toolchain. It has its own launcher, browser assets and tests;
editor development is paused. The toolchain has no dependency on that application.

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
- **Downward dependencies**: lower layers do not include headers from higher
  layers. `test_layering_audit` blocks new violations; the allowlist for
  existing ones (`tests/layering_allowlist.txt`) is empty.

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
| SDL3                  | -               | Optional: the SDL_GPU backend and the viewer (`NMO_RENDER_GPU`, `NMO_VIEWER`); tested with 3.4.18 |
| OpenGL                | 2.1             | Optional: the OpenGL backend and the viewer (`NMO_RENDER_GL`, `NMO_VIEWER`) |

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
completions. It has no shared library, which the Python binding needs (see
[Python Quick Start](#python-quick-start)). To build one locally:

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

`nmo.exe` embeds a manifest that runs it with the UTF-8 code page (`cmake/nmo_utf8.manifest`,
through `nmo_use_utf8_code_page()`), so paths beyond ASCII work on its command line on Windows 10
1903 and later; older Windows keep the ANSI code page. The library itself takes UTF-8 paths on
every Windows, whatever the program's code page.

### Build Options

| Option                     | Default | Description                                  |
|----------------------------|---------|----------------------------------------------|
| `NMO_BUILD_TESTS`          | ON      | Build the test suite (enables ctest)         |
| `NMO_BUILD_TOOLS`          | ON      | Build the `nmo` CLI tool                     |
| `NMO_BUILD_EXAMPLES`       | OFF     | Build example programs                       |
| `NMO_BUILD_SHARED`         | OFF     | Build as shared library (SOVERSION 3)        |
| `NMO_USE_ISOCLINE`         | ON      | Line editing in the REPL (see isocline above)|
| `NMO_MINGW_STATIC_RUNTIME` | OFF     | Link MinGW CLI executables with `-static`    |
| `NMO_ENABLE_SIMD`          | OFF     | Enable SIMD optimizations                    |
| `NMO_ENABLE_SANITIZERS`    | auto    | ASan/UBSan in Debug (non-Windows by default) |
| `NMO_RENDER`               | ON      | The renderer and its software backend        |
| `NMO_RENDER_GL`            | OFF     | The OpenGL backend (needs OpenGL)            |
| `NMO_RENDER_GPU`           | OFF     | The SDL_GPU backend (needs SDL3)             |
| `NMO_VIEWER`               | OFF     | The SDL3 viewer; implies both GPU backends   |
| `NMO_IVP_SOURCE_DIR`       | (empty) | A libivp source tree: builds the physics provider `nmo_physics_ivp` |

### API Reference

With Doxygen installed, the `nmo_docs` target writes the API reference, with
the public headers and the documents below, to `<build>/docs/html`:

```sh
cmake --build cmake-build-release --target nmo_docs
```

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
target_link_libraries(app PRIVATE nmo::edit nmo::nmo)                # edit plans and script edits
target_link_libraries(app PRIVATE nmo::lua nmo::project nmo::nmo)   # Lua and project; both bring nmo::edit
```

```sh
cc app.c $(pkg-config --cflags --libs libnmo)
cc app.c $(pkg-config --cflags libnmo-lua libnmo-project) $(pkg-config --static --libs libnmo-lua libnmo-project)
```

---

## Reference Documentation

- [File Format](docs/file-format.md): the layout of `.nmo`/`.cmo`/`.vmo` files,
  from the header to chunk encoding, and
  [how each class stores its state](docs/file-format-classes.md).
- [CLI Reference](docs/cli-reference.md): every `nmo` command group, the
  REPL, and shell completions.
- [API Documentation](docs/api.md): C entry points by area.
- [API Tiers](docs/api-tiers.md): which public APIs are stable and what
  each tier promises.
- [Ballance Level Format](docs/ballance-level-format.md): what a Ballance
  level file holds, measured on the 13 original levels.
- [Ballance Level Builder](docs/ballance-level-builder.md): building,
  checking and playing levels with `apps/ballance`.
- [Free-form Ballance Geometry](docs/ballance-freeform.md): rail and floor
  paths, connectors, holed platforms, scenery attachments and route templates.
- [Ballance Native Resources and Motion](docs/ballance-native-resources.md):
  extraction provenance, placement entries, motion contracts and original-level probes.
- [Ballance Passage and Landing Constraints](docs/ballance-spatial-constraints.md):
  floor support, ball clearance, cross-layer arrivals and passage motion rules.
- [Ballance Actions and Replayable Scenarios](docs/ballance-gameplay-scenarios.md):
  native action conditions, bounded negative cases and checkpoint recovery.
- [Ballance Test Base](docs/plans/2026-11-02-ballance-test-base.md): implemented
  native gameplay loading, sector starts and local contact probes.

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
- **`auto-var-init`** and **`memory-sanitizer`**: the same tests built with every automatic
  variable filled with a pattern, and under MemorySanitizer, for reads of memory never written
- **`render-backends`**: a build with `NMO_VIEWER` (the OpenGL and SDL_GPU backends and the
  viewer) against SDL3 on Linux, macOS, Windows MSVC and Windows MinGW, then the render, viewer
  and layering tests; only the macOS runner has a GPU, elsewhere the SDL_GPU tests skip
- **`shared-library`**: libnmo built with `NMO_BUILD_SHARED=ON` on the same four systems, then
  `test_python_binding`, which reads a sample kept in `bindings/python/tests/data`
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
