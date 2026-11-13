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
```

The exporter must support `-g plugins.json`; older exporter builds only produce
parameter, operation, and BuildingBlock JSON and are rejected.  Before
committing a data refresh, verify the generated files are current:

```powershell
powershell -ExecutionPolicy Bypass -File tools\scripts\export_virtools_data.ps1 `
  -ExtraPluginDirs "path\to\extra\BuildingBlocks" `
  -Check
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

### Ballance Python and Browser Code

The Python applications under `apps/ballance` and `apps/ballance-editor` each
have a `ruff.toml` and a pinned `requirements-dev.txt`. Use four-space indentation,
a 100-character line limit and single-quoted strings. Ruff checks unused imports,
undefined names and compressed statements as well as formatting. CLI and test
entry points may adjust `sys.path` before importing the application.

Use descriptive names for state and domain objects. Give branches and exception
handlers their own blocks, and split functions when validation, file generation
and process control obscure one another. Keep short mathematical names where
they make vector and matrix expressions easier to read.

The editor's browser code uses Prettier and ESLint, pinned in `package-lock.json`.
Use braces for control flow, one variable declaration per statement and no comma
expressions. HTML contains the page structure; executable code lives in separate
JavaScript files. Preserve script loading order and request-token handling.

With a Python virtual environment active, run these commands from the repository
root. NumPy and Pillow support rendering and its tests; Node.js 24 runs the
frontend tooling:

```sh
python3 -m pip install numpy Pillow -r apps/ballance/requirements-dev.txt
make -C apps/ballance check-style
make -C apps/ballance test

python3 -m pip install -r apps/ballance-editor/requirements-dev.txt
npm --prefix apps/ballance-editor ci
make -C apps/ballance-editor check-style
make -C apps/ballance-editor test
```

Each directory also provides `make format`; it changes files, while `check-style`
only checks them. Review the diff after formatting. Native tests need `BALLANCE`,
`BALLANCE_ELEMENTS`, `NMO_TEST_CLI` and, for the optional physics cases,
`NMO_TEST_PHYSICS=1`. Use absolute paths with `make -C`. Missing native assets cause
those tests to skip; a passing run with skips does not establish native coverage.

The two test suites are separate. The editor may depend on the toolchain, but
no toolchain module, test, launcher or build fingerprint may depend on the editor.
Application-only changes must leave toolchain evidence current. See the
[editor guide](apps/ballance-editor/README.md) for workspace and playback behavior.

### Authored maps and shared tooling

Commit reusable generators, validators, native resource descriptions and tests
with small, self-contained fixtures. Keep complete authored routes, recorded
solutions, map-specific scenarios, previews and production notes local. A test
that needs a particular authored map belongs with that map's local tests.

Use `.local/ballance/` for map inputs, local tests and archives, and `out/` for
generated levels and reports. Both are ignored. `apps/ballance/data/` tracks only
the five shared catalogs listed in the [builder guide](docs/ballance-level-builder.md#the-data).
Existing ignored map inputs may remain there for local path compatibility.
Adding a shared catalog requires an explicit exception in `.gitignore`.

The normal toolchain and editor test commands above run their tracked suites.
A prepared workspace may also have `.local/ballance/inventory.json` and a local
test runner. When that runner is present, run the preserved map tests separately:

```sh
python3 .local/ballance/run_tests.py
python3 .local/ballance/run_tests.py --editor
```

These local files are not supplied by a fresh clone. Native cases use the same
asset environment variables as the tracked tests. Keep complete map inputs out
of tracked tests; retain reusable assertions with small independent fixtures.

Ignore rules do not protect content pasted into an already tracked file. Review
the staged diff for map data, solutions and map-dependent documentation before
committing. Stage exact files or hunks:

```sh
git diff --cached --name-only
git diff --cached --check
git diff --cached
```

A prepared workspace may install `.git/hooks/pre-commit` to run
`.local/ballance/check_staged.py`. That local guard checks staged paths, known
map references and JSON route payloads, including forced additions and renamed
route files. It allows the shared catalogs and generic code fixtures. The hook
and its checker are workspace-local and are not installed by cloning; check
their presence before relying on them.

### Ballance toolchain regression coverage

The tracked suite exercises shared behavior with small generated routes and
traces. It runs without the ignored map inputs or `.local/ballance/` archives.

| Tests | Coverage |
| --- | --- |
| `test_tolerance_contracts.py` | Perturbation grids, scenario references, release-state identity, death outcomes, zero-control replay consistency and report evidence |
| `test_shortcut_contracts.py` | Local coordinates, ambiguous anchors, route splices, checkpoint retention, landing identity, required conditions and time savings |
| `test_floor_transitions.py` | Thickness stations, custom profiles, closed side walls, brick bands, UV seams, inclined platforms and reversed or mirrored ports |

Run the tolerance and shortcut tests without native assets:

```sh
python3 -m unittest discover -s apps/ballance/tests -p '*_contracts.py' -v
```

Their runner tests use `verification_fixtures.py` to build real metadata and
substitute external native dependencies and execution. Compilation, trace
parsing, verdicts and evidence hashes use the production code. These synthetic
logs test the checks; they do not establish that a ball can traverse a level.

`test_floor_transitions.py` also has an opt-in native case that plays two small
generated routes, including a rail approach to an inclined platform. Run it
with absolute paths to the game, extracted meshes and CLI:

```sh
BALLANCE=/path/to/Ballance BALLANCE_ELEMENTS=/path/to/extracted-elements \
NMO_TEST_CLI=/path/to/nmo NMO_TEST_PHYSICS=1 \
python3 -m unittest discover -s apps/ballance/tests -p test_floor_transitions.py -v
```

Without that environment, the geometry tests run and the native case skips.
For full coverage, use the same environment with `make -C apps/ballance test`.
Record skips when reporting results, and validate authored maps separately in
their local workspace. See the [free-form geometry guide](docs/ballance-freeform.md#build-and-validate)
for the related geometry and traversal tests.

### Architecture Rules

The layer stack, lowest to highest:

```
platform -> core -> io -> format -> type -> extension -> object -> export -> session -> runtime
         -> document -> chunk -> behavior -> engine -> bb -> render -> view -> edit -> lua -> project
```

A source file may include headers from its own layer and from layers below it, never from a
higher one; a relative include of another layer's private header (`"../runtime/x.h"`) counts too.
`test_layering_audit` (`tests/layering_audit.cmake`) checks this. Upward includes would be
recorded in `tests/layering_allowlist.txt` as debt, and the file is empty; do not add to it to
make a new dependency pass. Move the dependency down the stack instead, or give the lower layer an
interface the higher one fills in (as `nmo_chunk_file_context_t.ref_tokens` and
`nmo_session_t.behavior_index_destroy` do). The audit also fails when an allowlist entry is no
longer needed.

1. No circular dependencies: always depend downward in the layer hierarchy
2. Both `serialize` AND `deserialize` vtable methods are required for every new object type
3. Explicit ownership: use arena allocation, reference counting, or clear ownership transfer
4. Error handling: return `nmo_status_t`; check every non-`NMO_OK` result; use
    `nmo_last_error_*()` for detailed diagnostics when the API documents it
5. Memory safety: bounds-check all array and buffer accesses
6. Platform independence: use portable types (`uint32_t`, not `unsigned int`), and reach the
    operating system only through `src/platform/platform.h`. No other source, in `src/` or in
    `tools/`, includes a system header (`<windows.h>`, `<unistd.h>`, `<sys/...>`), defines a
    feature macro (`_POSIX_C_SOURCE`) or branches on `_WIN32`, `__APPLE__` or `__linux__`; add
    what is missing to `platform.h` and implement it in `platform_posix.c` and
    `platform_windows.c`. Paths are UTF-8: the library opens, removes and renames files only with
    `nmo_platform_fopen()`, `nmo_platform_remove()` and `nmo_platform_replace()`, and gives yyjson
    and Lua open files or bytes, not paths. Graphics API headers belong in `src/render/<backend>/`,
    and SDL only in `src/render/gpu/` and `src/view/`. `test_layering_audit` checks all of this.

### Directory Reference

Ranks match the order in `tests/layering_audit.cmake`; a layer may include itself and any layer
with a lower rank.

| Rank | Directory        | Layer       |
|------|------------------|-------------|
| 1    | `src/platform/`  | Platform    |
| 2    | `src/core/`      | Core        |
| 3    | `src/io/`        | IO          |
| 4    | `src/format/`    | Format      |
| 5    | `src/type/`      | Type        |
| 6    | `src/extension/` | Extension   |
| 7    | `src/object/`    | Object      |
| 8    | `src/export/`    | Export      |
| 9    | `src/session/`   | Session     |
| 10   | `src/runtime/`   | Runtime     |
| 11   | `src/document/`  | Document    |
| 12   | `src/chunk/`     | Chunk       |
| 13   | `src/behavior/`  | Behavior    |
| 14   | `src/engine/`    | Engine      |
| 15   | `src/bb/`        | Block packs |
| 16   | `src/render/`    | Render      |
| 17   | `src/view/`      | View        |
| 18   | `src/edit/`      | Edit        |
| 19   | `src/lua/`       | Lua         |
| 20   | `src/project/`   | Project     |

The Edit, Lua and Project layers build into their own libraries (`nmo_edit`, `nmo_lua`,
`nmo_project`) on top of the core library `nmo`, and Lua and Project build on Edit. The Engine
and block pack layers are libraries too (`nmo_engine`, `nmo_bb_*`), and so are the Render and
View layers (`nmo_render`, its backends, `nmo_view`), which are built for the CLI and not
installed. The core must
not depend on them: no core source or public header includes an `edit/`, `lua/` or `project/`
header (the audit checks both), and core tests link only `nmo`.

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

2. PR description should include:
   - What changed and why
   - Which issue(s) this addresses (if any)
   - Testing performed

3. Review process:
   - At least one maintainer approval required
   - All CI checks must pass
   - Address all review comments before merge

## Releases

1. Update the version in `include/nmo_types.h` and in `project()` in
   `CMakeLists.txt`, and turn the `CHANGELOG.md` notes into a
   `## [X.Y.Z] - YYYY-MM-DD` section.
2. Check the packages that CI attaches as artifacts to that commit's run.
3. Push a tag `vX.Y.Z` on that commit. CI checks that the tag matches both
   versions, builds and tests the packages, and publishes a GitHub release with
   them, a `SHA256SUMS` file, and the `CHANGELOG.md` section as notes.

`tools/scripts/package_release.py` builds one package locally. It runs the
test suite, installs into `dist/`, checks the CLI, its completions and data
lookup, builds `tests/package_consumer` against the package, and writes the
archive. Pass `--sample <file>` to also check that every plugin dependency of
a Virtools file resolves from the packaged data.

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
