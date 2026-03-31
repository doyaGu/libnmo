# libnmo

**libnmo** is a modern C17 library for reading and writing Virtools file formats
(`.nmo`, `.cmo`, `.vmo`).  It is designed as a complete serialization library
with a clean seven-layer architecture, not merely a parser.

## Quick Start (CLI)

```
nmo file info file.nmo
nmo file header file.nmo
nmo object list --class CK3dEntity file.nmo
nmo object show 123 file.nmo
nmo object find --name "Player*" file.nmo
nmo chunk list file.nmo
nmo debug load-phases file.nmo
nmo validate all file.nmo
nmo validate references file.nmo
```

## Features

- Symmetric read/write operations driven by unified schemas
- Partial understanding support for gradual reverse engineering
- Complete format support for Virtools files (versions 2-9)
- GUID-first type identification with O(1) hash lookups
- Object repository with 50-200x faster indexed lookups (by class / name / GUID)
- Arena allocation with mark/rewind scope for session-local data
- Shadow storage for lossless round-trips of unknown chunk tails
- Two-phase commit save pipeline with optional compression and CRC
- Memory-mapped zero-copy IO in addition to buffered file IO
- Extension/plugin system with static and DLL-based registration
- DSL subsystem for scripting
- Cross-platform: Windows, Linux, macOS
- Thread-safe context with atomic reference counting

## Architecture

Layer dependency direction (strict, no upward imports):

```
App -> Session -> Object -> Extension -> Type -> Format -> IO -> Core
```

| Layer     | Location       | Responsibility                                                    |
|-----------|----------------|-------------------------------------------------------------------|
| Core      | src/core/      | Arena, allocator, GUID, hash tables, containers, error, math      |
| IO        | src/io/        | File, memory, mmap, compressed, checksummed, transactional IO     |
| Format    | src/format/    | File header, chunk API (DWORD-aligned), ID remap, image codec     |
| Type      | src/type/      | GUID-based type registry, operation dispatch, string conversion   |
| Extension | src/extension/ | Plugin registry, DLL loading, host ABI, diagnostics               |
| Object    | src/object/    | 23 CK class schemas + 2 manager schemas, vtable serialization     |
| Session   | src/session/   | Load session, ID sanitizer, reference resolver, runtime graph     |
| App       | src/app/       | Context, session lifecycle, 15-phase load, 2-phase save, CLI API  |

### Key Design Decisions

- DWORD alignment: all chunk positions and sizes are in 4-byte DWORDs, not bytes
- Move semantics: APIs taking `T**` transfer ownership (callee sets `*ptr = NULL`)
- ECS-style state: combined state buffers with ancestor offsets for polymorphic access
- IntList verbatim: `id_offsets`, `chunk_offsets`, `manager_offsets` stored exactly as
  Virtools writes them for deterministic remap and iteration
- ID sanitization: bit 23 (`0x800000`) marks reference-only IDs; always call
  `nmo_id_sanitize()` before using an ID at runtime

## Building

### Prerequisites

- CMake 3.15 or later
- C17-compatible compiler (GCC, Clang, MSVC)
- miniz or system zlib (bundled fallback included)
- yyjson (bundled, optional -- required for JSON export)

### Recommended (Ninja, debug)

```
cmake -B cmake-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
```

### Release

```
cmake -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release
```

### Windows (MSVC, no Ninja)

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Build Options

| Option                   | Default | Description                           |
|--------------------------|---------|---------------------------------------|
| NMO_BUILD_TESTS          | ON      | Build test suite (enables ctest)      |
| NMO_BUILD_TOOLS          | ON      | Build nmo CLI tool                    |
| NMO_BUILD_EXAMPLES       | OFF     | Build example programs                |
| NMO_BUILD_SHARED         | OFF     | Build as shared library (SOVERSION 2) |
| NMO_ENABLE_SIMD          | OFF     | Enable SIMD optimizations             |
| NMO_ENABLE_SANITIZERS    | auto    | ASan/UBSan in Debug (non-Windows)     |

### Running Tests

```
ctest --test-dir cmake-build-debug -j4 --output-on-failure
```

Run a specific test binary:

```
.\cmake-build-debug\tests\unit\test_arena.exe
.\cmake-build-debug\tests\unit\test_chunk.exe
.\cmake-build-debug\tests\integration\test_data_roundtrip.exe
```

## Quick Start (API)

```c
#include <nmo.h>

int main(int argc, char **argv) {
    /* Create context -- owns type/operation/manager/extension registries */
    nmo_context_t *ctx = nmo_context_create(NULL);
    if (!ctx) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }

    /* Load file -- creates a session-local arena, repository, etc. */
    nmo_session_t *session = nmo_session_load(ctx, argv[1]);
    if (!session) {
        fprintf(stderr, "Failed to load: %s\n", argv[1]);
        nmo_context_release(ctx);
        return 1;
    }

    /* Query */
    nmo_file_info_t info = nmo_session_get_file_info(session);
    printf("Object count: %u\n", info.object_count);

    /* Cleanup */
    nmo_session_destroy(session);
    nmo_context_release(ctx);
    return 0;
}
```

## CLI Tools

### nmo (v3 group/action interface)

```
# File inspection
nmo file info <file>
nmo file header <file>

# Object discovery
nmo object list [--class <name>] <file>
nmo object show <id> <file>
nmo object find [--name <pattern>] <file>

# Chunk inspection
nmo chunk list <file>
nmo chunk show <id> <file>

# Type system
nmo type list <file>
nmo type show <name> <file>
nmo type class-tree <file>

# Validation
nmo validate all <file>
nmo validate references <file>

# Debug
nmo debug load-phases <file>
nmo repl start <file>
```

JSON output uses a stable envelope: `schema_version`, `tool`, `command`.

## API Documentation

### Core APIs

- Context management: `nmo_context_create()`, `nmo_context_release()` -- `include/app/nmo_context.h`
- Session management: `nmo_session_load()`, `nmo_session_destroy()`   -- `include/app/nmo_session.h`
- Error handling: `nmo_result_t`, error codes                          -- `include/core/nmo_error.h`

### Chunk APIs

- Chunk creation: `nmo_chunk_create()`         -- `include/format/nmo_chunk.h`
- Reading:        `nmo_chunk_read_dword()` etc  -- `include/format/nmo_chunk_parser.h`
- Writing:        `nmo_chunk_write_dword()` etc -- `include/format/nmo_chunk_writer.h`

Note: all chunk positions and sizes are in DWORDs (4 bytes), not bytes.

### Type System APIs

- Type registry:  `nmo_type_registry_lookup_by_guid()` -- `include/type/nmo_type_system.h`
- Operations:     `nmo_operation_registry_dispatch()`  -- `include/type/nmo_operations.h`
- Enums/flags:    `nmo_type_registry_register_enum()`  -- `include/type/nmo_dynamic_types.h`
- String convert: `nmo_type_to_string()`, `nmo_type_from_string()` -- `include/type/nmo_type_string.h`

### Object APIs

- Repository: `nmo_object_repository_add()`, `nmo_object_repository_find_by_id()`
  -- `include/object/nmo_object_repository.h`
- Index:      `nmo_object_index_find_by_class()`, `nmo_object_index_find_by_name()`
  -- `include/object/nmo_object_index.h`

## Testing

Current test status: **102/102 passing**

Test categories:
- `tests/unit/`        -- isolated function tests per module
- `tests/integration/` -- full workflow tests with real files from `data/`
- `tests/benchmarks/`  -- (planned) performance baseline

Test framework: custom macros in `tests/test_framework.h`.
Macros: `TEST()`, `ASSERT_EQ()`, `ASSERT_NE()`, `ASSERT_TRUE()`, etc.

## Development Status

**Current version**: 1.0.0-dev (Phase 3 -- Production Ready, in progress)

### Completed Phases

- [x] Phase 1 -- Project setup, layer interfaces, build system
- [x] Phase 2 -- Core layer (arena, GUID, hash tables, containers)
- [x] Phase 3 -- IO layer (file, memory, mmap, compressed, checksum, transactional)
- [x] Phase 4 -- Format layer (header, chunk parser/writer, ID remap, image)
- [x] Phase 5 -- Performance: object index, hash reserve, arena config (50-200x speedup)
- [x] Phase 6 -- Type system: type/operation/enum/flags registries, string conversion
- [x] Phase 6.1 -- Object layer migration: 23 CK classes + 2 managers, vtable dispatch
- [x] Phase 7.1 -- ID sanitizer (0x800000 mask, external reference handling)
- [x] Phase 7.2 -- Shadow blob retention (included-files blob + chunk raw tails)
- [x] Phase 7.3 -- Chunk writer version context stack (16-layer, parent version propagation)
- [x] Phase 7.4 -- Two-phase commit save pipeline (Layout/Serialize -> Pack/Commit)
- [x] Phase 8.1 -- Dual-track IO (mmap zero-copy + zlib decompression)
- [x] Phase 8.2 -- Reserve-and-patch for forward-reference writes
- [x] Phase 8.3 -- IntList auditor (debug-mode write count verification)
- [x] Phase 8.4 -- Round-trip test framework + DOM comparison API

### In Progress: Road to v1.0.0 (Production Ready)

P0 -- must complete before v1.0.0:

- [ ] Chunk read bounds checks: uniform `NMO_ERR_TRUNCATED_CHUNK` on all read entry points
- [ ] Round-trip coverage expansion: 50+ real/synthetic files, 23/23 core object types, >=99% pass
- [ ] CI pipeline: three-platform builds (Windows / Linux / macOS), PR-triggered suite

P1 -- before v1.0.0:

- [ ] Ownership refactor: `nmo_ownership_tag_t`, arena mark/rewind, retain-release semantics
- [ ] Sub-chunk coverage enhancement: more complex CKBehavior variants
- [ ] Performance benchmark suite: `tests/benchmarks/`, load/save/mmap baseline

### Backlog: v1.1.0

- [ ] nmo-codegen: schema code generation for core types
- [ ] nmo-memtrace: arena/heap usage stats with JSON output
- [ ] Update `docs/design.md`, add OBJECT_LAYER_MIGRATION_GUIDE.md, OWNERSHIP_RULES.md
- [ ] Source file rename: `ck*_schemas.c` -> `*_schemas.c` (plan in
  REFACTOR_PLAN_REMOVE_CK_PREFIX.md, not yet applied)

## Documentation

- [docs/architecture.md](docs/architecture.md) -- Comprehensive architecture reference
- [docs/CHUNK_API_GUIDE.md](docs/CHUNK_API_GUIDE.md) -- Chunk API with examples
- [CONTRIBUTING.md](CONTRIBUTING.md) -- Contribution guidelines and code standards
- [MIGRATION_GUIDE_V2.md](MIGRATION_GUIDE_V2.md) -- Migration from Builder-pattern schema API
- [TODO.md](TODO.md) -- Current task tracking and milestone definitions
- [plans/](plans/) -- Detailed design plans for active and upcoming work
- [claude_doc/](claude_doc/) -- Deep-dive implementation analysis per module

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the full guide.  Key points:

- 4-space indent, 100-char line limit, K&R braces
- Naming: `nmo_module_function()`, `nmo_type_name_t`, `NMO_ENUM_VALUE`, `NMO_MACRO`
- Layer rule: no upward dependencies -- lower layers never import higher ones
- Both `serialize` AND `deserialize` vtable methods required for new object types
- All public APIs documented with Doxygen `/** @brief ... @param ... @return ... */`
- Run tests before submitting; update CHANGELOG.md

### PR Checklist

- [ ] All 102+ tests pass
- [ ] No upward layer dependencies introduced
- [ ] All error paths handled (return `nmo_result_t`, check `.code != NMO_OK`)
- [ ] cppcheck / clang-tidy clean
- [ ] valgrind clean (on Linux)
- [ ] CHANGELOG.md updated

## License

MIT -- see [LICENSE](LICENSE) for details.

## Acknowledgments

This project implements the Virtools file format based on extensive reverse engineering
and documentation efforts by the community.

## Support

- Issues: https://github.com/doyaGu/libnmo/issues
- Documentation: `docs/` directory
- Discussions: GitHub Discussions
