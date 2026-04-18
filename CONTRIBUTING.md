# Contributing to libnmo

Thank you for your interest in contributing to libnmo!  This document describes
the development workflow, code standards, and review process.

## Code of Conduct

- Be respectful and constructive
- Focus on technical merit
- Help others learn and grow
- Follow project conventions

## Getting Started

1. Fork the repository
2. Clone your fork: `git clone https://github.com/YOUR_USERNAME/libnmo.git`
3. Create a branch: `git checkout -b my-feature`
4. Make your changes and run tests
5. Commit, push, and open a pull request

## Development Setup

### Prerequisites

- CMake 3.15+
- C17-compatible compiler (GCC, Clang, MSVC)
- Git
- miniz or system zlib (bundled fallback is included)
- yyjson (bundled; only required for JSON export features)

### Building (Ninja recommended)

```
cmake -B cmake-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
```

### Running Tests

```
ctest --test-dir cmake-build-debug -j4 --output-on-failure
```

### Refreshing Virtools Data

The checked-in Virtools JSON data is generated from the Ballance Virtools
runtime with `VirtoolsDataExporter.exe`.  Use the repository wrapper so manager
DLLs, BuildingBlock DLLs, plugin-level GUID metadata, JSON encoding, and stale
data checks stay consistent:

```powershell
$env:VIRTOOLS_DATA_EXPORTER = "path\to\VirtoolsDataExporter.exe"
$env:VIRTOOLS_GAME_ROOT = "path\to\Ballance"
powershell -ExecutionPolicy Bypass -File tools\scripts\export_virtools_data.ps1 `
  -ExtraPluginDirs "path\to\extra\BuildingBlocks"
python tools/scripts/gen_virtools_data.py
```

The exporter must support `-g plugins.json`; older exporter builds only produce
parameter, operation, and BuildingBlock JSON and are rejected.  Before
committing a data refresh, verify the generated files are current:

```powershell
powershell -ExecutionPolicy Bypass -File tools\scripts\export_virtools_data.ps1 `
  -ExtraPluginDirs "path\to\extra\BuildingBlocks" `
  -Check
python tools/scripts/gen_virtools_data.py
```

## Coding Standards

### Style

- Indentation: 4 spaces (no tabs)
- Line length: 100 characters maximum
- Braces: K&R style (opening brace on same line)
- Naming:
  - Functions: `nmo_module_function_name()`
  - Types: `nmo_type_name_t`
  - Enums: `NMO_ENUM_VALUE`
  - Macros: `NMO_MACRO_NAME`
- Comments: `/* */` for multi-line, `//` for single-line
- Documentation: Doxygen for all public APIs (see below)

### Example

```c
/**
 * @brief Allocate memory from an arena.
 * @param arena Arena allocator
 * @param size  Size in bytes
 * @param align Alignment requirement (must be power of two)
 * @return Pointer to allocated memory, or NULL on failure
 */
void *nmo_arena_alloc(nmo_arena_t *arena, size_t size, size_t align) {
    if (!arena || size == 0) {
        return NULL;
    }

    /* Compute aligned start address */
    uintptr_t start = (arena->cursor + align - 1) & ~(align - 1);
    if (start + size > arena->end) {
        return NULL;  /* would overflow current block */
    }

    arena->cursor = start + size;
    return (void *)start;
}
```

### Architecture Rules

The layer stack is (strict -- no upward dependencies):

```
App -> Session -> Object -> Extension -> Type -> Format -> IO -> Core
```

Lower layers NEVER include headers from higher layers.

1. No circular dependencies: always depend downward in the layer hierarchy
2. Both `serialize` AND `deserialize` vtable methods are required for every new object type
3. Explicit ownership: use arena allocation, reference counting, or clear ownership transfer
4. Error handling: return `nmo_status_t`; check every non-`NMO_OK` result; use
    `nmo_last_error_*()` for detailed diagnostics when the API documents it
5. Memory safety: bounds-check all array and buffer accesses
6. Platform independence: use portable types (`uint32_t`, not `unsigned int`), portable abstractions

### Directory Reference

| Directory      | Layer     | Notes                                      |
|----------------|-----------|---------------------------------------------|
| src/core/      | Core      | No dependencies on any other nmo layer      |
| src/io/        | IO        | Depends only on Core                        |
| src/format/    | Format    | Depends on Core, IO                         |
| src/type/      | Type      | Depends on Core, IO, Format                 |
| src/extension/ | Extension | Depends on Core through Type                |
| src/object/    | Object    | Depends on Core through Extension           |
| src/session/   | Session   | Depends on Core through Object              |
| src/app/       | App       | Depends on all lower layers                 |

### Chunk API Notes

All chunk positions and sizes are in **DWORDs (4 bytes)**, not bytes.  Use
`nmo_chunk_read_dword()` / `nmo_chunk_write_dword()` etc.  Object IDs embedded
in a chunk require `StartObjectIDSequence` / `StopObjectIDSequence` guards.

## Testing

### Unit Tests

- Test each function in isolation
- Use the test framework in `tests/test_framework.h`
- Macros: `TEST()`, `ASSERT_EQ()`, `ASSERT_NE()`, `ASSERT_TRUE()`, `ASSERT_FALSE()`
- Test edge cases and error conditions (invalid input, truncated buffers, NULL pointers)

```c
#include "test_framework.h"
#include "core/nmo_arena.h"

TEST(arena, basic_allocation) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024);
    ASSERT_NE(NULL, arena);

    void *ptr = nmo_arena_alloc(arena, 100, 8);
    ASSERT_NE(NULL, ptr);

    nmo_arena_destroy(arena);
}
```

### Integration Tests

- Test complete workflows
- Test with real files from `data/` (use the `NMO_TEST_DATA_DIR` macro to locate them)
- All 23 core CK classes and 2 manager schemas must survive a round-trip test
- CLI write commands must save to a temporary copy, reload the saved file, and
  run `nmo validate all` before the test passes.

### Fuzz Tests

- Test parsers with malformed / truncated input
- Crash and memory-corruption free is a hard requirement
- AFL or libFuzzer are both accepted

## Documenting Code

Add Doxygen comments to every public API:

```c
/**
 * @brief Short one-line summary.
 *
 * Longer description with more detail.
 *
 * @param param1 Description of param1
 * @param param2 Description of param2
 * @return Description of return value
 * @retval NMO_OK   Success
 * @retval NMO_ERR_NOMEM  Out of memory
 */
```

Update `README.md` for user-facing changes and `CHANGELOG.md` for every release or
significant internal change.

## Pull Request Process

1. Before submitting:
   - All tests must pass (`ctest --output-on-failure`)
   - Shell completions must be current (`python tools/scripts/gen_completions.py --check`)
   - Virtools exported JSON must be current when `data/virtools_*.json` changes
   - Run static analysis: cppcheck, clang-tidy
   - Check for memory leaks: valgrind (Linux) or DrMemory (Windows)
   - Update `CHANGELOG.md` under `[Unreleased]`

## Release Packaging

Windows MinGW release packages are produced with:

```powershell
pwsh tools/scripts/package_release.ps1 -Version 1.0.0 -BuildDir build_package_release_static -DistDir dist
```

The script configures a static-runtime Release build, runs CTest, installs into
a staging tree, copies documentation and third-party licenses, verifies
`nmo completion <shell>` against installed completion files, rejects packaged or
import-table `libwinpthread` dependencies, runs an external static-link smoke
test, and creates the final zip in `dist/`.

2. PR description should include:
   - What changed and why
   - Which issue(s) this addresses (if any)
   - Testing performed

3. Review process:
   - At least one maintainer approval required
   - All CI checks must pass
   - Address all review comments before merge

## Commit Messages

Format:

```
<type>(<scope>): <subject>

<body>

<footer>
```

Types: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`

Examples:

```
feat(io): add memory-mapped file support

Implement zero-copy mmap IO for better performance on large files.
Uses mmap on POSIX and CreateFileMapping on Windows.

Closes #42
```

```
fix(chunk): return NMO_ERR_TRUNCATED_CHUNK on short reads

Previously a read past the end of the chunk buffer would dereference
out-of-bounds memory.  Now all nmo_chunk_read_* entry points check the
remaining DWORD count before advancing the cursor.

Fixes #91
```

## Reporting Issues

### Bug Reports

Include:
- libnmo version (output of `nmo file info --version` or git tag)
- Operating system and compiler version
- Steps to reproduce
- Expected vs actual behavior
- Error messages or logs
- Minimal reproducer file or source snippet if possible

### Feature Requests

Include:
- Use case description
- Proposed API sketch (if applicable)
- Alternative solutions considered
- Impact on the existing layer boundaries

## Questions

- Check existing documentation under `docs/` and `plans/` first
- Check `claude_doc/` for deep-dive implementation notes per module
- Open a GitHub issue for questions not answered there
