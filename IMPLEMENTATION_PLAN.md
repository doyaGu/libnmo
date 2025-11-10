# libnmo - Comprehensive Implementation Plan

**Version**: 1.0
**Date**: 2025-11-10
**Status**: Ready for Implementation

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Architecture Overview](#architecture-overview)
3. [Implementation Phases](#implementation-phases)
4. [Detailed Phase Breakdown](#detailed-phase-breakdown)
5. [Dependency Graph](#dependency-graph)
6. [Testing Strategy](#testing-strategy)
7. [Development Timeline](#development-timeline)
8. [Risk Analysis](#risk-analysis)
9. [Quality Metrics](#quality-metrics)
10. [Appendix: Task Checklist](#appendix-task-checklist)

---

## Executive Summary

### Project Goal
Implement a complete, production-ready C library for reading and writing Virtools file formats (.nmo/.cmo/.vmo) with full compatibility with the original Virtools runtime.

### Key Deliverables
- ✅ Core library (libnmo) with layered architecture
- ✅ Complete load/save pipeline (15-phase load, 14-phase save)
- ✅ Schema system with pre-generated schemas
- ✅ Manager system with extensibility hooks
- ✅ Comprehensive test suite (unit, integration, fuzz)
- ✅ CLI tools (inspect, validate, convert, diff)
- ✅ Documentation and examples

### Design Principles
1. **Layered Architecture**: Clear separation of concerns (Core → IO → Format → Schema → Session)
2. **Zero Circular Dependencies**: Bottom-up design only
3. **Explicit Ownership**: Reference-counted context, arena allocation, non-owning spans
4. **Composable IO**: File → Checksum → Compression → Parser
5. **Schema-Driven**: Pre-generated C code for type safety
6. **Production-Ready**: Bounds-checked, error chains, comprehensive testing

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    APPLICATION LAYER                        │
│  - User applications                                        │
│  - CLI tools (inspect, validate, convert)                   │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────────┐
│                    SESSION LAYER                            │
│  - nmo_context (global state, thread-safe)                  │
│  - nmo_session (per-operation state)                        │
│  - nmo_parser (15-phase load pipeline)                      │
│  - nmo_builder (14-phase save pipeline)                     │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────────┐
│                      OBJECT LAYER                           │
│  - nmo_object_repository (ID management)                    │
│  - nmo_load_session (ID remapping)                          │
│  - nmo_id_remap_table (file ID → runtime ID)               │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────────┐
│                      SCHEMA LAYER                           │
│  - nmo_schema_registry (pre-generated schemas)              │
│  - nmo_validator (validation rules)                         │
│  - nmo_migrator (version migration)                         │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────────┐
│                      FORMAT LAYER                           │
│  - nmo_header (Part0/Part1 parsing)                         │
│  - nmo_chunk (CKStateChunk serialization)                   │
│  - nmo_object (object metadata)                             │
│  - nmo_manager (manager data)                               │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────────┐
│                        IO LAYER                             │
│  - nmo_io (base interface)                                  │
│  - nmo_io_file, nmo_io_memory                               │
│  - nmo_io_compressed (zlib wrapper)                         │
│  - nmo_io_checksum (Adler-32 wrapper)                       │
│  - nmo_txn (transactional writes)                           │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────────┐
│                       CORE LAYER                            │
│  - nmo_allocator (memory abstraction)                       │
│  - nmo_arena (bump allocator)                               │
│  - nmo_error (error handling with chains)                   │
│  - nmo_logger (diagnostic output)                           │
│  - nmo_guid (8-byte GUID operations)                        │
└─────────────────────────────────────────────────────────────┘
```

---

## Implementation Phases

### Phase 1: Project Setup and Core Foundation
**Duration**: 1-2 days
**Dependencies**: None

#### Tasks
1. Set up CMake build system
2. Configure directory structure
3. Set up CI/CD (optional)
4. Create basic README and CONTRIBUTING guides

#### Deliverables
- [ ] CMakeLists.txt with proper project structure
- [ ] Directory tree: `include/`, `src/`, `tests/`, `tools/`, `examples/`
- [ ] Build instructions documented

---

### Phase 2: Core Layer Implementation
**Duration**: 3-4 days
**Dependencies**: Phase 1

#### 2.1 Allocator System
```c
// nmo_allocator.h
typedef void* (*nmo_alloc_fn)(void* user_data, size_t size, size_t alignment);
typedef void  (*nmo_free_fn)(void* user_data, void* ptr);

typedef struct nmo_allocator {
    nmo_alloc_fn alloc;
    nmo_free_fn  free;
    void*       user_data;
} nmo_allocator;

nmo_allocator nmo_allocator_default(void);
nmo_allocator nmo_allocator_custom(nmo_alloc_fn alloc, nmo_free_fn free, void* user_data);
```

**Implementation Notes**:
- Default allocator wraps `malloc`/`free`
- Support aligned allocation for SIMD operations
- Thread-safety is user-defined

#### 2.2 Arena Allocator
```c
// nmo_arena.h
typedef struct nmo_arena nmo_arena;

nmo_arena* nmo_arena_create(nmo_allocator* allocator, size_t initial_size);
void* nmo_arena_alloc(nmo_arena* arena, size_t size, size_t alignment);
void nmo_arena_reset(nmo_arena* arena);
void nmo_arena_destroy(nmo_arena* arena);
```

**Implementation Notes**:
- Bump-pointer allocation
- Fast batch deallocation
- Grow in chunks (default 64KB)

#### 2.3 Error System
```c
// nmo_error.h
typedef enum {
    NMO_OK = 0,
    NMO_ERR_NOMEM,
    NMO_ERR_BUFFER_OVERRUN,
    NMO_ERR_FILE_NOT_FOUND,
    NMO_ERR_CANT_OPEN_FILE,
    NMO_ERR_INVALID_SIGNATURE,
    NMO_ERR_UNSUPPORTED_VERSION,
    NMO_ERR_CHECKSUM_MISMATCH,
    NMO_ERR_DECOMPRESSION_FAILED,
    NMO_ERR_VALIDATION_FAILED,
    NMO_ERR_INVALID_OFFSET,
    NMO_ERR_EOF,
} nmo_error_code;

typedef struct nmo_error {
    nmo_error_code    code;
    nmo_severity     severity;
    const char*      message;
    const char*      file;
    int              line;
    struct nmo_error* cause;  // Causal chain
} nmo_error;

typedef struct nmo_result {
    nmo_error_code  code;
    nmo_error*     error;
} nmo_result;

nmo_error* nmo_error_create(nmo_arena* arena, nmo_error_code code,
                             nmo_severity severity, const char* message);
void nmo_error_add_cause(nmo_error* err, nmo_error* cause);
```

**Implementation Notes**:
- Errors allocated from arena for automatic cleanup
- Support error chains for diagnostics
- Severity levels: DEBUG, INFO, WARNING, ERROR, FATAL

#### 2.4 Logger System
```c
// nmo_logger.h
typedef enum {
    NMO_LOG_DEBUG,
    NMO_LOG_INFO,
    NMO_LOG_WARN,
    NMO_LOG_ERROR,
} nmo_log_level;

typedef void (*nmo_log_fn)(void* user_data, nmo_log_level level, const char* message);

typedef struct nmo_logger {
    nmo_log_fn    log;
    void*        user_data;
    nmo_log_level level;
} nmo_logger;

void nmo_log(nmo_logger* logger, nmo_log_level level, const char* format, ...);
nmo_logger nmo_logger_stderr(void);
nmo_logger nmo_logger_null(void);
```

#### 2.5 GUID System
```c
// nmo_guid.h
typedef struct nmo_guid {
    uint32_t d1;
    uint32_t d2;
} nmo_guid;

int nmo_guid_equals(nmo_guid a, nmo_guid b);
uint32_t nmo_guid_hash(nmo_guid guid);
nmo_guid nmo_guid_parse(const char* str);
void nmo_guid_format(nmo_guid guid, char* buffer, size_t size);
```

#### Deliverables
- [ ] `src/core/allocator.c` with default implementation
- [ ] `src/core/arena.c` with bump allocator
- [ ] `src/core/error.c` with error chain support
- [ ] `src/core/logger.c` with stderr/null loggers
- [ ] `src/core/guid.c` with hash/parse/format
- [ ] Unit tests for all core components

---

### Phase 3: IO Layer Implementation
**Duration**: 4-5 days
**Dependencies**: Phase 2

#### 3.1 Base IO Interface
```c
// nmo_io.h
typedef enum {
    NMO_IO_READ   = 0x01,
    NMO_IO_WRITE  = 0x02,
    NMO_IO_CREATE = 0x04,
} nmo_io_mode;

typedef int (*nmo_io_read_fn)(void* handle, void* buffer, size_t size, size_t* bytes_read);
typedef int (*nmo_io_write_fn)(void* handle, const void* buffer, size_t size);
typedef int (*nmo_io_seek_fn)(void* handle, int64_t offset, int whence);
typedef int64_t (*nmo_io_tell_fn)(void* handle);
typedef int (*nmo_io_close_fn)(void* handle);

typedef struct nmo_io_interface {
    nmo_io_read_fn   read;
    nmo_io_write_fn  write;
    nmo_io_seek_fn   seek;
    nmo_io_tell_fn   tell;
    nmo_io_close_fn  close;
    void*           handle;
} nmo_io_interface;
```

#### 3.2 File IO
```c
// nmo_io_file.h
nmo_io_interface* nmo_file_io_open(const char* path, nmo_io_mode mode);
```

**Implementation Notes**:
- Platform-specific (POSIX `fopen`, Windows `CreateFile`)
- Support large files (>2GB)
- Proper error reporting

#### 3.3 Memory IO
```c
// nmo_io_memory.h
nmo_io_interface* nmo_memory_io_open_read(const void* data, size_t size);
nmo_io_interface* nmo_memory_io_open_write(size_t initial_capacity);
const void* nmo_memory_io_get_data(nmo_io_interface* io, size_t* size);
```

#### 3.4 Compressed IO
```c
// nmo_io_compressed.h
typedef enum {
    NMO_CODEC_ZLIB,
} nmo_compression_codec;

typedef struct nmo_compressed_io_desc {
    nmo_compression_codec codec;
    int                  level;       // 1-9
    size_t               buffer_size;
} nmo_compressed_io_desc;

nmo_io_interface* nmo_compressed_io_wrap(nmo_io_interface* inner,
                                          nmo_compressed_io_desc* desc);
```

**Implementation Notes**:
- Use zlib for compression/decompression
- Streaming mode for large files
- Internal buffering (default 64KB)

#### 3.5 Checksum IO
```c
// nmo_io_checksum.h
typedef enum {
    NMO_CHECKSUM_ADLER32,
    NMO_CHECKSUM_CRC32,
} nmo_checksum_algorithm;

typedef struct nmo_checksummed_io_desc {
    nmo_checksum_algorithm algorithm;
    uint32_t              initial_value;
} nmo_checksummed_io_desc;

nmo_io_interface* nmo_checksummed_io_wrap(nmo_io_interface* inner,
                                           nmo_checksummed_io_desc* desc);
uint32_t nmo_checksummed_io_get_checksum(nmo_io_interface* io);
```

#### 3.6 Transactional IO
```c
// nmo_txn.h
typedef enum {
    NMO_TXN_NONE,
    NMO_TXN_FSYNC,
    NMO_TXN_FDATASYNC,
} nmo_txn_durability;

typedef struct nmo_txn_desc {
    const char*        path;
    nmo_txn_durability durability;
    const char*        staging_dir;
} nmo_txn_desc;

typedef struct nmo_txn_handle nmo_txn_handle;

nmo_txn_handle* nmo_txn_open(nmo_txn_desc* desc);
int nmo_txn_write(nmo_txn_handle* txn, const void* data, size_t size);
int nmo_txn_commit(nmo_txn_handle* txn);
void nmo_txn_rollback(nmo_txn_handle* txn);
void nmo_txn_close(nmo_txn_handle* txn);
```

**Implementation Notes**:
- Write to temporary file
- Atomic rename on commit
- Automatic rollback on error
- Platform-specific fsync

#### Deliverables
- [ ] `src/io/io.c` base interface
- [ ] `src/io/io_file.c` for POSIX and Windows
- [ ] `src/io/io_memory.c` for in-memory buffers
- [ ] `src/io/io_compressed.c` with zlib integration
- [ ] `src/io/io_checksum.c` with Adler-32
- [ ] `src/io/txn.c` / `txn_posix.c` / `txn_windows.c`
- [ ] Integration tests for IO stack

---

### Phase 4: Format Layer - Headers and Basic Structures
**Duration**: 3-4 days
**Dependencies**: Phase 3

#### 4.1 File Header
```c
// nmo_header.h
typedef struct nmo_file_header {
    char     signature[8];           // "Nemo Fi\0"
    uint32_t file_version;           // 2-9
    uint32_t ck_version;
    uint32_t hdr1_pack_size;
    uint32_t hdr1_unpack_size;
    uint32_t data_pack_size;
    uint32_t data_unpack_size;
    uint32_t crc;                    // Adler-32 (v8+)
} nmo_file_header;

int nmo_header_parse(nmo_io_interface* io, nmo_file_header* header);
int nmo_header_validate(nmo_file_header* header);
int nmo_header_serialize(nmo_file_header* header, nmo_io_interface* io);
```

**Implementation Notes**:
- Part0 is always 32 bytes
- Part1 is additional 32 bytes (v5+)
- Signature: "Nemo Fi\0" (0x4E656D6F204669 00)
- Version check: 2 ≤ version ≤ 9

#### 4.2 Header1 (Object Descriptors)
```c
// nmo_header1.h
typedef struct nmo_object_desc {
    nmo_object_id  file_id;
    nmo_class_id   class_id;
    const char*   name;
    nmo_object_id  parent_id;
} nmo_object_desc;

typedef struct nmo_plugin_dep {
    nmo_guid          guid;
    nmo_plugin_category category;
    uint32_t         version;
} nmo_plugin_dep;

typedef struct nmo_header1 {
    uint32_t         object_count;
    uint32_t         manager_count;
    uint32_t         max_object_id;

    nmo_object_desc*  objects;
    nmo_plugin_dep*   plugin_deps;
    size_t           plugin_dep_count;

    const char**     included_files;
    size_t          included_file_count;
} nmo_header1;

int nmo_header1_parse(const void* data, size_t size,
                      nmo_header1* header, nmo_arena* arena);
nmo_buffer* nmo_header1_serialize(nmo_header1* header);
```

#### 4.3 Object Metadata
```c
// nmo_object.h
typedef struct nmo_object {
    nmo_object_id      id;
    nmo_class_id       class_id;
    const char*        name;
    uint32_t          flags;

    nmo_object*        parent;
    nmo_object**       children;
    size_t            child_count;

    nmo_chunk*         chunk;
    void*             data;

    nmo_object_id      file_index;
    uint32_t          creation_flags;
    uint32_t          save_flags;

    nmo_arena*         arena;
} nmo_object;
```

#### 4.4 Manager Data
```c
// nmo_manager.h
typedef struct nmo_manager {
    nmo_guid       guid;
    const char*   name;
    nmo_plugin_category category;

    int (*pre_load)(void* session, void* user_data);
    int (*post_load)(void* session, void* user_data);
    int (*load_data)(void* session, const nmo_chunk* chunk, void* user_data);
    nmo_chunk* (*save_data)(void* session, void* user_data);
    int (*pre_save)(void* session, void* user_data);
    int (*post_save)(void* session, void* user_data);

    void* user_data;
} nmo_manager;
```

#### Deliverables
- [ ] `src/format/header.c` for Part0/Part1 parsing
- [ ] `src/format/header1.c` for object descriptors
- [ ] `src/format/object.c` for object metadata
- [ ] `src/format/manager.c` for manager hooks
- [ ] Unit tests for header parsing/serialization

---

### Phase 5: Format Layer - CKStateChunk
**Duration**: 5-6 days
**Dependencies**: Phase 4

This is the most complex phase as CKStateChunk is the core serialization unit.

#### 5.1 Chunk Structure
```c
// nmo_chunk.h
typedef struct nmo_chunk {
    // Identity
    nmo_class_id       class_id;
    uint32_t          data_version;
    uint32_t          chunk_version;
    uint8_t           chunk_class_id;
    uint32_t          chunk_options;

    // Data buffer
    uint32_t*         data;
    size_t            data_size;         // In DWORDs
    size_t            data_capacity;

    // Tracking lists
    nmo_id_list       ids;
    nmo_chunk_list    chunks;
    nmo_manager_list  managers;

    // Parser/writer state
    nmo_chunk_parser* parser;
    nmo_chunk_writer* writer;

    // Compression info
    size_t            uncompressed_size;
    size_t            compressed_size;
    int               is_compressed;

    // Ownership
    nmo_arena*        arena;
    int               owns_data;
} nmo_chunk;
```

#### 5.2 Chunk Parser (Reader)
```c
// nmo_chunk_parser.h
typedef struct nmo_chunk_parser nmo_chunk_parser;

nmo_chunk_parser* nmo_chunk_parser_create(nmo_chunk* chunk);
size_t nmo_chunk_parser_tell(nmo_chunk_parser* p);
int nmo_chunk_parser_seek(nmo_chunk_parser* p, size_t pos);
int nmo_chunk_parser_skip(nmo_chunk_parser* p, size_t dwords);
size_t nmo_chunk_parser_remaining(nmo_chunk_parser* p);
int nmo_chunk_parser_at_end(nmo_chunk_parser* p);

// Primitive reads
int nmo_chunk_parser_read_byte(nmo_chunk_parser* p, uint8_t* out);
int nmo_chunk_parser_read_word(nmo_chunk_parser* p, uint16_t* out);
int nmo_chunk_parser_read_dword(nmo_chunk_parser* p, uint32_t* out);
int nmo_chunk_parser_read_int(nmo_chunk_parser* p, int32_t* out);
int nmo_chunk_parser_read_float(nmo_chunk_parser* p, float* out);
int nmo_chunk_parser_read_guid(nmo_chunk_parser* p, nmo_guid* out);

// Complex reads
int nmo_chunk_parser_read_bytes(nmo_chunk_parser* p, void* dest, size_t bytes);
int nmo_chunk_parser_read_string(nmo_chunk_parser* p, char** out, nmo_arena* arena);
int nmo_chunk_parser_read_buffer(nmo_chunk_parser* p, void** out, size_t* size, nmo_arena* arena);

// Object ID tracking
int nmo_chunk_parser_read_object_id(nmo_chunk_parser* p, nmo_object_id* out);

// Identifier system
int nmo_chunk_parser_seek_identifier(nmo_chunk_parser* p, uint32_t identifier);
int nmo_chunk_parser_read_identifier(nmo_chunk_parser* p, uint32_t* identifier);

void nmo_chunk_parser_destroy(nmo_chunk_parser* p);
```

**Implementation Notes**:
- All reads are DWORD-aligned
- Bounds checking on every operation
- Track cursor position for sequential access
- Support identifier-based navigation

#### 5.3 Chunk Writer
```c
// nmo_chunk_writer.h
typedef struct nmo_chunk_writer nmo_chunk_writer;

nmo_chunk_writer* nmo_chunk_writer_create(nmo_arena* arena);
void nmo_chunk_writer_start(nmo_chunk_writer* w, nmo_class_id class_id, uint32_t chunk_version);

// Primitive writes
int nmo_chunk_writer_write_byte(nmo_chunk_writer* w, uint8_t value);
int nmo_chunk_writer_write_word(nmo_chunk_writer* w, uint16_t value);
int nmo_chunk_writer_write_dword(nmo_chunk_writer* w, uint32_t value);
int nmo_chunk_writer_write_int(nmo_chunk_writer* w, int32_t value);
int nmo_chunk_writer_write_float(nmo_chunk_writer* w, float value);
int nmo_chunk_writer_write_guid(nmo_chunk_writer* w, nmo_guid guid);

// Complex writes
int nmo_chunk_writer_write_bytes(nmo_chunk_writer* w, const void* data, size_t bytes);
int nmo_chunk_writer_write_string(nmo_chunk_writer* w, const char* str);
int nmo_chunk_writer_write_buffer(nmo_chunk_writer* w, const void* data, size_t size);

// Object ID tracking
int nmo_chunk_writer_write_object_id(nmo_chunk_writer* w, nmo_object_id id);

// Sequences
int nmo_chunk_writer_start_object_sequence(nmo_chunk_writer* w, size_t count);
int nmo_chunk_writer_start_manager_sequence(nmo_chunk_writer* w, size_t count);

// Sub-chunks
int nmo_chunk_writer_start_sub_chunk(nmo_chunk_writer* w);
int nmo_chunk_writer_end_sub_chunk(nmo_chunk_writer* w);

// Identifiers
int nmo_chunk_writer_write_identifier(nmo_chunk_writer* w, uint32_t identifier);

// Finalize
nmo_chunk* nmo_chunk_writer_finalize(nmo_chunk_writer* w);
void nmo_chunk_writer_destroy(nmo_chunk_writer* w);
```

**Implementation Notes**:
- Automatic buffer growth (500 DWORD increments)
- Track optional lists (IDs, chunks, managers)
- DWORD alignment maintained automatically
- Set `chunk_version = CHUNK_VERSION4` (7)

#### 5.4 Chunk Serialization
```c
// Serialization/Deserialization
nmo_buffer* nmo_chunk_serialize(nmo_chunk* chunk);
nmo_chunk* nmo_chunk_deserialize(const void* data, size_t size, nmo_arena* arena);

// ID remapping
int nmo_chunk_remap_ids(nmo_chunk* chunk, nmo_id_remap_table* table);
```

**Binary Format (Version Info Packing)**:
```
Bits [0-7]:   DataVersion    (8 bits)
Bits [8-15]:  ChunkClassID   (8 bits)
Bits [16-23]: ChunkVersion   (8 bits)
Bits [24-31]: ChunkOptions   (8 bits)

versionInfo = (dataVersion | (chunkClassID << 8)) |
              ((chunkVersion | (chunkOptions << 8)) << 16)
```

#### 5.5 Implementation Priority
1. Basic chunk structure and allocation
2. Parser with primitive reads
3. Writer with primitive writes
4. Serialization/deserialization
5. Object ID tracking
6. Sub-chunk support
7. Identifier system
8. ID remapping

#### Deliverables
- [ ] `src/format/chunk.c` core implementation
- [ ] `src/format/chunk_parser.c` reader
- [ ] `src/format/chunk_writer.c` writer
- [ ] `src/format/chunk_serialize.c` binary format
- [ ] Unit tests for all chunk operations
- [ ] Round-trip tests (write → read → verify)

---

### Phase 6: Schema System
**Duration**: 3-4 days
**Dependencies**: Phase 5

#### 6.1 Schema Descriptors
```c
// nmo_schema.h
typedef enum {
    NMO_FIELD_INT8,
    NMO_FIELD_UINT8,
    NMO_FIELD_INT16,
    NMO_FIELD_UINT16,
    NMO_FIELD_INT32,
    NMO_FIELD_UINT32,
    NMO_FIELD_FLOAT,
    NMO_FIELD_STRING,
    NMO_FIELD_GUID,
    NMO_FIELD_OBJECT_ID,
    NMO_FIELD_STRUCT,
    NMO_FIELD_ARRAY,
} nmo_field_type;

typedef struct nmo_field_descriptor {
    const char*     name;
    nmo_field_type  type;
    size_t         offset;
    size_t         size;
    size_t         count;
    nmo_class_id    class_id;
    const char*     validation_rule;
} nmo_field_descriptor;

typedef struct nmo_schema_descriptor {
    nmo_class_id              class_id;
    const char*              class_name;
    nmo_class_id              parent_class_id;
    nmo_field_descriptor*     fields;
    size_t                   field_count;
    uint32_t                 chunk_version;

    void (*serialize_fn)(nmo_object* obj, nmo_chunk_writer* writer);
    void (*deserialize_fn)(nmo_object* obj, nmo_chunk_parser* parser);
} nmo_schema_descriptor;
```

#### 6.2 Schema Registry
```c
// nmo_schema_registry.h
typedef struct nmo_schema_registry nmo_schema_registry;

nmo_schema_registry* nmo_schema_registry_create(nmo_allocator* allocator);

int nmo_schema_registry_add(nmo_schema_registry* registry,
                             const nmo_schema_descriptor* schema);

int nmo_schema_registry_add_builtin(nmo_schema_registry* registry);

const nmo_schema_descriptor* nmo_schema_registry_find_by_id(
    nmo_schema_registry* registry, nmo_class_id class_id);

const nmo_schema_descriptor* nmo_schema_registry_find_by_name(
    nmo_schema_registry* registry, const char* class_name);

int nmo_verify_schema_consistency(nmo_schema_registry* registry);

void nmo_schema_registry_destroy(nmo_schema_registry* registry);
```

#### 6.3 Built-in Schemas (Pre-generated)
```c
// src/builtin_schemas/nmo_builtin_schemas.c

// Schema for CKObject (base class)
static const nmo_field_descriptor ckobject_fields[] = {
    {"id", NMO_FIELD_UINT32, offsetof(nmo_object, id), sizeof(uint32_t), 1, 0, NULL},
    {"name", NMO_FIELD_STRING, offsetof(nmo_object, name), sizeof(char*), 1, 0, NULL},
    {"flags", NMO_FIELD_UINT32, offsetof(nmo_object, flags), sizeof(uint32_t), 1, 0, NULL},
};

static void ckobject_serialize(nmo_object* obj, nmo_chunk_writer* writer) {
    nmo_chunk_writer_write_identifier(writer, 1);  // Identifier 1: name
    nmo_chunk_writer_write_string(writer, obj->name);
    nmo_chunk_writer_write_dword(writer, obj->flags);
}

static void ckobject_deserialize(nmo_object* obj, nmo_chunk_parser* parser) {
    uint32_t identifier;
    if (nmo_chunk_parser_seek_identifier(parser, 1) == NMO_OK) {
        nmo_chunk_parser_read_identifier(parser, &identifier);
        nmo_chunk_parser_read_string(parser, (char**)&obj->name, obj->arena);
    }
    nmo_chunk_parser_read_dword(parser, &obj->flags);
}

static const nmo_schema_descriptor ckobject_schema = {
    .class_id = 0x00000001,
    .class_name = "CKObject",
    .parent_class_id = 0,
    .fields = (nmo_field_descriptor*)ckobject_fields,
    .field_count = 3,
    .chunk_version = 7,
    .serialize_fn = ckobject_serialize,
    .deserialize_fn = ckobject_deserialize,
};

// Register all built-in schemas
int nmo_schema_registry_add_builtin(nmo_schema_registry* registry) {
    nmo_schema_registry_add(registry, &ckobject_schema);
    // Add more schemas...
    return NMO_OK;
}
```

**Built-in Schemas to Implement**:
- CKObject (0x00000001)
- CKBeObject (0x00000002)
- CKSceneObject (0x00000003)
- CK3dEntity (0x00000004)
- CK3dObject (0x00000005)
- CKCamera (0x00000006)
- CKLight (0x00000007)
- CKMesh (0x00000008)
- CKMaterial (0x00000009)
- CKTexture (0x0000000A)
- CKBehavior (0x00000014)
- CKParameter (0x00000015)

#### 6.4 Validator
```c
// nmo_validator.h
typedef enum {
    NMO_VALIDATION_STRICT,
    NMO_VALIDATION_PERMISSIVE,
} nmo_validation_mode;

typedef enum {
    NMO_VALID,
    NMO_VALID_WITH_WARNINGS,
    NMO_INVALID,
} nmo_validation_result;

typedef struct nmo_validation nmo_validation;

nmo_validation* nmo_validation_create(nmo_schema_registry* registry,
                                      nmo_validation_mode mode);

nmo_validation_result nmo_validate_object(nmo_validation* v, nmo_object* obj);
nmo_validation_result nmo_validate_file(nmo_validation* v, const char* path);

void nmo_validation_destroy(nmo_validation* v);
```

#### 6.5 Migrator
```c
// nmo_migrator.h
typedef struct nmo_migrator nmo_migrator;

nmo_migrator* nmo_migrator_create(nmo_schema_registry* registry);

int nmo_migrate_chunk(nmo_migrator* m, nmo_chunk* chunk, uint32_t target_version);

void nmo_migrator_destroy(nmo_migrator* m);
```

#### Deliverables
- [ ] `src/schema/schema.c` core structures
- [ ] `src/schema/schema_registry.c` with thread-safe lookup
- [ ] `src/builtin_schemas/nmo_builtin_schemas.c` with at least 12 schemas
- [ ] `src/schema/validator.c` basic validation
- [ ] `src/schema/migrator.c` version migration
- [ ] Unit tests for schema registration and lookup

---

### Phase 7: Object System and ID Remapping
**Duration**: 4-5 days
**Dependencies**: Phase 6

#### 7.1 Object Repository
```c
// nmo_object_repository.h
typedef struct nmo_object_repository nmo_object_repository;

nmo_object_repository* nmo_object_repository_create(nmo_arena* arena);

nmo_object* nmo_object_create(nmo_object_repository* repo,
                              nmo_class_id class_id,
                              const char* name,
                              uint32_t flags);

nmo_object* nmo_object_find_by_id(nmo_object_repository* repo, nmo_object_id id);
nmo_object* nmo_object_find_by_name(nmo_object_repository* repo, const char* name);
nmo_object** nmo_object_find_by_class(nmo_object_repository* repo,
                                      nmo_class_id class_id,
                                      size_t* count);

void nmo_object_repository_destroy(nmo_object_repository* repo);
```

**Implementation Notes**:
- Hash table for ID → object lookup
- Hash table for name → object lookup
- Per-class arrays for efficient filtering
- Dense array for iteration

#### 7.2 Load Session Management
```c
// nmo_load_session.h
typedef struct nmo_load_session {
    nmo_object_repository* repo;
    nmo_object_id         saved_id_max;
    nmo_object_id         id_base;
    nmo_hash_table*       file_to_runtime;
    int                  active;
} nmo_load_session;

int nmo_load_session_start(nmo_object_repository* repo, nmo_object_id max_saved_id);
int nmo_load_session_register(nmo_load_session* session,
                              nmo_object* obj,
                              nmo_object_id file_id);
int nmo_load_session_end(nmo_load_session* session);
```

#### 7.3 ID Remapping
```c
// nmo_id_remap.h
typedef struct nmo_id_remap_table {
    nmo_hash_table*        map;
    nmo_id_remap_entry*    entries;
    size_t                entry_count;
} nmo_id_remap_table;

nmo_id_remap_table* nmo_build_remap_table(nmo_load_session* session);

int nmo_id_remap_lookup(nmo_id_remap_table* table,
                        nmo_object_id old_id,
                        nmo_object_id* new_id);

typedef struct nmo_id_remap_plan {
    nmo_id_remap_table* table;
    nmo_hash_table*    name_to_id;
    size_t            objects_remapped;
    size_t            conflicts_resolved;
} nmo_id_remap_plan;

nmo_id_remap_plan* nmo_id_remap_plan_create(nmo_object_repository* repo,
                                            nmo_object** objects_to_save,
                                            size_t object_count);

void nmo_id_remap_plan_destroy(nmo_id_remap_plan* plan);
```

**Implementation Notes**:
- During load: file IDs → runtime IDs
- During save: runtime IDs → sequential file IDs (0-based)
- Handle reference-only objects (bit 23 set)
- Handle negative IDs (external references)

#### Deliverables
- [ ] `src/session/object_repository.c` with hash tables
- [ ] `src/session/load_session.c` ID tracking
- [ ] `src/session/id_remap.c` remapping logic
- [ ] Unit tests for ID allocation and remapping
- [ ] Integration tests for load/save cycles

---

### Phase 8: Session Layer and Context
**Duration**: 3-4 days
**Dependencies**: Phase 7

#### 8.1 Context (Global State)
```c
// nmo_context.h
typedef struct nmo_context_desc {
    nmo_allocator*  allocator;
    nmo_logger*     logger;
    int             thread_pool_size;
} nmo_context_desc;

typedef struct nmo_context nmo_context;

nmo_context* nmo_context_create(nmo_context_desc* desc);
void nmo_context_retain(nmo_context* ctx);
void nmo_context_release(nmo_context* ctx);

// Access registries
nmo_schema_registry* nmo_context_get_schema_registry(nmo_context* ctx);
nmo_manager_registry* nmo_context_get_manager_registry(nmo_context* ctx);
```

**Implementation Notes**:
- Reference-counted for thread safety
- Owns schema registry, manager registry
- Global logger and allocator
- Thread-safe access (read-write locks)

#### 8.2 Session (Per-Operation State)
```c
// nmo_session.h
typedef struct nmo_session nmo_session;

nmo_session* nmo_session_create(nmo_context* ctx);

typedef struct nmo_file_info {
    uint32_t file_version;
    uint32_t ck_version;
    size_t   file_size;
    uint32_t object_count;
    uint32_t manager_count;
    uint32_t write_mode;
} nmo_file_info;

nmo_file_info nmo_session_get_file_info(nmo_session* session);

void nmo_session_destroy(nmo_session* session);
```

**Implementation Notes**:
- Single-threaded, non-reentrant
- Owns arena for session-local allocations
- Owns object repository
- Borrows context (does not retain)

#### Deliverables
- [ ] `src/app/context.c` with reference counting
- [ ] `src/app/session.c` with arena management
- [ ] Unit tests for context lifecycle
- [ ] Thread safety tests for context

---

### Phase 9: Load Pipeline (15 Phases)
**Duration**: 6-7 days
**Dependencies**: Phase 8

Implement the complete 15-phase load workflow as documented in `libnmo.md`.

#### 9.1 Load API
```c
// nmo_parser.h
typedef enum {
    NMO_LOAD_DEFAULT            = 0,
    NMO_LOAD_DODIALOG           = 0x0001,
    NMO_LOAD_AUTOMATICMODE      = 0x0002,
    NMO_LOAD_CHECKDUPLICATES    = 0x0004,
    NMO_LOAD_AS_DYNAMIC_OBJECT  = 0x0008,
    NMO_LOAD_ONLYBEHAVIORS      = 0x0010,
    NMO_LOAD_CHECK_DEPENDENCIES = 0x0020,
} nmo_load_flags;

nmo_result nmo_load_file(nmo_session* session,
                         const char* path,
                         nmo_load_flags flags);
```

#### 9.2 Load Pipeline Phases

**Phase 1: Open IO**
```c
nmo_io_interface* io = nmo_file_io_open(path, NMO_IO_READ);
if (!io) return NMO_ERR_FILE_NOT_FOUND;
```

**Phase 2: Parse File Header**
```c
nmo_file_header header;
int result = nmo_header_parse(io, &header);
if (result != NMO_OK) return result;

result = nmo_header_validate(&header);
if (result != NMO_OK) return result;
```

**Phase 3: Read and Decompress Header1**
```c
nmo_buffer* hdr1_compressed = nmo_buffer_read(io, header.hdr1_pack_size);
nmo_buffer* hdr1_data;

if (header.hdr1_pack_size != header.hdr1_unpack_size) {
    hdr1_data = nmo_decompress_buffer(hdr1_compressed, NMO_CODEC_ZLIB,
                                       header.hdr1_unpack_size);
} else {
    hdr1_data = hdr1_compressed;
}
```

**Phase 4: Parse Header1**
```c
nmo_header1 hdr1;
result = nmo_header1_parse(hdr1_data->data, hdr1_data->size, &hdr1, session->arena);
```

**Phase 5: Start Load Session**
```c
nmo_load_session_start(session->repo, hdr1.max_object_id);
```

**Phase 6: Check Plugin Dependencies**
```c
for (size_t i = 0; i < hdr1.plugin_dep_count; i++) {
    nmo_plugin_dep* dep = &hdr1.plugin_deps[i];
    if (!nmo_plugin_is_available(session->context, dep->guid)) {
        nmo_log_warn(session->context->logger, "Missing plugin: %s", dep->name);
    }
}
```

**Phase 7: Manager Pre-Load Hooks**
```c
nmo_execute_managers_pre_load(session);
```

**Phase 8: Read and Decompress Data Section**
```c
nmo_buffer* data_compressed = nmo_buffer_read(io, header.data_pack_size);
nmo_buffer* data;

if (header.data_pack_size != header.data_unpack_size) {
    data = nmo_decompress_buffer(data_compressed, NMO_CODEC_ZLIB,
                                  header.data_unpack_size);
} else {
    data = data_compressed;
}
```

**Phase 9: Parse Manager Chunks**
```c
size_t offset = 0;
for (size_t i = 0; i < hdr1.manager_count; i++) {
    uint32_t size = *(uint32_t*)&data->data[offset];
    offset += 4;

    nmo_chunk* chunk = nmo_chunk_deserialize(&data->data[offset], size, session->arena);
    offset += size;

    nmo_manager* mgr = nmo_find_manager_by_guid(session, chunk->guid);
    if (mgr && mgr->load_data) {
        mgr->load_data(session, chunk, mgr->user_data);
    }
}
```

**Phase 10: Create Objects**
```c
for (size_t i = 0; i < hdr1.object_count; i++) {
    nmo_object_desc* desc = &hdr1.objects[i];

    if (desc->file_id & NMO_OBJECT_REFERENCE_FLAG) {
        continue;  // Reference-only
    }

    nmo_object* obj = nmo_object_create(session->repo, desc->class_id,
                                       desc->name, 0);
    nmo_load_session_register(session->load_session, obj, desc->file_id);
}
```

**Phase 11: Parse Object Chunks**
```c
for (size_t i = 0; i < hdr1.object_count; i++) {
    nmo_object_desc* desc = &hdr1.objects[i];

    if (desc->file_id & NMO_OBJECT_REFERENCE_FLAG) {
        continue;
    }

    uint32_t size = *(uint32_t*)&data->data[offset];
    offset += 4;

    nmo_chunk* chunk = nmo_chunk_deserialize(&data->data[offset], size, session->arena);
    offset += size;

    nmo_object* obj = nmo_object_find_by_id(session->repo,
                                            session->load_session->id_base + desc->file_id);
    obj->chunk = chunk;
}
```

**Phase 12: Build ID Remap Table**
```c
nmo_id_remap_table* remap_table = nmo_build_remap_table(session->load_session);
```

**Phase 13: Remap IDs in All Chunks**
```c
for (size_t i = 0; i < session->repo->object_count; i++) {
    nmo_object* obj = session->repo->objects[i];
    if (obj->chunk) {
        nmo_chunk_remap_ids(obj->chunk, remap_table);
    }
}
```

**Phase 14: Deserialize Objects**
```c
for (size_t i = 0; i < session->repo->object_count; i++) {
    nmo_object* obj = session->repo->objects[i];
    const nmo_schema_descriptor* schema =
        nmo_schema_registry_find_by_id(session->context->schema_registry,
                                      obj->class_id);

    if (schema && schema->deserialize_fn && obj->chunk) {
        nmo_chunk_parser* parser = nmo_chunk_parser_create(obj->chunk);
        schema->deserialize_fn(obj, parser);
        nmo_chunk_parser_destroy(parser);
    }
}
```

**Phase 15: Manager Post-Load Hooks**
```c
nmo_execute_managers_post_load(session);
nmo_io_close(io);
return NMO_OK;
```

#### Deliverables
- [ ] `src/session/parser.c` with complete 15-phase pipeline
- [ ] Integration tests for each phase
- [ ] Error handling for all failure cases
- [ ] Support for all load flags

---

### Phase 10: Save Pipeline (14 Phases)
**Duration**: 5-6 days
**Dependencies**: Phase 9

#### 10.1 Save API
```c
// nmo_builder.h
typedef struct nmo_save_options {
    uint32_t              file_version;
    nmo_file_write_mode   write_mode;
    nmo_compression_level compress_level;
} nmo_save_options;

typedef struct nmo_save_builder nmo_save_builder;

nmo_save_builder* nmo_save_builder_create(nmo_session* session);
void nmo_save_builder_add(nmo_save_builder* builder, nmo_object* obj);
void nmo_save_builder_add_all(nmo_save_builder* builder);
void nmo_save_builder_set_options(nmo_save_builder* builder, nmo_save_options* opts);
nmo_result nmo_save_builder_execute(nmo_save_builder* builder, const char* path);
void nmo_save_builder_destroy(nmo_save_builder* builder);
```

#### 10.2 Save Pipeline Phases

**Phase 1: Manager Pre-Save Hooks**
```c
nmo_execute_managers_pre_save(builder->session);
```

**Phase 2: Build ID Remap Plan**
```c
nmo_id_remap_plan* plan = nmo_id_remap_plan_create(builder->session->repo,
                                                   builder->objects,
                                                   builder->object_count);
```

**Phase 3: Serialize Manager Chunks**
```c
nmo_buffer* manager_buffer = nmo_buffer_create(64 * 1024);
for (size_t i = 0; i < builder->manager_count; i++) {
    nmo_manager* mgr = builder->managers[i];
    if (mgr->save_data) {
        nmo_chunk* chunk = mgr->save_data(builder->session, mgr->user_data);
        nmo_buffer* chunk_buf = nmo_chunk_serialize(chunk);

        uint32_t size = (uint32_t)chunk_buf->size;
        nmo_buffer_append(manager_buffer, &size, 4);
        nmo_buffer_append(manager_buffer, chunk_buf->data, chunk_buf->size);
    }
}
```

**Phase 4: Serialize Object Chunks**
```c
nmo_buffer* object_buffer = nmo_buffer_create(1024 * 1024);
for (size_t i = 0; i < builder->object_count; i++) {
    nmo_object* obj = builder->objects[i];
    const nmo_schema_descriptor* schema =
        nmo_schema_registry_find_by_id(builder->session->context->schema_registry,
                                      obj->class_id);

    nmo_chunk_writer* writer = nmo_chunk_writer_create(builder->session->arena);
    nmo_chunk_writer_start(writer, obj->class_id, schema->chunk_version);

    if (schema && schema->serialize_fn) {
        schema->serialize_fn(obj, writer);
    }

    nmo_chunk* chunk = nmo_chunk_writer_finalize(writer);
    nmo_chunk_remap_ids(chunk, plan->table);

    nmo_buffer* chunk_buf = nmo_chunk_serialize(chunk);
    uint32_t size = (uint32_t)chunk_buf->size;
    nmo_buffer_append(object_buffer, &size, 4);
    nmo_buffer_append(object_buffer, chunk_buf->data, chunk_buf->size);
}
```

**Phase 5-14**: Build Header1, compress sections, compute checksum, write to disk, etc.

#### Deliverables
- [ ] `src/session/builder.c` with complete 14-phase pipeline
- [ ] Transaction support for atomic saves
- [ ] Compression integration
- [ ] Checksum computation
- [ ] Integration tests

---

### Phase 11: Manager System
**Duration**: 2-3 days
**Dependencies**: Phase 10

#### 11.1 Manager Registry
```c
// nmo_manager_registry.h
typedef struct nmo_manager_registry nmo_manager_registry;

nmo_manager_registry* nmo_manager_registry_create(nmo_allocator* allocator);
int nmo_manager_register(nmo_manager_registry* registry, nmo_manager* mgr);
nmo_manager* nmo_manager_find_by_guid(nmo_manager_registry* registry, nmo_guid guid);
void nmo_manager_registry_destroy(nmo_manager_registry* registry);
```

#### 11.2 Example Manager Implementation
```c
// Example: Render Manager
const nmo_guid NMO_GUID_RENDER_MANAGER = { 0x6BED328B, 0x4523A08C };

typedef struct render_manager_state {
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t bpp;
} render_manager_state;

int render_manager_load_data(void* session, const nmo_chunk* chunk, void* user_data) {
    render_manager_state* state = (render_manager_state*)user_data;
    nmo_chunk_parser* parser = nmo_chunk_parser_create((nmo_chunk*)chunk);

    nmo_chunk_parser_read_dword(parser, &state->screen_width);
    nmo_chunk_parser_read_dword(parser, &state->screen_height);
    nmo_chunk_parser_read_dword(parser, &state->bpp);

    nmo_chunk_parser_destroy(parser);
    return NMO_OK;
}

nmo_chunk* render_manager_save_data(void* session, void* user_data) {
    render_manager_state* state = (render_manager_state*)user_data;
    nmo_session* s = (nmo_session*)session;

    nmo_chunk_writer* writer = nmo_chunk_writer_create(s->arena);
    nmo_chunk_writer_start(writer, 0, CHUNK_VERSION4);

    nmo_chunk_writer_write_dword(writer, state->screen_width);
    nmo_chunk_writer_write_dword(writer, state->screen_height);
    nmo_chunk_writer_write_dword(writer, state->bpp);

    return nmo_chunk_writer_finalize(writer);
}

void register_render_manager(nmo_context* ctx) {
    render_manager_state* state = calloc(1, sizeof(render_manager_state));

    nmo_manager render_mgr = {
        .guid = NMO_GUID_RENDER_MANAGER,
        .name = "CKRenderManager",
        .category = NMO_PLUGIN_RENDER_DLL,
        .load_data = render_manager_load_data,
        .save_data = render_manager_save_data,
        .user_data = state
    };

    nmo_manager_register(ctx->manager_registry, &render_mgr);
}
```

#### Deliverables
- [ ] `src/format/manager_registry.c`
- [ ] Example manager implementations (at least 3)
- [ ] Unit tests for manager registration and hooks

---

### Phase 12: Testing Infrastructure
**Duration**: 5-6 days
**Dependencies**: Phase 11

#### 12.1 Unit Tests
- Core: allocator, arena, error, logger, GUID
- IO: file, memory, compressed, checksum, transaction
- Format: header, chunk parser/writer, serialization
- Schema: registry, validator
- Object: repository, load session, ID remapping
- Session: context, session

#### 12.2 Integration Tests
- Round-trip: load → save → load
- Version migration: v2 → v8
- Manager hooks
- ID remapping with complex graphs
- Compression variants

#### 12.3 Fuzz Tests
- Header parsing with malformed data
- Chunk parsing with random buffers
- Decompression with corrupt data

#### 12.4 Test Framework
```c
// tests/test_framework.h
#define TEST(suite, name) \
    void suite##_##name(void)

#define ASSERT_EQ(expected, actual) \
    test_assert_eq(__FILE__, __LINE__, (expected), (actual))

#define ASSERT_NE(expected, actual) \
    test_assert_ne(__FILE__, __LINE__, (expected), (actual))

#define ASSERT_TRUE(condition) \
    test_assert_true(__FILE__, __LINE__, (condition))

#define ASSERT_FALSE(condition) \
    test_assert_false(__FILE__, __LINE__, (condition))

// Test runner
int run_tests(void);
```

#### Deliverables
- [ ] Test framework with assertions
- [ ] Unit tests (>80% coverage)
- [ ] Integration tests (round-trip, migration)
- [ ] Fuzz tests for parsers
- [ ] CI/CD integration

---

### Phase 13: CLI Tools
**Duration**: 4-5 days
**Dependencies**: Phase 12

#### 13.1 nmo-inspect
```bash
nmo-inspect file.nmo                 # Text output
nmo-inspect --format=json file.nmo   # JSON output
nmo-inspect --verbose file.nmo       # Verbose output
```

**Features**:
- File header info
- Object list
- Manager list
- Plugin dependencies
- Validation errors

#### 13.2 nmo-validate
```bash
nmo-validate file.nmo                # Validate file
nmo-validate --strict file.nmo       # Strict mode
```

**Features**:
- Schema validation
- Checksum verification
- Plugin dependency check
- Object reference validation

#### 13.3 nmo-convert
```bash
nmo-convert input.nmo output.nmo --version=8
nmo-convert input.nmo output.nmo --compress
```

**Features**:
- Version migration
- Compression toggle
- Format conversion

#### 13.4 nmo-diff
```bash
nmo-diff file1.nmo file2.nmo         # Compare files
```

**Features**:
- Object-level diff
- Chunk-level diff
- Semantic comparison

#### Deliverables
- [ ] `tools/nmo-inspect.c`
- [ ] `tools/nmo-validate.c`
- [ ] `tools/nmo-convert.c`
- [ ] `tools/nmo-diff.c`
- [ ] Man pages for all tools

---

### Phase 14: Documentation and Examples
**Duration**: 3-4 days
**Dependencies**: Phase 13

#### 14.1 API Documentation
- Doxygen comments for all public APIs
- Usage examples
- Best practices

#### 14.2 Examples
```c
// examples/simple_load.c
#include "nmo.h"

int main(int argc, char** argv) {
    nmo_context* ctx = nmo_context_create(&(nmo_context_desc){
        .allocator = NULL,
        .logger = nmo_logger_stderr(),
        .thread_pool_size = 4
    });

    nmo_schema_registry_add_builtin(ctx->schema_registry);
    nmo_session* session = nmo_session_create(ctx);

    nmo_result result = nmo_load_file(session, argv[1], NMO_LOAD_DEFAULT);
    if (result.code != NMO_OK) {
        fprintf(stderr, "Load failed: %s\n", result.error->message);
        return 1;
    }

    nmo_file_info info = nmo_session_get_file_info(session);
    printf("Object Count: %u\n", info.object_count);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
    return 0;
}
```

#### 14.3 Tutorials
- Getting started
- Custom managers
- Schema extension
- Error handling
- Performance tuning

#### Deliverables
- [ ] Doxygen configuration
- [ ] API documentation
- [ ] 5+ examples
- [ ] 3+ tutorials
- [ ] README with quick start

---

## Dependency Graph

```
Phase 1: Project Setup
    ↓
Phase 2: Core Layer
    ↓
Phase 3: IO Layer
    ↓
Phase 4: Format Layer - Headers
    ↓
Phase 5: Format Layer - CKStateChunk ← (Critical Path)
    ↓
Phase 6: Schema System
    ↓
Phase 7: Object System
    ↓
Phase 8: Session Layer
    ↓
Phase 9: Load Pipeline ← (Critical Path)
    ↓
Phase 10: Save Pipeline
    ↓
Phase 11: Manager System
    ↓
Phase 12: Testing Infrastructure ← (Quality Gate)
    ↓
Phase 13: CLI Tools
    ↓
Phase 14: Documentation

Critical Path: 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 12
```

---

## Testing Strategy

### Unit Testing
- **Coverage Target**: >80%
- **Focus Areas**: Core, IO, Format, Schema
- **Tools**: Custom test framework, valgrind

### Integration Testing
- **Round-trip Tests**: Load → Save → Load
- **Version Migration**: v2-v9 compatibility
- **Manager Hooks**: Pre/post load/save
- **ID Remapping**: Complex object graphs

### Fuzz Testing
- **Targets**: Header parsing, chunk parsing, decompression
- **Tools**: AFL, libFuzzer
- **Duration**: Continuous

### Regression Testing
- **Test Suite**: Known Virtools files
- **Validation**: Byte-perfect round-trips
- **Performance**: Load/save benchmarks

---

## Development Timeline

| Phase | Duration | Start | End |
|-------|----------|-------|-----|
| 1: Project Setup | 2 days | Day 1 | Day 2 |
| 2: Core Layer | 4 days | Day 3 | Day 6 |
| 3: IO Layer | 5 days | Day 7 | Day 11 |
| 4: Format Headers | 4 days | Day 12 | Day 15 |
| 5: CKStateChunk | 6 days | Day 16 | Day 21 |
| 6: Schema System | 4 days | Day 22 | Day 25 |
| 7: Object System | 5 days | Day 26 | Day 30 |
| 8: Session Layer | 4 days | Day 31 | Day 34 |
| 9: Load Pipeline | 7 days | Day 35 | Day 41 |
| 10: Save Pipeline | 6 days | Day 42 | Day 47 |
| 11: Manager System | 3 days | Day 48 | Day 50 |
| 12: Testing | 6 days | Day 51 | Day 56 |
| 13: CLI Tools | 5 days | Day 57 | Day 61 |
| 14: Documentation | 4 days | Day 62 | Day 65 |

**Total Duration**: ~65 days (3 months)

---

## Risk Analysis

### High Risk
1. **CKStateChunk Complexity** (Phase 5)
   - Mitigation: Incremental implementation, extensive testing

2. **ID Remapping Logic** (Phase 7)
   - Mitigation: Reference implementation tests, fuzz testing

3. **Load/Save Pipeline** (Phases 9-10)
   - Mitigation: Phase-by-phase verification, integration tests

### Medium Risk
1. **Schema System** (Phase 6)
   - Mitigation: Pre-generated schemas, validation tests

2. **Platform Compatibility**
   - Mitigation: CI/CD on multiple platforms

### Low Risk
1. **Core/IO Layers** (Phases 2-3)
   - Standard implementations, well-understood

---

## Quality Metrics

### Code Quality
- **Lines of Code**: ~15,000-20,000 LOC
- **Test Coverage**: >80%
- **Static Analysis**: Clean (cppcheck, clang-tidy)
- **Memory Leaks**: Zero (valgrind)

### Performance
- **Load Time**: <100ms for 1000 objects
- **Memory Usage**: <2x file size
- **Compression Ratio**: ~40% with zlib

### Compatibility
- **File Versions**: v2-v9
- **Platforms**: Linux, macOS, Windows
- **Compilers**: GCC, Clang, MSVC

---

## Appendix: Task Checklist

### Phase 1: Project Setup
- [ ] CMakeLists.txt
- [ ] Directory structure
- [ ] README.md
- [ ] CONTRIBUTING.md

### Phase 2: Core Layer
- [ ] nmo_allocator.h/.c
- [ ] nmo_arena.h/.c
- [ ] nmo_error.h/.c
- [ ] nmo_logger.h/.c
- [ ] nmo_guid.h/.c
- [ ] Unit tests

### Phase 3: IO Layer
- [ ] nmo_io.h/.c
- [ ] nmo_io_file.h/.c
- [ ] nmo_io_memory.h/.c
- [ ] nmo_io_compressed.h/.c
- [ ] nmo_io_checksum.h/.c
- [ ] nmo_txn.h/.c
- [ ] Integration tests

### Phase 4: Format Headers
- [ ] nmo_header.h/.c
- [ ] nmo_header1.h/.c
- [ ] nmo_object.h/.c
- [ ] nmo_manager.h/.c
- [ ] Unit tests

### Phase 5: CKStateChunk
- [ ] nmo_chunk.h/.c
- [ ] nmo_chunk_parser.h/.c
- [ ] nmo_chunk_writer.h/.c
- [ ] nmo_chunk_serialize.h/.c
- [ ] Round-trip tests

### Phase 6: Schema System
- [ ] nmo_schema.h/.c
- [ ] nmo_schema_registry.h/.c
- [ ] nmo_validator.h/.c
- [ ] nmo_migrator.h/.c
- [ ] nmo_builtin_schemas.c

### Phase 7: Object System
- [ ] nmo_object_repository.h/.c
- [ ] nmo_load_session.h/.c
- [ ] nmo_id_remap.h/.c
- [ ] Remapping tests

### Phase 8: Session Layer
- [ ] nmo_context.h/.c
- [ ] nmo_session.h/.c
- [ ] Thread safety tests

### Phase 9: Load Pipeline
- [ ] nmo_parser.h/.c
- [ ] 15-phase implementation
- [ ] Integration tests

### Phase 10: Save Pipeline
- [ ] nmo_builder.h/.c
- [ ] 14-phase implementation
- [ ] Integration tests

### Phase 11: Manager System
- [ ] nmo_manager_registry.h/.c
- [ ] Example managers

### Phase 12: Testing
- [ ] Test framework
- [ ] Unit tests
- [ ] Integration tests
- [ ] Fuzz tests

### Phase 13: CLI Tools
- [ ] nmo-inspect
- [ ] nmo-validate
- [ ] nmo-convert
- [ ] nmo-diff

### Phase 14: Documentation
- [ ] Doxygen
- [ ] Examples
- [ ] Tutorials

---

## Conclusion

This implementation plan provides a comprehensive roadmap for building libnmo from scratch. By following these phases systematically, you will create a production-ready, well-tested library that is fully compatible with the Virtools file format.

**Key Success Factors**:
1. Follow the layered architecture strictly
2. Test each phase thoroughly before moving to the next
3. Maintain zero circular dependencies
4. Document as you go
5. Run integration tests frequently

**Next Steps**:
1. Review this plan with the team
2. Set up development environment
3. Begin Phase 1: Project Setup
4. Establish CI/CD pipeline early
5. Create test fixtures from real Virtools files

Good luck with the implementation!
