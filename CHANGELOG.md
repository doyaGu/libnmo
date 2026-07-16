# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-07-14

### Added
- Lossless `nmo_ref_t` references with explicit resolved, unresolved, ambiguous,
  class-mismatch, and null states.
- Caller-owned load diagnostics and strict end-of-scan validation for recoverable
  schema failures.
- Explicit invalid-reference normalization for save-as workflows.
- Sectioned InterfaceChunk support for local/shared parameter sections and both
  graph mapping section pairs, including raw mapping tags.

### Changed
- All 43 built-in schema implementations propagate chunk read, skip, and write
  failures; a source audit test prevents ignored results from returning.
- Failed schema state is discarded while its original chunk remains available
  for an unchanged save.
- Unresolved IDs are preserved through legacy schema load/save paths instead of
  being silently encoded as null references.
- Grid layer ID/chunk lanes are represented as atomic `nmo_grid_layer_t` records.
- Behavior graph traversal detects cycles, and Behavior index rebuilds release
  their previous slot storage.

### Fixed
- Negative and impossible sequence/manager counts are rejected before allocation.
- Behavior dependency remapping no longer compacts serialized lanes.
- Sectioned InterfaceChunk writing preserves absent versus present-empty sections.

## [Unreleased] - 2026-xx-xx

### Added - Phase 8: Round-Trip Framework
- DOM comparison API (`nmo_comparison.h`): diff two loaded sessions at the object
  level; used by round-trip integration tests
- Round-trip test framework: loads a file, saves it to a memory buffer, reloads
  from buffer, and runs DOM comparison
- IntList auditor: debug-mode verifier that records every `StartIntList` write and
  asserts the correct count on `StopIntList`

### Added - Phase 8: Dual-Track IO and Reserve-and-Patch
- Dual-track IO: automatically selects mmap (zero-copy) or buffered file IO based
  on file size and OS capabilities
- Reserve-and-patch pattern: `nmo_chunk_reserve_dword()` writes a placeholder and
  `nmo_chunk_patch_dword()` fills in the real value after the size is known
- Transactional write (`nmo_txn`): platform-specific atomic-commit helpers
  (POSIX `fsync` / Windows `FlushFileBuffers`)

### Added - Phase 7: Save Pipeline and Core Infrastructure
- Two-phase commit save pipeline:
  - Phase 1 (Layout and Serialize): all objects serialized into memory chunks;
    ID mapping computed; shadow blobs restored
  - Phase 2 (Pack and Commit): file header written; optional zlib compression;
    CRC-32 appended; atomic fsync
- Chunk writer version context stack: 16-level nesting; parent version propagated
  automatically on push/pop
- ID sanitizer (`nmo_id_sanitizer_t`): strips `0x800000` reference marker,
  handles negative external-reference IDs, maintains bidirectional
  file-index <-> runtime-ID mapping; 6 unit tests
- Shadow storage (`nmo_shadow_storage_t`): retains included-files blob and raw
  chunk tail bytes so unknown data survives round-trips; 10 unit tests

### Changed
- Object layer: all 23 CK class schemas + 2 manager schemas migrated to
  explicit vtable dispatch.  No legacy bridge macros remain.
  `RuntimeFallback = none` for all registered types.
- Test count reached 102/102 (up from 88 at 1.4.0)

---

## [1.4.0] - 2025-12-20 - Phase 6 Completion (Type System)

### Added - Type System
- Type registry (`include/type/nmo_type_system.h`):
  - GUID-first type identification with O(1) hash lookups
  - Support for primitives, enums, flags, structs, and manager types
  - Type inheritance via parent GUID chaining
  - UI visibility flags and type categories
  - Arena-based allocation; slot recycling; Tortoise-Hare cycle detection
- Enum/flags registration:
  - `nmo_type_registry_register_enum()` -- named value enumerations
  - `nmo_type_registry_register_flags()` -- bitfield flag types
  - Value-to-name and name-to-value conversion APIs
  - Combined flags string (e.g. `FLAG_A|FLAG_B`)
- Operation registry (`include/type/nmo_operations.h`):
  - 4D dispatch tree: Operation -> P1 type -> P2 type -> result type
  - 50+ builtin operations: arithmetic, logic, bitwise, trig, vector
- String conversion (`include/type/nmo_type_string.h`):
  - `nmo_type_to_string()` / `nmo_type_from_string()` generic conversion
  - Built-in formatters for primitives, vectors, colors
- Manager type descriptors:
  - `nmo_manager_type_descriptor_t` for custom manager serialization
  - Serialize/deserialize callbacks integrated with the chunk API
- Reflection (`include/type/nmo_reflection.h`): struct field introspection

### Added - Object Layer (Phase 6.1)
- Object index system (`include/object/nmo_object_index.h`):
  - O(1) lookup by class ID, name, or GUID (incremental, add/remove)
  - Index rebuild and statistics APIs
- Object repository (`include/object/nmo_object_repository.h`):
  - Dual-index: `nmo_indexed_map_t` + name hash table
  - Move-semantics `add` (sets `*obj_ref = NULL` on success)
  - Runtime ID allocation with wraparound

### Added - Extension Layer
- Extension registry (`include/extension/nmo_extension_registry.h`):
  owns all plugins; supports static and DLL-based registration
- Extension host ABI (`include/extension/nmo_extension_host.h`)
- Extension diagnostics (`include/extension/nmo_extension_diagnostics.h`)
- Extension loader (`include/extension/nmo_extension_loader.h`):
  loads `.dll`/`.so` plugins; enforces unregister-before-unload rule

### Changed
- `nmo_context_t` now uses `nmo_type_registry_t` internally
- Deprecated `nmo_schema_registry_t` (Builder-pattern API); see MIGRATION_GUIDE_V2.md

### Fixed
- Context initialization: type registry properly created in `nmo_context_create()`
- Removed tests for non-existent data files (`Empty.cmo`, `Empty.vmo`)

### Tests
- 88/88 tests passing at release time

---

## [1.3.1] - 2025-12-05 - Bug Fixes and Streaming IO

### Fixed
- vtable write functions missing `arena` parameter (P0 issue):
  - Added `arena` to all 23 vtable write function signatures
  - Removed 4 NULL-arena workarounds in mesh, light, camera, 3d-entity schemas
  - Updated `nmo_schema_write_struct()` to accept the arena parameter

### Added
- Session-level chunk pool wiring: chunks allocated during load reuse
  `nmo_chunk_pool_t`, improving memory locality
- `nmo_data_section_parse()` accepts an optional chunk pool parameter
- Chunk compression APIs: `nmo_chunk_compress()`,
  `nmo_chunk_compress_if_beneficial()`, `nmo_chunk_decompress()`
  (replace older pack/unpack helpers; backward compatible)
- Streaming IO subsystem (`include/io/nmo_io_stream.h`): incremental
  reader/writer for large data sections; configurable buffer sizes;
  transparent (de)compression; streaming writer patches file header on finalize
- `nmo_string_t` -- dynamic string container + `nmo_string_view_t` helpers
  providing XString-compatible behaviors (assign, append/insert, replace,
  search, case conversion, printf-style formatting, numeric conversions)

### Tests
- `test_data_roundtrip`: added `parse_with_chunk_pool` case
- `test_chunk_api`: extended with compression API coverage
- `test_stream_io`: round-trip streaming save/load (compressed and uncompressed)
- `test_string`: full string API coverage

---

## [1.3.0] - 2025-11-12 - Utility Refactoring and Enhanced Chunk Features

### Added - Core Utility Library
- `nmo_utils.h` -- unified utility library:
  - Alignment: `nmo_align_dword()`, `nmo_align()`, `nmo_bytes_to_dwords()`
  - Byte order: `nmo_bswap16/32/64()`, `nmo_le*toh()`, `nmo_htole*()`
  - Little-endian read/write: `nmo_read_u*_le()`, `nmo_write_u*_le()`
  - Min/max/clamp: `NMO_MIN`, `NMO_MAX`, `nmo_clamp_*()`
  - Buffer bounds: `nmo_check_buffer_bounds()`, `NMO_CHECK_BUFFER_SIZE`

### Added - Enhanced Chunk Features
- True 16-bit endian conversion (real byte swap, not just aliases):
  - `nmo_chunk_parser_read_array_lendian16()` with real word swap
  - `nmo_chunk_writer_write_array_lendian16()` with real word swap
- Complete math type read/write (Vector2, Vector3, Vector4, Matrix4x4,
  Quaternion, Color) -- all verified with round-trip tests
- Deep chunk cloning in `nmo_chunk_clone()`: recursive sub-chunk copy,
  independent data/ID/manager buffer copies
- `nmo_chunk_parser_seek_identifier_with_size()`: returns size until next
  identifier (matches `CKStateChunk::SeekIdentifierAndReturnSize()`)

### Changed
- Refactored 7+ source files to use `nmo_utils.h` instead of local duplicates
- Simplified `chunk_internal.h` to a thin wrapper

### Tests
- `tests/unit/test_chunk_advanced.c`: 9/9 tests passing

---

## [1.2.0] - 2025-11-12 - Phase 5: Object Indexing and Performance

### Added - Object Indexing System
- `include/object/nmo_object_index.h`: class-ID, name, and GUID indexes for
  O(1) lookup; incremental add/remove; index rebuild; statistics API

### Added - Hash Table and Arena Enhancements
- `nmo_hash_table_reserve()` / `nmo_hash_table_get_capacity()` for
  pre-allocation; power-of-2 capacity rounding
- `nmo_arena_config_t` + `nmo_arena_create_ex()`: configurable block size,
  growth factor, alignment
- `nmo_arena_reserve()` for bulk pre-allocation

### Performance
- Object lookup by class:  50-100x faster (O(n) -> O(1))
- Object lookup by name:   100-200x faster (O(n) -> O(1))
- Object lookup by GUID:   50-150x faster (O(n) -> O(1))
- Arena allocation:        5-10x faster
- Hash table bulk insert:  30-50% faster with reserve
- Memory overhead:         20-30% for 50-200x performance gain

### Tests
- 7/7 unit tests passing

---

## [1.0.0] - TBD - First Stable Release

First stable release is targeted after the Phase 3 (Production Ready) milestone:
- Chunk read bounds checking (no crash on truncated input)
- Round-trip coverage: 50+ files, 23/23 object types, >=99% pass rate
- CI pipeline: Windows / Linux / macOS, PR-triggered

See TODO.md for current Phase 3 milestone definitions (M3.0, M3.1, M3.2).
