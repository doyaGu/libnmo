# libnmo Development Checklist

**Status**: In Progress
**Last Updated**: 2025-11-10

This checklist tracks the implementation progress of libnmo. Check off items as they are completed.

---

## Phase 1: Project Setup (Days 1-2)

### Build System
- [ ] Create root `CMakeLists.txt`
- [ ] Configure project metadata (name, version, languages)
- [ ] Set C standard to C17
- [ ] Add build options (tests, tools, examples, SIMD)
- [ ] Find and link zlib dependency
- [ ] Configure compiler flags (-Wall -Wextra -Werror)
- [ ] Set up installation rules

### Directory Structure
- [ ] Create `include/` directory
- [ ] Create `include/core/` subdirectory
- [ ] Create `include/io/` subdirectory
- [ ] Create `include/format/` subdirectory
- [ ] Create `include/schema/` subdirectory
- [ ] Create `include/session/` subdirectory
- [ ] Create `include/app/` subdirectory
- [ ] Create `src/` directory (mirror include structure)
- [ ] Create `tests/` directory (unit, integration, fuzz)
- [ ] Create `tools/` directory
- [ ] Create `examples/` directory
- [ ] Create `docs/` directory (already exists)

### Documentation
- [ ] Update README.md with build instructions
- [ ] Create CONTRIBUTING.md
- [ ] Create LICENSE file
- [ ] Create CHANGELOG.md

### CI/CD (Optional)
- [ ] Set up GitHub Actions / GitLab CI
- [ ] Configure build matrix (Linux, macOS, Windows)
- [ ] Configure test runner
- [ ] Configure code coverage
- [ ] Configure static analysis (cppcheck, clang-tidy)

---

## Phase 2: Core Layer (Days 3-6)

### Allocator (Day 3)
- [ ] Create `include/core/nmo_allocator.h`
- [ ] Define `nmo_alloc_fn` and `nmo_free_fn` types
- [ ] Define `nmo_allocator` structure
- [ ] Implement `nmo_allocator_default()` (malloc/free wrapper)
- [ ] Implement `nmo_allocator_custom()` factory
- [ ] Add alignment support
- [ ] Create `src/core/allocator.c`
- [ ] Write unit tests in `tests/unit/test_allocator.c`
- [ ] Test custom allocator
- [ ] Test aligned allocation

### Arena (Day 3)
- [ ] Create `include/core/nmo_arena.h`
- [ ] Define `nmo_arena` opaque type
- [ ] Declare `nmo_arena_create()`
- [ ] Declare `nmo_arena_alloc()`
- [ ] Declare `nmo_arena_reset()`
- [ ] Declare `nmo_arena_destroy()`
- [ ] Create `src/core/arena.c`
- [ ] Implement bump-pointer allocation
- [ ] Implement automatic growth (64KB chunks)
- [ ] Implement alignment handling
- [ ] Write unit tests in `tests/unit/test_arena.c`
- [ ] Test allocation patterns
- [ ] Test reset behavior
- [ ] Benchmark performance

### Error System (Day 4)
- [ ] Create `include/core/nmo_error.h`
- [ ] Define `nmo_error_code` enum (all error codes)
- [ ] Define `nmo_severity` enum
- [ ] Define `nmo_error` structure with causal chain
- [ ] Define `nmo_result` structure
- [ ] Declare `nmo_error_create()`
- [ ] Declare `nmo_error_add_cause()`
- [ ] Create `src/core/error.c`
- [ ] Implement error creation with arena allocation
- [ ] Implement error chain linking
- [ ] Add helper macros (NMO_ERROR, NMO_RETURN_IF_ERROR)
- [ ] Write unit tests in `tests/unit/test_error.c`
- [ ] Test error chains
- [ ] Test severity filtering

### Logger (Day 4)
- [ ] Create `include/core/nmo_logger.h`
- [ ] Define `nmo_log_level` enum
- [ ] Define `nmo_log_fn` callback type
- [ ] Define `nmo_logger` structure
- [ ] Declare `nmo_log()` family of functions
- [ ] Declare `nmo_logger_stderr()`
- [ ] Declare `nmo_logger_null()`
- [ ] Create `src/core/logger.c`
- [ ] Implement stderr logger
- [ ] Implement null logger
- [ ] Implement level filtering
- [ ] Implement variadic formatting
- [ ] Write unit tests in `tests/unit/test_logger.c`
- [ ] Test log levels
- [ ] Test custom loggers

### GUID (Day 5)
- [ ] Create `include/core/nmo_guid.h`
- [ ] Define `nmo_guid` structure (d1, d2)
- [ ] Declare `nmo_guid_equals()`
- [ ] Declare `nmo_guid_hash()`
- [ ] Declare `nmo_guid_parse()`
- [ ] Declare `nmo_guid_format()`
- [ ] Create `src/core/guid.c`
- [ ] Implement equality comparison
- [ ] Implement hash function (for hash tables)
- [ ] Implement string parsing (hex format)
- [ ] Implement string formatting
- [ ] Write unit tests in `tests/unit/test_guid.c`
- [ ] Test parsing/formatting round-trip
- [ ] Test hash distribution

### Integration (Day 6)
- [ ] Create umbrella header `include/nmo_types.h`
- [ ] Define common types (nmo_object_id, nmo_class_id, etc.)
- [ ] Run all core unit tests
- [ ] Fix any memory leaks (valgrind)
- [ ] Run static analysis
- [ ] Update documentation

---

## Phase 3: IO Layer (Days 7-11)

### Base IO Interface (Day 7)
- [ ] Create `include/io/nmo_io.h`
- [ ] Define `nmo_io_mode` enum
- [ ] Define `nmo_io_read_fn`, `nmo_io_write_fn`, etc.
- [ ] Define `nmo_io_interface` structure
- [ ] Declare `nmo_io_close()`
- [ ] Create `src/io/io.c`
- [ ] Implement `nmo_io_close()`
- [ ] Add error handling helpers

### File IO (Day 7-8)
- [ ] Create `include/io/nmo_io_file.h`
- [ ] Declare `nmo_file_io_open()`
- [ ] Create `src/io/io_file.c`
- [ ] Implement POSIX version (fopen, fread, fwrite, fseek, ftell, fclose)
- [ ] Implement large file support (>2GB)
- [ ] Add Windows-specific version (`io_file_windows.c`)
- [ ] Implement Windows version (CreateFile, ReadFile, WriteFile)
- [ ] Write unit tests in `tests/unit/test_io_file.c`
- [ ] Test read/write operations
- [ ] Test seek/tell operations
- [ ] Test error conditions (file not found, permissions)

### Memory IO (Day 8)
- [ ] Create `include/io/nmo_io_memory.h`
- [ ] Declare `nmo_memory_io_open_read()`
- [ ] Declare `nmo_memory_io_open_write()`
- [ ] Declare `nmo_memory_io_get_data()`
- [ ] Create `src/io/io_memory.c`
- [ ] Implement read-only memory buffer
- [ ] Implement write buffer with dynamic growth
- [ ] Implement data extraction
- [ ] Write unit tests in `tests/unit/test_io_memory.c`
- [ ] Test read operations
- [ ] Test write operations with growth
- [ ] Test data extraction

### Compressed IO (Day 9-10)
- [ ] Create `include/io/nmo_io_compressed.h`
- [ ] Define `nmo_compression_codec` enum
- [ ] Define `nmo_compressed_io_desc` structure
- [ ] Declare `nmo_compressed_io_wrap()`
- [ ] Create `src/io/io_compressed.c`
- [ ] Implement zlib compression wrapper
- [ ] Implement zlib decompression wrapper
- [ ] Implement streaming mode with buffering
- [ ] Handle compression levels (1-9)
- [ ] Write unit tests in `tests/unit/test_io_compressed.c`
- [ ] Test compression/decompression
- [ ] Test streaming mode
- [ ] Test different compression levels

### Checksum IO (Day 10)
- [ ] Create `include/io/nmo_io_checksum.h`
- [ ] Define `nmo_checksum_algorithm` enum
- [ ] Define `nmo_checksummed_io_desc` structure
- [ ] Declare `nmo_checksummed_io_wrap()`
- [ ] Declare `nmo_checksummed_io_get_checksum()`
- [ ] Create `src/io/io_checksum.c`
- [ ] Implement Adler-32 wrapper (zlib)
- [ ] Implement CRC32 wrapper (optional)
- [ ] Track checksum during read/write
- [ ] Write unit tests in `tests/unit/test_io_checksum.c`
- [ ] Test checksum computation
- [ ] Verify against known checksums

### Transactional IO (Day 11)
- [ ] Create `include/io/nmo_txn.h`
- [ ] Define `nmo_txn_durability` enum
- [ ] Define `nmo_txn_desc` structure
- [ ] Define `nmo_txn_handle` opaque type
- [ ] Declare `nmo_txn_open()`
- [ ] Declare `nmo_txn_write()`
- [ ] Declare `nmo_txn_commit()`
- [ ] Declare `nmo_txn_rollback()`
- [ ] Declare `nmo_txn_close()`
- [ ] Create `src/io/txn_posix.c`
- [ ] Implement temporary file creation
- [ ] Implement atomic rename on commit
- [ ] Implement fsync/fdatasync support
- [ ] Implement rollback (delete staging file)
- [ ] Create `src/io/txn_windows.c` for Windows
- [ ] Write unit tests in `tests/unit/test_txn.c`
- [ ] Test commit behavior
- [ ] Test rollback behavior
- [ ] Test crash recovery

### Integration (Day 11)
- [ ] Test IO stack composition (File → Checksum → Compression)
- [ ] Integration test: Write compressed file, verify checksum, read back
- [ ] Run all IO unit tests
- [ ] Check for memory leaks
- [ ] Update documentation

---

## Phase 4: Format Layer - Headers (Days 12-15)

### File Header (Day 12)
- [ ] Create `include/format/nmo_header.h`
- [ ] Define `nmo_file_header` structure (Part0 + Part1)
- [ ] Declare `nmo_header_parse()`
- [ ] Declare `nmo_header_validate()`
- [ ] Declare `nmo_header_serialize()`
- [ ] Create `src/format/header.c`
- [ ] Implement signature check ("Nemo Fi\0")
- [ ] Implement version check (2 ≤ version ≤ 9)
- [ ] Implement Part0 parsing (32 bytes)
- [ ] Implement Part1 parsing (32 bytes, v5+)
- [ ] Implement header serialization
- [ ] Write unit tests in `tests/unit/test_header.c`
- [ ] Test signature validation
- [ ] Test version checks
- [ ] Test Part0/Part1 parsing

### Header1 (Day 13-14)
- [ ] Create `include/format/nmo_header1.h`
- [ ] Define `nmo_object_desc` structure
- [ ] Define `nmo_plugin_dep` structure
- [ ] Define `nmo_header1` structure
- [ ] Declare `nmo_header1_parse()`
- [ ] Declare `nmo_header1_serialize()`
- [ ] Create `src/format/header1.c`
- [ ] Implement object descriptor parsing
- [ ] Implement plugin dependency parsing
- [ ] Implement included file parsing (stub)
- [ ] Implement Header1 serialization
- [ ] Handle reference-only objects (bit 23)
- [ ] Write unit tests in `tests/unit/test_header1.c`
- [ ] Test object descriptor parsing
- [ ] Test plugin dependency parsing
- [ ] Test round-trip serialization

### Object Metadata (Day 14)
- [ ] Create `include/format/nmo_object.h`
- [ ] Define `nmo_object` structure
- [ ] Define object flags enum
- [ ] Declare object creation/destruction functions
- [ ] Create `src/format/object.c`
- [ ] Implement object allocation
- [ ] Implement hierarchy management (parent/children)
- [ ] Write unit tests in `tests/unit/test_object.c`

### Manager Metadata (Day 15)
- [ ] Create `include/format/nmo_manager.h`
- [ ] Define `nmo_manager` structure
- [ ] Define manager hook types
- [ ] Define `nmo_plugin_category` enum
- [ ] Create `src/format/manager.c`
- [ ] Document manager hook contract
- [ ] Write example manager implementation
- [ ] Write unit tests in `tests/unit/test_manager.c`

### Integration (Day 15)
- [ ] Run all format unit tests
- [ ] Test header + Header1 interaction
- [ ] Update documentation

---

## Phase 5: CKStateChunk (Days 16-21)

### Chunk Structure (Day 16)
- [ ] Create `include/format/nmo_chunk.h`
- [ ] Define `nmo_chunk` structure
- [ ] Define `nmo_chunk_options` enum
- [ ] Define `CHUNK_VERSION4` constant (value 7)
- [ ] Define tracking list structures
- [ ] Create `src/format/chunk.c`
- [ ] Implement chunk allocation
- [ ] Implement data buffer management
- [ ] Implement `CheckSize()` growth logic (500 DWORD increments)

### Chunk Parser (Day 17-18)
- [ ] Create `include/format/nmo_chunk_parser.h`
- [ ] Define `nmo_chunk_parser` opaque type
- [ ] Declare parser lifecycle functions
- [ ] Declare cursor management functions (tell, seek, skip)
- [ ] Declare primitive read functions (byte, word, dword, int, float, guid)
- [ ] Declare complex read functions (bytes, string, buffer)
- [ ] Declare object ID reading
- [ ] Declare identifier system
- [ ] Create `src/format/chunk_parser.c`
- [ ] Implement parser creation
- [ ] Implement bounds checking
- [ ] Implement primitive reads with DWORD alignment
- [ ] Implement string reading (length-prefixed, null-terminated)
- [ ] Implement buffer reading
- [ ] Implement object ID tracking
- [ ] Implement identifier seek/read
- [ ] Write unit tests in `tests/unit/test_chunk_parser.c`
- [ ] Test all primitive types
- [ ] Test string reading
- [ ] Test bounds checking
- [ ] Test identifier navigation

### Chunk Writer (Day 18-19)
- [ ] Create `include/format/nmo_chunk_writer.h`
- [ ] Define `nmo_chunk_writer` opaque type
- [ ] Declare writer lifecycle functions
- [ ] Declare `nmo_chunk_writer_start()`
- [ ] Declare primitive write functions (byte, word, dword, int, float, guid)
- [ ] Declare complex write functions (bytes, string, buffer)
- [ ] Declare object ID writing
- [ ] Declare sequence functions (object, manager)
- [ ] Declare sub-chunk functions
- [ ] Declare identifier writing
- [ ] Declare `nmo_chunk_writer_finalize()`
- [ ] Create `src/format/chunk_writer.c`
- [ ] Implement writer creation
- [ ] Implement automatic buffer growth
- [ ] Implement primitive writes with DWORD alignment
- [ ] Implement string writing (length-prefixed, padded)
- [ ] Implement buffer writing
- [ ] Implement object ID tracking
- [ ] Implement sequence tracking
- [ ] Implement sub-chunk tracking
- [ ] Implement identifier linking
- [ ] Implement finalization
- [ ] Write unit tests in `tests/unit/test_chunk_writer.c`
- [ ] Test all primitive types
- [ ] Test string writing
- [ ] Test buffer growth
- [ ] Test identifier chains

### Chunk Serialization (Day 20)
- [ ] Create `include/format/nmo_chunk_serialize.h`
- [ ] Declare `nmo_chunk_serialize()`
- [ ] Declare `nmo_chunk_deserialize()`
- [ ] Create `src/format/chunk_serialize.c`
- [ ] Implement version info packing (4-byte format)
- [ ] Implement `ConvertToBuffer()` logic
- [ ] Implement optional list serialization (IDs, chunks, managers)
- [ ] Implement `ConvertFromBuffer()` logic
- [ ] Handle legacy chunk versions (4, 5, 6)
- [ ] Write unit tests in `tests/unit/test_chunk_serialize.c`
- [ ] Test version info packing/unpacking
- [ ] Test optional lists
- [ ] Test legacy format compatibility

### ID Remapping (Day 21)
- [ ] Declare `nmo_chunk_remap_ids()`
- [ ] Implement chunk ID remapping
- [ ] Implement recursive sub-chunk remapping
- [ ] Implement manager int remapping
- [ ] Write unit tests in `tests/unit/test_chunk_remap.c`
- [ ] Test simple remapping
- [ ] Test nested chunk remapping

### Integration (Day 21)
- [ ] Round-trip test: Writer → Serialize → Deserialize → Parser
- [ ] Test complex chunk structures
- [ ] Test with real object data
- [ ] Benchmark performance
- [ ] Run all chunk tests
- [ ] Check for memory leaks
- [ ] Update documentation

---

## Phase 6: Schema System (Days 22-25)

### Schema Descriptors (Day 22)
- [ ] Create `include/schema/nmo_schema.h`
- [ ] Define `nmo_field_type` enum
- [ ] Define `nmo_field_descriptor` structure
- [ ] Define `nmo_schema_descriptor` structure
- [ ] Create `src/schema/schema.c`
- [ ] Implement field descriptor utilities
- [ ] Write unit tests

### Schema Registry (Day 23)
- [ ] Create `include/schema/nmo_schema_registry.h`
- [ ] Define `nmo_schema_registry` opaque type
- [ ] Declare `nmo_schema_registry_create()`
- [ ] Declare `nmo_schema_registry_add()`
- [ ] Declare `nmo_schema_registry_add_builtin()`
- [ ] Declare `nmo_schema_registry_find_by_id()`
- [ ] Declare `nmo_schema_registry_find_by_name()`
- [ ] Declare `nmo_verify_schema_consistency()`
- [ ] Create `src/schema/schema_registry.c`
- [ ] Implement hash table for ID → schema
- [ ] Implement hash table for name → schema
- [ ] Implement thread-safe access (RW locks)
- [ ] Implement consistency checks
- [ ] Write unit tests in `tests/unit/test_schema_registry.c`
- [ ] Test registration
- [ ] Test lookup
- [ ] Test thread safety

### Built-in Schemas (Day 24)
- [ ] Create `src/builtin_schemas/nmo_builtin_schemas.c`
- [ ] Create `include/nmo_builtin_schemas.h`
- [ ] Implement CKObject schema (0x00000001)
- [ ] Implement CKBeObject schema (0x00000002)
- [ ] Implement CKSceneObject schema (0x00000003)
- [ ] Implement CK3dEntity schema (0x00000004)
- [ ] Implement CK3dObject schema (0x00000005)
- [ ] Implement CKCamera schema (0x00000006)
- [ ] Implement CKLight schema (0x00000007)
- [ ] Implement CKMesh schema (0x00000008)
- [ ] Implement CKMaterial schema (0x00000009)
- [ ] Implement CKTexture schema (0x0000000A)
- [ ] Implement CKBehavior schema (0x00000014)
- [ ] Implement CKParameter schema (0x00000015)
- [ ] Test all schemas
- [ ] Document schema format

### Validator (Day 25)
- [ ] Create `include/schema/nmo_validator.h`
- [ ] Define `nmo_validation_mode` enum
- [ ] Define `nmo_validation_result` enum
- [ ] Declare `nmo_validation_create()`
- [ ] Declare `nmo_validate_object()`
- [ ] Declare `nmo_validate_file()`
- [ ] Create `src/schema/validator.c`
- [ ] Implement field validation
- [ ] Implement reference validation
- [ ] Implement strict vs permissive modes
- [ ] Write unit tests

### Migrator (Day 25)
- [ ] Create `include/schema/nmo_migrator.h`
- [ ] Declare `nmo_migrator_create()`
- [ ] Declare `nmo_migrate_chunk()`
- [ ] Create `src/schema/migrator.c`
- [ ] Implement version detection
- [ ] Implement basic migration strategies
- [ ] Write unit tests

### Integration (Day 25)
- [ ] Test schema registration and lookup
- [ ] Test validation with real objects
- [ ] Run all schema tests
- [ ] Update documentation

---

## Phase 7: Object System (Days 26-30)

### Object Repository (Day 26-27)
- [ ] Create `include/session/nmo_object_repository.h`
- [ ] Declare `nmo_object_repository_create()`
- [ ] Declare `nmo_object_create()`
- [ ] Declare `nmo_object_find_by_id()`
- [ ] Declare `nmo_object_find_by_name()`
- [ ] Declare `nmo_object_find_by_class()`
- [ ] Create `src/session/object_repository.c`
- [ ] Implement ID → object hash table
- [ ] Implement name → object hash table
- [ ] Implement per-class arrays
- [ ] Implement ID allocation
- [ ] Write unit tests in `tests/unit/test_object_repository.c`
- [ ] Test object creation
- [ ] Test lookups
- [ ] Test ID uniqueness

### Load Session (Day 28)
- [ ] Create `include/session/nmo_load_session.h`
- [ ] Define `nmo_load_session` structure
- [ ] Declare `nmo_load_session_start()`
- [ ] Declare `nmo_load_session_register()`
- [ ] Declare `nmo_load_session_end()`
- [ ] Create `src/session/load_session.c`
- [ ] Implement ID reservation
- [ ] Implement file ID → runtime ID mapping
- [ ] Implement session cleanup
- [ ] Write unit tests

### ID Remapping (Day 29-30)
- [ ] Create `include/session/nmo_id_remap.h`
- [ ] Define `nmo_id_remap_table` structure
- [ ] Define `nmo_id_remap_plan` structure
- [ ] Declare `nmo_build_remap_table()` (for load)
- [ ] Declare `nmo_id_remap_lookup()`
- [ ] Declare `nmo_id_remap_plan_create()` (for save)
- [ ] Create `src/session/id_remap.c`
- [ ] Implement load-time remapping (file → runtime)
- [ ] Implement save-time remapping (runtime → sequential)
- [ ] Handle reference-only objects (bit 23)
- [ ] Handle negative IDs (external references)
- [ ] Write unit tests in `tests/unit/test_id_remap.c`
- [ ] Test simple remapping
- [ ] Test complex graphs
- [ ] Test reference-only objects

### Integration (Day 30)
- [ ] Integration test: Create objects, save, load, verify IDs
- [ ] Test with complex object hierarchies
- [ ] Run all object system tests
- [ ] Update documentation

---

## Phase 8: Session Layer (Days 31-34)

### Context (Day 31-32)
- [ ] Create `include/app/nmo_context.h`
- [ ] Define `nmo_context_desc` structure
- [ ] Define `nmo_context` opaque type
- [ ] Declare `nmo_context_create()`
- [ ] Declare `nmo_context_retain()`
- [ ] Declare `nmo_context_release()`
- [ ] Declare registry getters
- [ ] Create `src/app/context.c`
- [ ] Implement reference counting
- [ ] Implement thread-safe retain/release
- [ ] Create schema registry
- [ ] Create manager registry
- [ ] Implement context destruction
- [ ] Write unit tests in `tests/unit/test_context.c`
- [ ] Test reference counting
- [ ] Test thread safety
- [ ] Test registry access

### Session (Day 33-34)
- [ ] Create `include/app/nmo_session.h`
- [ ] Define `nmo_session` opaque type
- [ ] Define `nmo_file_info` structure
- [ ] Declare `nmo_session_create()`
- [ ] Declare `nmo_session_get_file_info()`
- [ ] Declare `nmo_session_destroy()`
- [ ] Create `src/app/session.c`
- [ ] Implement session creation (arena allocation)
- [ ] Create object repository
- [ ] Borrow context reference
- [ ] Implement file info tracking
- [ ] Implement session destruction
- [ ] Write unit tests in `tests/unit/test_session.c`
- [ ] Test session lifecycle
- [ ] Test arena cleanup

### Integration (Day 34)
- [ ] Test context + session integration
- [ ] Test multiple sessions per context
- [ ] Run all session tests
- [ ] Update documentation

---

## Phase 9: Load Pipeline (Days 35-41)

### Parser Structure (Day 35)
- [ ] Create `include/session/nmo_parser.h`
- [ ] Define `nmo_load_flags` enum
- [ ] Declare `nmo_load_file()`
- [ ] Create `src/session/parser.c`
- [ ] Set up basic structure
- [ ] Implement error handling

### Phase 1-5: Open and Parse Headers (Day 36)
- [ ] Implement Phase 1: Open IO
- [ ] Implement Phase 2: Parse file header
- [ ] Implement Phase 3: Read and decompress Header1
- [ ] Implement Phase 4: Parse Header1
- [ ] Implement Phase 5: Start load session
- [ ] Test header parsing
- [ ] Test decompression

### Phase 6-8: Dependencies and Data (Day 37)
- [ ] Implement Phase 6: Check plugin dependencies
- [ ] Implement Phase 7: Manager pre-load hooks
- [ ] Implement Phase 8: Read and decompress data section
- [ ] Test plugin validation
- [ ] Test manager hooks
- [ ] Test data decompression

### Phase 9-11: Parse Chunks (Day 38)
- [ ] Implement Phase 9: Parse manager chunks
- [ ] Implement Phase 10: Create objects
- [ ] Implement Phase 11: Parse object chunks
- [ ] Test manager chunk parsing
- [ ] Test object creation
- [ ] Test chunk storage

### Phase 12-13: ID Remapping (Day 39)
- [ ] Implement Phase 12: Build ID remap table
- [ ] Implement Phase 13: Remap IDs in all chunks
- [ ] Test remap table construction
- [ ] Test ID remapping
- [ ] Test recursive remapping

### Phase 14-15: Deserialize and Finalize (Day 40)
- [ ] Implement Phase 14: Deserialize objects using schemas
- [ ] Implement Phase 15: Manager post-load hooks
- [ ] Test object deserialization
- [ ] Test manager post-load
- [ ] Test complete pipeline

### Integration and Testing (Day 41)
- [ ] Integration test: Load complete Virtools file
- [ ] Test all load flags
- [ ] Test error recovery
- [ ] Test with various file versions (v2-v9)
- [ ] Performance benchmarks
- [ ] Memory leak checks
- [ ] Update documentation

---

## Phase 10: Save Pipeline (Days 42-47)

### Builder Structure (Day 42)
- [ ] Create `include/session/nmo_builder.h`
- [ ] Define `nmo_save_options` structure
- [ ] Define `nmo_save_builder` opaque type
- [ ] Declare `nmo_save_builder_create()`
- [ ] Declare `nmo_save_builder_add()`
- [ ] Declare `nmo_save_builder_add_all()`
- [ ] Declare `nmo_save_builder_set_options()`
- [ ] Declare `nmo_save_builder_execute()`
- [ ] Create `src/session/builder.c`
- [ ] Set up basic structure

### Phase 1-3: Preparation and Managers (Day 43)
- [ ] Implement Phase 1: Manager pre-save hooks
- [ ] Implement Phase 2: Build ID remap plan
- [ ] Implement Phase 3: Serialize manager chunks
- [ ] Test manager pre-save
- [ ] Test ID remap plan
- [ ] Test manager serialization

### Phase 4-5: Serialize Objects and Build Header1 (Day 44)
- [ ] Implement Phase 4: Serialize object chunks using schemas
- [ ] Implement Phase 5: Build Header1
- [ ] Test object serialization
- [ ] Test schema invocation
- [ ] Test Header1 construction

### Phase 6-8: Buffers and Compression (Day 45)
- [ ] Implement Phase 6: Serialize Header1
- [ ] Implement Phase 7: Combine data buffers (managers + objects)
- [ ] Implement Phase 8: Apply compression
- [ ] Test buffer combination
- [ ] Test compression
- [ ] Test compression levels

### Phase 9-11: Header and Checksum (Day 46)
- [ ] Implement Phase 9: Build file header (Part0/Part1)
- [ ] Implement Phase 10: Compute Adler-32 checksum
- [ ] Implement Phase 11: Open transaction
- [ ] Test header construction
- [ ] Test checksum computation
- [ ] Test transaction opening

### Phase 12-14: Write and Finalize (Day 47)
- [ ] Implement Phase 12: Write data (header + Header1 + data)
- [ ] Implement Phase 13: Commit transaction
- [ ] Implement Phase 14: Manager post-save hooks
- [ ] Test transactional write
- [ ] Test commit/rollback
- [ ] Test manager post-save

### Integration and Testing (Day 47)
- [ ] Integration test: Round-trip (load → save → load)
- [ ] Test all save options
- [ ] Test compression modes
- [ ] Test error handling
- [ ] Performance benchmarks
- [ ] Update documentation

---

## Phase 11: Manager System (Days 48-50)

### Manager Registry (Day 48)
- [ ] Create `include/format/nmo_manager_registry.h`
- [ ] Declare `nmo_manager_registry_create()`
- [ ] Declare `nmo_manager_register()`
- [ ] Declare `nmo_manager_find_by_guid()`
- [ ] Create `src/format/manager_registry.c`
- [ ] Implement GUID → manager hash table
- [ ] Implement registration
- [ ] Write unit tests

### Example Managers (Day 49-50)
- [ ] Implement Render Manager example
- [ ] Implement Attribute Manager example
- [ ] Implement Time Manager example
- [ ] Test manager hooks
- [ ] Test manager data serialization
- [ ] Document manager API
- [ ] Create manager implementation guide

### Integration (Day 50)
- [ ] Test manager registration
- [ ] Test manager lifecycle (pre/post load/save)
- [ ] Test manager data persistence
- [ ] Update documentation

---

## Phase 12: Testing Infrastructure (Days 51-56)

### Test Framework (Day 51)
- [ ] Create `tests/test_framework.h`
- [ ] Implement `TEST()` macro
- [ ] Implement assertion macros (ASSERT_EQ, ASSERT_NE, etc.)
- [ ] Implement test runner
- [ ] Implement test discovery
- [ ] Add colored output
- [ ] Add timing

### Unit Tests (Day 52-53)
- [ ] Verify all unit tests are written
- [ ] Ensure >80% code coverage
- [ ] Run coverage report (gcov/lcov)
- [ ] Fix any uncovered code paths
- [ ] Add missing tests

### Integration Tests (Day 54)
- [ ] Round-trip test suite
- [ ] Version migration tests (v2 → v8)
- [ ] Complex object graph tests
- [ ] Manager hook integration tests
- [ ] Error recovery tests
- [ ] Create test fixtures (real Virtools files)

### Fuzz Tests (Day 55)
- [ ] Set up fuzzing infrastructure (AFL/libFuzzer)
- [ ] Fuzz header parser
- [ ] Fuzz Header1 parser
- [ ] Fuzz chunk parser
- [ ] Fuzz decompression
- [ ] Run 24-hour fuzz campaign
- [ ] Fix any crashes found

### CI/CD (Day 56)
- [ ] Set up automated test runs
- [ ] Configure coverage reporting
- [ ] Configure static analysis
- [ ] Configure valgrind checks
- [ ] Create test report generation
- [ ] Document testing procedures

---

## Phase 13: CLI Tools (Days 57-61)

### nmo-inspect (Day 57-58)
- [ ] Create `tools/nmo-inspect.c`
- [ ] Implement argument parsing
- [ ] Implement text output format
- [ ] Implement JSON output format
- [ ] Implement verbose mode
- [ ] Display file header info
- [ ] Display object list
- [ ] Display manager list
- [ ] Display plugin dependencies
- [ ] Add error reporting
- [ ] Write man page
- [ ] Test with various files

### nmo-validate (Day 59)
- [ ] Create `tools/nmo-validate.c`
- [ ] Implement argument parsing
- [ ] Implement validation modes (strict/permissive)
- [ ] Check file signature
- [ ] Verify checksum
- [ ] Validate schemas
- [ ] Check plugin dependencies
- [ ] Check object references
- [ ] Generate validation report
- [ ] Write man page
- [ ] Test with valid/invalid files

### nmo-convert (Day 60)
- [ ] Create `tools/nmo-convert.c`
- [ ] Implement argument parsing
- [ ] Implement version migration
- [ ] Implement compression toggle
- [ ] Implement format options
- [ ] Add progress reporting
- [ ] Write man page
- [ ] Test with various conversions

### nmo-diff (Day 61)
- [ ] Create `tools/nmo-diff.c`
- [ ] Implement argument parsing
- [ ] Implement object-level diff
- [ ] Implement chunk-level diff
- [ ] Implement semantic comparison
- [ ] Generate diff report
- [ ] Write man page
- [ ] Test with similar files

### Integration (Day 61)
- [ ] Test all tools together
- [ ] Create usage examples
- [ ] Update documentation

---

## Phase 14: Documentation (Days 62-65)

### API Documentation (Day 62)
- [ ] Add Doxygen comments to all public APIs
- [ ] Configure Doxygen
- [ ] Generate HTML documentation
- [ ] Review and fix any warnings
- [ ] Add usage examples to headers

### Examples (Day 63)
- [ ] Create `examples/simple_load.c`
- [ ] Create `examples/simple_save.c`
- [ ] Create `examples/file_converter.c`
- [ ] Create `examples/custom_manager.c`
- [ ] Create `examples/object_inspector.c`
- [ ] Document each example
- [ ] Test all examples

### Tutorials (Day 64)
- [ ] Write "Getting Started" tutorial
- [ ] Write "Custom Managers" tutorial
- [ ] Write "Error Handling" tutorial
- [ ] Write "Performance Tuning" tutorial
- [ ] Create tutorial code samples

### Final Documentation (Day 65)
- [ ] Update README.md
- [ ] Update CONTRIBUTING.md
- [ ] Create CHANGELOG.md
- [ ] Create API reference index
- [ ] Create troubleshooting guide
- [ ] Review all documentation
- [ ] Generate final documentation package

---

## Post-Development

### Release Preparation
- [ ] Version tagging (v1.0.0)
- [ ] Create release notes
- [ ] Package source tarball
- [ ] Package binary releases (Linux, macOS, Windows)
- [ ] Generate checksums
- [ ] Create GitHub release
- [ ] Announce release

### Future Work
- [ ] SIMD optimizations
- [ ] Multi-threaded loading
- [ ] Python bindings
- [ ] Additional tools
- [ ] Performance profiling
- [ ] Extended schema support
- [ ] Better error messages
- [ ] Localization support

---

## Progress Tracking

**Overall Progress**: 0% (0/14 phases complete)

**Current Phase**: Not started

**Blockers**: None

**Notes**: Ready to begin implementation

---

## Sign-off

- [ ] All phases completed
- [ ] All tests passing
- [ ] Documentation complete
- [ ] Ready for release

**Completed by**: _______________
**Date**: _______________
