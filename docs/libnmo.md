# libnmo - Complete Documentation

A modern C library for reading and writing Virtools file formats (.nmo/.cmo/.vmo).

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Core Layer](#2-core-layer)
3. [IO Layer](#3-io-layer)
4. [Format Layer](#4-format-layer)
5. [Schema System](#5-schema-system)
6. [Session Layer](#6-session-layer)
7. [Object System](#7-object-system)
8. [Load Pipeline](#8-load-pipeline)
9. [Save Pipeline](#9-save-pipeline)
10. [Manager System](#10-manager-system)
11. [Flags and Constants](#11-flags-and-constants)
12. [Module Structure](#12-module-structure)
13. [Build System](#13-build-system)
14. [Implementation Examples](#14-implementation-examples)
15. [Testing and Tools](#15-testing-and-tools)

---

## 1. Architecture Overview

### 1.1 Design Principles

**Single Responsibility**
- Each module has a clear, focused purpose
- Minimal coupling between layers
- Bottom-up dependencies only

**Layered Architecture**
```
Application → Session → Format → IO → Core
```

**Explicit Ownership**
- Context: Reference-counted, shared, thread-safe
- Session: Exclusive ownership, single-threaded
- Arena: Bulk allocation/deallocation
- Spans: Non-owning views
- Buffers: Owned data

**Composable IO**
```
File → Checksum → Compression → Parser
```

**Schema-Driven Operations**
- Schemas are pre-generated C code (checked into repository)
- Validation rules defined in schemas
- Serialization driven by field descriptors
- Type-safe through generated code

**Error Handling**
- Return codes for control flow
- Error stack for diagnostics
- Causal chains for error propagation
- Severity levels for filtering

### 1.2 Key Concepts

**GUID Format**
GUIDs are 8 bytes: `{d1, d2}` stored as two 32-bit integers.

**Pre-Generated Schemas**
Schemas are generated from external schema definitions and checked into the repository as C source files. The library does not depend on schema generation tools at build time.

**CKStateChunk**
The core serialization container used for all object and manager data. Supports:
- Binary serialization with DWORD alignment
- Object ID tracking for reference resolution
- Sub-chunk management for nested structures
- Compression integration
- Version handling (v4-v7)

**ID Remapping**
Objects maintain both file IDs (from loaded files) and runtime IDs (in memory). During load, IDs are remapped to avoid conflicts. During save, IDs are compacted to sequential values.

**Manager Hooks**
Managers can register callbacks for load/save operations:
- PreLoad: Initialize before loading
- PostLoad: Finalize after loading
- LoadData: Parse manager chunk
- SaveData: Serialize manager chunk
- PreSave: Prepare for saving
- PostSave: Cleanup after saving

---

## 2. Core Layer

Foundation components providing memory management, error handling, and logging.

### 2.1 Allocator

```c
// Allocator interface (wrapper around malloc/free or custom)
typedef void* (*nmo_alloc_fn)(void* user_data, size_t size, size_t alignment);
typedef void  (*nmo_free_fn)(void* user_data, void* ptr);

typedef struct nmo_allocator {
    nmo_alloc_fn alloc;
    nmo_free_fn  free;
    void*       user_data;
} nmo_allocator;

// Default allocator (uses malloc/free)
nmo_allocator nmo_allocator_default(void);

// Custom allocator
nmo_allocator nmo_allocator_custom(nmo_alloc_fn alloc, 
                                    nmo_free_fn free, 
                                    void* user_data);
```

### 2.2 Arena

```c
// Arena allocator (fast bump-pointer allocation)
typedef struct nmo_arena nmo_arena;

// Create arena with initial capacity
nmo_arena* nmo_arena_create(nmo_allocator* allocator, size_t initial_size);

// Allocate from arena
void* nmo_arena_alloc(nmo_arena* arena, size_t size, size_t alignment);

// Reset arena (free all allocations)
void nmo_arena_reset(nmo_arena* arena);

// Destroy arena
void nmo_arena_destroy(nmo_arena* arena);
```

**Usage Pattern:**
```c
nmo_arena* arena = nmo_arena_create(&allocator, 64 * 1024);

// Many allocations
char* str1 = nmo_arena_alloc(arena, 256, 1);
int* array = nmo_arena_alloc(arena, 1000 * sizeof(int), alignof(int));

// Free all at once
nmo_arena_destroy(arena);
```

### 2.3 Error System

```c
// Error codes
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

// Error severity
typedef enum {
    NMO_SEVERITY_DEBUG,
    NMO_SEVERITY_INFO,
    NMO_SEVERITY_WARNING,
    NMO_SEVERITY_ERROR,
    NMO_SEVERITY_FATAL,
} nmo_severity;

// Error structure
typedef struct nmo_error {
    nmo_error_code   code;
    nmo_severity    severity;
    const char*     message;
    const char*     file;
    int             line;
    struct nmo_error* cause;  // Causal chain
} nmo_error;

// Result type
typedef struct nmo_result {
    nmo_error_code  code;
    nmo_error*     error;  // NULL if code == NMO_OK
} nmo_result;

// Create error
nmo_error* nmo_error_create(nmo_arena* arena, 
                            nmo_error_code code,
                            nmo_severity severity,
                            const char* message);

// Add cause to error chain
void nmo_error_add_cause(nmo_error* err, nmo_error* cause);
```

### 2.4 Logger

```c
// Log level
typedef enum {
    NMO_LOG_DEBUG,
    NMO_LOG_INFO,
    NMO_LOG_WARN,
    NMO_LOG_ERROR,
} nmo_log_level;

// Logger interface
typedef void (*nmo_log_fn)(void* user_data, 
                           nmo_log_level level, 
                           const char* message);

typedef struct nmo_logger {
    nmo_log_fn   log;
    void*       user_data;
    nmo_log_level level;  // Minimum level to log
} nmo_logger;

// Logging functions
void nmo_log(nmo_logger* logger, nmo_log_level level, const char* format, ...);
void nmo_log_debug(nmo_logger* logger, const char* format, ...);
void nmo_log_info(nmo_logger* logger, const char* format, ...);
void nmo_log_warn(nmo_logger* logger, const char* format, ...);
void nmo_log_error(nmo_logger* logger, const char* format, ...);

// Predefined loggers
nmo_logger nmo_logger_stderr(void);
nmo_logger nmo_logger_null(void);
```

### 2.5 GUID

```c
// GUID structure (8 bytes: d1, d2)
typedef struct nmo_guid {
    uint32_t d1;
    uint32_t d2;
} nmo_guid;

// GUID comparison
int nmo_guid_equals(nmo_guid a, nmo_guid b);

// GUID hashing
uint32_t nmo_guid_hash(nmo_guid guid);

// GUID parsing
nmo_guid nmo_guid_parse(const char* str);

// GUID formatting
void nmo_guid_format(nmo_guid guid, char* buffer, size_t size);
```

---

## 3. IO Layer

Composable IO abstractions with support for files, memory buffers, compression, and checksums.

### 3.1 Base IO Interface

```c
// IO mode flags
typedef enum {
    NMO_IO_READ   = 0x01,
    NMO_IO_WRITE  = 0x02,
    NMO_IO_CREATE = 0x04,
} nmo_io_mode;

// IO operations
typedef int (*nmo_io_read_fn)(void* handle, void* buffer, size_t size, size_t* bytes_read);
typedef int (*nmo_io_write_fn)(void* handle, const void* buffer, size_t size);
typedef int (*nmo_io_seek_fn)(void* handle, int64_t offset, int whence);
typedef int64_t (*nmo_io_tell_fn)(void* handle);
typedef int (*nmo_io_close_fn)(void* handle);

// IO interface
typedef struct nmo_io_interface {
    nmo_io_read_fn   read;
    nmo_io_write_fn  write;
    nmo_io_seek_fn   seek;
    nmo_io_tell_fn   tell;
    nmo_io_close_fn  close;
    void*           handle;
} nmo_io_interface;

// Close IO
void nmo_io_close(nmo_io_interface* io);
```

### 3.2 File IO

```c
// Open file
nmo_io_interface* nmo_file_io_open(const char* path, nmo_io_mode mode);

// Example usage
nmo_io_interface* io = nmo_file_io_open("example.nmo", NMO_IO_READ);
if (!io) {
    return NMO_ERR_FILE_NOT_FOUND;
}

uint8_t buffer[4096];
size_t bytes_read;
io->read(io->handle, buffer, sizeof(buffer), &bytes_read);

nmo_io_close(io);
```

### 3.3 Memory IO

```c
// Open memory buffer for reading
nmo_io_interface* nmo_memory_io_open_read(const void* data, size_t size);

// Open memory buffer for writing (dynamic growth)
nmo_io_interface* nmo_memory_io_open_write(size_t initial_capacity);

// Get written data
const void* nmo_memory_io_get_data(nmo_io_interface* io, size_t* size);
```

### 3.4 Compressed IO

```c
// Compression codec
typedef enum {
    NMO_CODEC_ZLIB,
} nmo_compression_codec;

// Compression level (1-9)
typedef int nmo_compression_level;

// Compressed IO descriptor
typedef struct nmo_compressed_io_desc {
    nmo_compression_codec codec;
    nmo_compression_level level;       // For writing
    size_t               buffer_size;  // Internal buffer
} nmo_compressed_io_desc;

// Wrap IO with compression
nmo_io_interface* nmo_compressed_io_wrap(nmo_io_interface* inner,
                                       nmo_compressed_io_desc* desc);

// Example: Read compressed file
nmo_io_interface* file = nmo_file_io_open("data.zlib", NMO_IO_READ);
nmo_compressed_io_desc desc = {
    .codec = NMO_CODEC_ZLIB,
    .buffer_size = 64 * 1024
};
nmo_io_interface* compressed = nmo_compressed_io_wrap(file, &desc);

// Read decompressed data
uint8_t buffer[1024];
size_t bytes_read;
compressed->read(compressed->handle, buffer, sizeof(buffer), &bytes_read);

nmo_io_close(compressed);  // Also closes inner file IO
```

### 3.5 Checksum IO

```c
// Checksum algorithm
typedef enum {
    NMO_CHECKSUM_ADLER32,
    NMO_CHECKSUM_CRC32,
} nmo_checksum_algorithm;

// Checksummed IO descriptor
typedef struct nmo_checksummed_io_desc {
    nmo_checksum_algorithm algorithm;
    uint32_t              initial_value;
} nmo_checksummed_io_desc;

// Wrap IO with checksum
nmo_io_interface* nmo_checksummed_io_wrap(nmo_io_interface* inner,
                                        nmo_checksummed_io_desc* desc);

// Get computed checksum
uint32_t nmo_checksummed_io_get_checksum(nmo_io_interface* io);
```

### 3.6 Transactional IO

```c
// Transaction durability
typedef enum {
    NMO_TXN_NONE,      // No fsync
    NMO_TXN_FSYNC,     // fsync after commit
    NMO_TXN_FDATASYNC, // fdatasync after commit
} nmo_txn_durability;

// Transaction descriptor
typedef struct nmo_txn_desc {
    const char*        path;
    nmo_txn_durability durability;
    const char*        staging_dir;  // NULL for default
} nmo_txn_desc;

typedef struct nmo_txn_handle nmo_txn_handle;

// Open transaction (writes to staging file)
nmo_txn_handle* nmo_txn_open(nmo_txn_desc* desc);

// Write to transaction
int nmo_txn_write(nmo_txn_handle* txn, const void* data, size_t size);

// Commit transaction (atomic rename)
int nmo_txn_commit(nmo_txn_handle* txn);

// Rollback transaction (delete staging file)
void nmo_txn_rollback(nmo_txn_handle* txn);

// Close transaction
void nmo_txn_close(nmo_txn_handle* txn);

// Multi-file transaction scope
typedef struct nmo_txn_scope nmo_txn_scope;

nmo_txn_scope* nmo_txn_scope_create(void);
void nmo_txn_scope_add(nmo_txn_scope* scope, nmo_txn_handle* txn);
int nmo_txn_scope_commit(nmo_txn_scope* scope);  // All or nothing
void nmo_txn_scope_rollback(nmo_txn_scope* scope);
void nmo_txn_scope_destroy(nmo_txn_scope* scope);
```

---

## 4. Format Layer

Virtools-specific binary format handling for headers, chunks, objects, and managers.

### 4.1 File Header

```c
// File header (Part0)
typedef struct nmo_file_header {
    char     signature[8];           // "Nemo Fi\0"
    uint32_t file_version;           // 2-9
    uint32_t ck_version;             // CK version
    uint32_t hdr1_pack_size;         // Header1 compressed size
    uint32_t hdr1_unpack_size;       // Header1 uncompressed size
    uint32_t data_pack_size;         // Data compressed size
    uint32_t data_unpack_size;       // Data uncompressed size
    uint32_t crc;                    // Adler-32 checksum (v8+)
} nmo_file_header;

// Parse file header
int nmo_header_parse(nmo_io_interface* io, nmo_file_header* header);

// Validate header
int nmo_header_validate(nmo_file_header* header);
```

### 4.2 Header1 (Object Descriptors)

```c
// Object descriptor
typedef struct nmo_object_desc {
    nmo_object_id  file_id;       // Object ID (bit 23 = reference flag)
    nmo_class_id   class_id;
    const char*   name;
    nmo_object_id  parent_id;
} nmo_object_desc;

// Plugin dependency
typedef struct nmo_plugin_dep {
    nmo_guid       guid;
    nmo_plugin_category category;
    uint32_t      version;
} nmo_plugin_dep;

// Header1 structure
typedef struct nmo_header1 {
    uint32_t         object_count;
    uint32_t         manager_count;
    uint32_t         max_object_id;
    
    nmo_object_desc*  objects;
    nmo_plugin_dep*   plugin_deps;
    size_t           plugin_dep_count;
    
    // Included files
    const char**     included_files;
    size_t          included_file_count;
} nmo_header1;

// Parse Header1
int nmo_header1_parse(const void* data, size_t size, 
                      nmo_header1* header, nmo_arena* arena);

// Build Header1
nmo_buffer* nmo_header1_serialize(nmo_header1* header);
```

### 4.3 CKStateChunk

CKStateChunk is the core serialization container used for all object and manager data.

#### Binary Format

```
┌─────────────────────────────────────────────────────────┐
│ Version Info (DWORD)                                    │
│  [31:24] ChunkOptions                                   │
│  [23:16] ChunkVersion (4,5,6,7)                        │
│  [15:8]  ChunkClassID (legacy, usually 0)              │
│  [7:0]   DataVersion (object-specific version)         │
├─────────────────────────────────────────────────────────┤
│ Chunk Size (DWORD) - size of payload in DWORDs         │
├─────────────────────────────────────────────────────────┤
│ Payload Data (N DWORDs, DWORD-aligned)                 │
├─────────────────────────────────────────────────────────┤
│ Optional Lists:                                         │
│                                                         │
│ IF (ChunkOptions & CHNK_OPTION_IDS):                  │
│   ├─ ID Count (DWORD)                                 │
│   ├─ ID Array Offsets (DWORD[ID Count])              │
│                                                         │
│ IF (ChunkOptions & CHNK_OPTION_CHN):                  │
│   ├─ Sub-Chunk Count (DWORD)                          │
│   ├─ Sub-Chunk Array Offsets (DWORD[Sub-Chunk Count])│
│                                                         │
│ IF (ChunkOptions & CHNK_OPTION_MAN):                  │
│   ├─ Manager Int Count (DWORD)                        │
│   ├─ Manager Int Array Offsets (DWORD[Manager Count])│
└─────────────────────────────────────────────────────────┘
```

#### Chunk Options

```c
typedef enum {
    CHNK_OPTION_IDS      = 0x01,  // Chunk contains object IDs
    CHNK_OPTION_CHN      = 0x02,  // Chunk contains sub-chunks
    CHNK_OPTION_MAN      = 0x04,  // Chunk contains manager ints
    CHNK_OPTION_FILE     = 0x08,  // IDs are file-relative
    CHNK_OPTION_ALLOWDYN = 0x10,  // Allow dynamic resizing
    CHNK_OPTION_LISTBIG  = 0x20,  // Use 16-bit list sizes
    CHNK_OPTION_DONTDELETE_PTR    = 0x40,
    CHNK_OPTION_DONTDELETE_PARSER = 0x80
} nmo_chunk_options;

typedef enum {
    CHUNK_VERSION1 = 4,
    CHUNK_VERSION2 = 5,
    CHUNK_VERSION3 = 6,
    CHUNK_VERSION4 = 7   // Current version
} nmo_chunk_version;
```

#### Runtime Structure

```c
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
    nmo_id_list       ids;               // Object ID positions
    nmo_chunk_list    chunks;            // Sub-chunks
    nmo_manager_list  managers;          // Manager ints
    
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

#### Chunk Parser

```c
typedef struct nmo_chunk_parser nmo_chunk_parser;

// Create parser
nmo_chunk_parser* nmo_chunk_parser_create(nmo_chunk* chunk);

// Position management
size_t nmo_chunk_parser_tell(nmo_chunk_parser* p);
int nmo_chunk_parser_seek(nmo_chunk_parser* p, size_t pos);
int nmo_chunk_parser_skip(nmo_chunk_parser* p, size_t dwords);
size_t nmo_chunk_parser_remaining(nmo_chunk_parser* p);
int nmo_chunk_parser_at_end(nmo_chunk_parser* p);

// Bounds-checked reading
int nmo_chunk_parser_read_dword(nmo_chunk_parser* p, uint32_t* out);
int nmo_chunk_parser_read_float(nmo_chunk_parser* p, float* out);
int nmo_chunk_parser_read_bytes(nmo_chunk_parser* p, void* dest, size_t bytes);
int nmo_chunk_parser_read_string(nmo_chunk_parser* p, char** out, nmo_arena* arena);

// Object ID tracking
int nmo_chunk_parser_read_object_id(nmo_chunk_parser* p, nmo_object_id* out);

void nmo_chunk_parser_destroy(nmo_chunk_parser* p);
```

#### Chunk Writer

```c
typedef struct nmo_chunk_writer nmo_chunk_writer;

// Create writer
nmo_chunk_writer* nmo_chunk_writer_create(nmo_arena* arena);

// Start chunk
void nmo_chunk_writer_start(nmo_chunk_writer* w, 
                            nmo_class_id class_id,
                            uint32_t chunk_version);

// Write operations
int nmo_chunk_writer_write_dword(nmo_chunk_writer* w, uint32_t value);
int nmo_chunk_writer_write_float(nmo_chunk_writer* w, float value);
int nmo_chunk_writer_write_bytes(nmo_chunk_writer* w, const void* data, size_t bytes);
int nmo_chunk_writer_write_string(nmo_chunk_writer* w, const char* str);

// Object ID tracking
int nmo_chunk_writer_write_object_id(nmo_chunk_writer* w, nmo_object_id id);

// Sub-chunk management
int nmo_chunk_writer_start_sub_chunk(nmo_chunk_writer* w);
int nmo_chunk_writer_end_sub_chunk(nmo_chunk_writer* w);

// Finalize
nmo_chunk* nmo_chunk_writer_finalize(nmo_chunk_writer* w);
void nmo_chunk_writer_destroy(nmo_chunk_writer* w);
```

#### Serialization/Deserialization

```c
// Serialize chunk to buffer
nmo_buffer* nmo_chunk_serialize(nmo_chunk* chunk);

// Deserialize chunk from buffer
nmo_chunk* nmo_chunk_deserialize(const void* data, size_t size, nmo_arena* arena);

// ID remapping
int nmo_chunk_remap_ids(nmo_chunk* chunk, nmo_id_remap_table* table);
```

### 4.4 Object Metadata

```c
typedef struct nmo_object {
    nmo_object_id      id;
    nmo_class_id       class_id;
    const char*        name;
    uint32_t          flags;
    
    // Hierarchy
    nmo_object*        parent;
    nmo_object**       children;
    size_t            child_count;
    
    // Data
    nmo_chunk*         chunk;
    void*             data;
    
    // Metadata
    nmo_object_id      file_index;
    uint32_t          creation_flags;
    uint32_t          save_flags;
    
    nmo_arena*         arena;
} nmo_object;
```

### 4.5 Manager Data

```c
typedef struct nmo_manager {
    nmo_guid       guid;
    const char*   name;
    nmo_plugin_category category;
    
    // Hooks
    int (*pre_load)(void* session, void* user_data);
    int (*post_load)(void* session, void* user_data);
    int (*load_data)(void* session, const nmo_chunk* chunk, void* user_data);
    nmo_chunk* (*save_data)(void* session, void* user_data);
    int (*pre_save)(void* session, void* user_data);
    int (*post_save)(void* session, void* user_data);
    
    void* user_data;
} nmo_manager;
```

---

## 5. Schema System

Schemas define the structure of objects and drive serialization, validation, and migration.

### 5.1 Schema Descriptors

Schemas are pre-generated C code (checked into the repository).

```c
// Field types
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

// Field descriptor
typedef struct nmo_field_descriptor {
    const char*     name;
    nmo_field_type  type;
    size_t         offset;
    size_t         size;
    
    // For arrays
    size_t         count;
    
    // For object references
    nmo_class_id    class_id;
    
    // Validation
    const char*     validation_rule;
} nmo_field_descriptor;

// Schema descriptor
typedef struct nmo_schema_descriptor {
    nmo_class_id              class_id;
    const char*              class_name;
    nmo_class_id              parent_class_id;
    
    nmo_field_descriptor*     fields;
    size_t                   field_count;
    
    uint32_t                 chunk_version;
    
    // Serialization functions (generated)
    void (*serialize_fn)(nmo_object* obj, nmo_chunk_writer* writer);
    void (*deserialize_fn)(nmo_object* obj, nmo_chunk_parser* parser);
} nmo_schema_descriptor;
```

### 5.2 Schema Registry

```c
typedef struct nmo_schema_registry nmo_schema_registry;

// Create registry
nmo_schema_registry* nmo_schema_registry_create(nmo_allocator* allocator);

// Register schema
int nmo_schema_registry_add(nmo_schema_registry* registry, 
                            const nmo_schema_descriptor* schema);

// Register built-in schemas (pre-generated)
int nmo_schema_registry_add_builtin(nmo_schema_registry* registry);

// Lookup
const nmo_schema_descriptor* nmo_schema_registry_find_by_id(
    nmo_schema_registry* registry, 
    nmo_class_id class_id);

const nmo_schema_descriptor* nmo_schema_registry_find_by_name(
    nmo_schema_registry* registry, 
    const char* class_name);

// Verify consistency
int nmo_verify_schema_consistency(nmo_schema_registry* registry);

void nmo_schema_registry_destroy(nmo_schema_registry* registry);
```

### 5.3 Validator

```c
// Validation mode
typedef enum {
    NMO_VALIDATION_STRICT,      // Fail on any violation
    NMO_VALIDATION_PERMISSIVE,  // Warn but continue
} nmo_validation_mode;

// Validation result
typedef enum {
    NMO_VALID,
    NMO_VALID_WITH_WARNINGS,
    NMO_INVALID,
} nmo_validation_result;

typedef struct nmo_validation nmo_validation;

// Create validator
nmo_validation* nmo_validation_create(nmo_schema_registry* registry,
                                      nmo_validation_mode mode);

// Validate object
nmo_validation_result nmo_validate_object(nmo_validation* v, nmo_object* obj);

// Validate file
nmo_validation_result nmo_validate_file(nmo_validation* v, const char* path);

void nmo_validation_destroy(nmo_validation* v);
```

### 5.4 Migrator

```c
typedef struct nmo_migrator nmo_migrator;

// Create migrator
nmo_migrator* nmo_migrator_create(nmo_schema_registry* registry);

// Migrate chunk to target version
int nmo_migrate_chunk(nmo_migrator* m, 
                      nmo_chunk* chunk, 
                      uint32_t target_version);

void nmo_migrator_destroy(nmo_migrator* m);
```

---

## 6. Session Layer

Global context and per-operation session management.

### 6.1 Context

```c
// Context descriptor
typedef struct nmo_context_desc {
    nmo_allocator*  allocator;
    nmo_logger*     logger;
    int             thread_pool_size;
} nmo_context_desc;

typedef struct nmo_context nmo_context;

// Create context (reference counted)
nmo_context* nmo_context_create(nmo_context_desc* desc);

// Retain context
void nmo_context_retain(nmo_context* ctx);

// Release context
void nmo_context_release(nmo_context* ctx);
```

### 6.2 Session

```c
typedef struct nmo_session nmo_session;

// Create session (borrows context)
nmo_session* nmo_session_create(nmo_context* ctx);

// Get file info after load
typedef struct nmo_file_info {
    uint32_t file_version;
    uint32_t ck_version;
    size_t   file_size;
    uint32_t object_count;
    uint32_t manager_count;
    uint32_t write_mode;
} nmo_file_info;

nmo_file_info nmo_session_get_file_info(nmo_session* session);

// Destroy session
void nmo_session_destroy(nmo_session* session);
```

---

## 7. Object System

Object repository, ID remapping, and load session management.

### 7.1 Object Repository

```c
typedef struct nmo_object_repository nmo_object_repository;

// Create repository
nmo_object_repository* nmo_object_repository_create(nmo_arena* arena);

// Create object
nmo_object* nmo_object_create(nmo_object_repository* repo,
                              nmo_class_id class_id,
                              const char* name,
                              uint32_t flags);

// Find objects
nmo_object* nmo_object_find_by_id(nmo_object_repository* repo,
                                  nmo_object_id id);

nmo_object* nmo_object_find_by_name(nmo_object_repository* repo,
                                    const char* name);

nmo_object** nmo_object_find_by_class(nmo_object_repository* repo,
                                      nmo_class_id class_id,
                                      size_t* count);

void nmo_object_repository_destroy(nmo_object_repository* repo);
```

### 7.2 Load Session Management

```c
typedef struct nmo_load_session {
    nmo_object_repository* repo;
    nmo_object_id         saved_id_max;
    nmo_object_id         id_base;
    nmo_hash_table*       file_to_runtime;
    int                  active;
} nmo_load_session;

// Start load session (reserves ID range)
int nmo_load_session_start(nmo_object_repository* repo, 
                           nmo_object_id max_saved_id);

// Register loaded object
int nmo_load_session_register(nmo_load_session* session,
                              nmo_object* obj,
                              nmo_object_id file_id);

// End load session
int nmo_load_session_end(nmo_load_session* session);
```

### 7.3 ID Remapping

```c
// ID remap table
typedef struct nmo_id_remap_table {
    nmo_hash_table*        map;
    nmo_id_remap_entry*    entries;
    size_t                entry_count;
} nmo_id_remap_table;

// Build remap table from load session
nmo_id_remap_table* nmo_build_remap_table(nmo_load_session* session);

// Lookup remapped ID
int nmo_id_remap_lookup(nmo_id_remap_table* table,
                        nmo_object_id old_id,
                        nmo_object_id* new_id);

// Remap plan for save operations
typedef struct nmo_id_remap_plan {
    nmo_id_remap_table* table;
    nmo_hash_table*    name_to_id;
    size_t            objects_remapped;
    size_t            conflicts_resolved;
} nmo_id_remap_plan;

// Create remap plan for save
nmo_id_remap_plan* nmo_id_remap_plan_create(nmo_object_repository* repo,
                                            nmo_object** objects_to_save,
                                            size_t object_count);

void nmo_id_remap_plan_destroy(nmo_id_remap_plan* plan);
```

---

## 8. Load Pipeline

Complete 15-phase load workflow.

```c
// Load flags
typedef enum {
    NMO_LOAD_DEFAULT            = 0,
    NMO_LOAD_DODIALOG           = 0x0001,
    NMO_LOAD_AUTOMATICMODE      = 0x0002,
    NMO_LOAD_CHECKDUPLICATES    = 0x0004,
    NMO_LOAD_AS_DYNAMIC_OBJECT  = 0x0008,
    NMO_LOAD_ONLYBEHAVIORS      = 0x0010,
    NMO_LOAD_CHECK_DEPENDENCIES = 0x0020,
} nmo_load_flags;

// Load file
nmo_result nmo_load_file(nmo_session* session, 
                         const char* path, 
                         nmo_load_flags flags);
```

### 15-Phase Load Workflow

```c
int nmo_load_file_impl(nmo_session* session, const char* path, nmo_load_flags flags) {
    // Phase 1: Open IO
    nmo_io_interface* io = nmo_file_io_open(path, NMO_IO_READ);
    
    // Phase 2: Parse file header
    nmo_file_header header;
    nmo_header_parse(io, &header);
    nmo_header_validate(&header);
    
    // Phase 3: Read and decompress Header1
    nmo_buffer* hdr1_compressed = nmo_buffer_read(io, header.hdr1_pack_size);
    nmo_buffer* hdr1_data = nmo_decompress_buffer(hdr1_compressed, NMO_CODEC_ZLIB, 
                                                   header.hdr1_unpack_size);
    
    // Phase 4: Parse Header1
    nmo_header1 hdr1;
    nmo_header1_parse(hdr1_data->data, hdr1_data->size, &hdr1, session->arena);
    
    // Phase 5: Start load session
    nmo_load_session_start(session->repo, hdr1.max_object_id);
    
    // Phase 6: Check plugin dependencies
    for (size_t i = 0; i < hdr1.plugin_dep_count; i++) {
        nmo_plugin_dep* dep = &hdr1.plugin_deps[i];
        // Verify plugin is available
    }
    
    // Phase 7: Manager pre-load hooks
    nmo_execute_managers_pre_load(session);
    
    // Phase 8: Read and decompress data section
    nmo_buffer* data_compressed = nmo_buffer_read(io, header.data_pack_size);
    nmo_buffer* data = nmo_decompress_buffer(data_compressed, NMO_CODEC_ZLIB,
                                             header.data_unpack_size);
    
    // Phase 9: Parse manager chunks
    size_t offset = 0;
    for (size_t i = 0; i < hdr1.manager_count; i++) {
        uint32_t size = *(uint32_t*)&data->data[offset];
        offset += 4;
        
        nmo_chunk* chunk = nmo_chunk_deserialize(&data->data[offset], size, session->arena);
        offset += size;
        
        // Call manager load_data hook
        nmo_manager* mgr = nmo_find_manager_by_guid(session, chunk->guid);
        if (mgr && mgr->load_data) {
            mgr->load_data(session, chunk, mgr->user_data);
        }
    }
    
    // Phase 10: Create objects
    for (size_t i = 0; i < hdr1.object_count; i++) {
        nmo_object_desc* desc = &hdr1.objects[i];
        
        // Skip reference-only objects
        if (desc->file_id & NMO_OBJECT_REFERENCE_FLAG) {
            continue;
        }
        
        nmo_object* obj = nmo_object_create(session->repo, desc->class_id, 
                                           desc->name, 0);
        nmo_load_session_register(session->load_session, obj, desc->file_id);
    }
    
    // Phase 11: Parse object chunks
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
    
    // Phase 12: Build ID remap table
    nmo_id_remap_table* remap_table = nmo_build_remap_table(session->load_session);
    
    // Phase 13: Remap IDs in all chunks
    for (size_t i = 0; i < session->repo->object_count; i++) {
        nmo_object* obj = session->repo->objects[i];
        if (obj->chunk) {
            nmo_chunk_remap_ids(obj->chunk, remap_table);
        }
    }
    
    // Phase 14: Deserialize objects
    for (size_t i = 0; i < session->repo->object_count; i++) {
        nmo_object* obj = session->repo->objects[i];
        const nmo_schema_descriptor* schema = 
            nmo_schema_registry_find_by_id(session->context->schema_registry, 
                                          obj->class_id);
        
        if (schema && schema->deserialize_fn && obj->chunk) {
            schema->deserialize_fn(obj, nmo_chunk_parser_create(obj->chunk));
        }
    }
    
    // Phase 15: Manager post-load hooks
    nmo_execute_managers_post_load(session);
    
    nmo_io_close(io);
    return NMO_OK;
}
```

---

## 9. Save Pipeline

Complete 14-phase save workflow.

```c
// Save options
typedef struct nmo_save_options {
    uint32_t              file_version;
    nmo_file_write_mode   write_mode;
    nmo_compression_level compress_level;
} nmo_save_options;

// Save builder
typedef struct nmo_save_builder nmo_save_builder;

// Create save builder
nmo_save_builder* nmo_save_builder_create(nmo_session* session);

// Add objects to save
void nmo_save_builder_add(nmo_save_builder* builder, nmo_object* obj);
void nmo_save_builder_add_all(nmo_save_builder* builder);

// Set options
void nmo_save_builder_set_options(nmo_save_builder* builder, 
                                  nmo_save_options* opts);

// Execute save
nmo_result nmo_save_builder_execute(nmo_save_builder* builder, 
                                    const char* path);

void nmo_save_builder_destroy(nmo_save_builder* builder);
```

### 14-Phase Save Workflow

```c
int nmo_save_builder_execute_impl(nmo_save_builder* builder, const char* path) {
    // Phase 1: Manager pre-save hooks
    nmo_execute_managers_pre_save(builder->session);
    
    // Phase 2: Build ID remap plan
    nmo_id_remap_plan* plan = nmo_id_remap_plan_create(builder->session->repo,
                                                       builder->objects,
                                                       builder->object_count);
    
    // Phase 3: Serialize manager chunks
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
    
    // Phase 4: Serialize object chunks
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
    
    // Phase 5: Build Header1
    nmo_header1 header1 = {0};
    header1.object_count = builder->object_count;
    header1.manager_count = builder->manager_count;
    header1.max_object_id = builder->object_count;
    
    header1.objects = nmo_arena_alloc(builder->session->arena,
                                     header1.object_count * sizeof(nmo_object_desc),
                                     alignof(nmo_object_desc));
    
    for (size_t i = 0; i < builder->object_count; i++) {
        nmo_object* obj = builder->objects[i];
        nmo_object_desc* desc = &header1.objects[i];
        
        nmo_object_id new_id;
        nmo_id_remap_lookup(plan->table, obj->id, &new_id);
        
        desc->file_id = new_id;
        desc->class_id = obj->class_id;
        desc->name = obj->name;
        desc->parent_id = 0;
        
        if (obj->save_flags & NMO_OBJECT_ONLYFORFILEREFERENCE) {
            desc->file_id |= NMO_OBJECT_REFERENCE_FLAG;
        }
    }
    
    header1.plugin_deps = nmo_compute_plugin_dependencies(builder);
    
    // Phase 6: Serialize Header1
    nmo_buffer* header1_buf = nmo_serialize_header1(&header1);
    
    // Phase 7: Combine data buffers
    nmo_buffer* data_buf = nmo_buffer_create(manager_buffer->size + object_buffer->size);
    nmo_buffer_append(data_buf, manager_buffer->data, manager_buffer->size);
    nmo_buffer_append(data_buf, object_buffer->data, object_buffer->size);
    
    // Phase 8: Apply compression
    nmo_buffer* final_data = data_buf;
    size_t uncompressed_size = data_buf->size;
    
    if (builder->options.write_mode & NMO_WRITE_MODE_COMPRESSED) {
        final_data = nmo_compress_buffer(data_buf, NMO_CODEC_ZLIB, 
                                         builder->options.compress_level);
    }
    
    // Phase 9: Build file header
    nmo_file_header header = {0};
    memcpy(header.signature, "Nemo Fi\0", 8);
    header.file_version = builder->options.file_version;
    header.hdr1_pack_size = (uint32_t)header1_buf->size;
    header.hdr1_unpack_size = (uint32_t)header1_buf->size;
    header.data_pack_size = (uint32_t)final_data->size;
    header.data_unpack_size = (uint32_t)uncompressed_size;
    
    // Phase 10: Compute checksum
    if (header.file_version >= 8) {
        uint32_t crc = nmo_adler32(0, &header, sizeof(header));
        crc = nmo_adler32(crc, header1_buf->data, header1_buf->size);
        crc = nmo_adler32(crc, final_data->data, final_data->size);
        header.crc = crc;
    }
    
    // Phase 11: Open transaction
    nmo_txn_handle* txn = nmo_txn_open(&(nmo_txn_desc){
        .path = path,
        .durability = NMO_TXN_FSYNC
    });
    
    // Phase 12: Write data
    nmo_txn_write(txn, &header, sizeof(header));
    nmo_txn_write(txn, header1_buf->data, header1_buf->size);
    nmo_txn_write(txn, final_data->data, final_data->size);
    
    // Phase 13: Commit transaction
    int result = nmo_txn_commit(txn);
    nmo_txn_close(txn);
    
    // Phase 14: Manager post-save hooks
    if (result == NMO_OK) {
        nmo_execute_managers_post_save(builder->session);
    }
    
    return result;
}
```

---

## 10. Manager System

Managers provide hooks for initialization, data loading/saving, and cleanup.

### 10.1 Manager Structure

```c
typedef struct nmo_manager {
    nmo_guid       guid;
    const char*   name;
    nmo_plugin_category category;
    
    // Hooks
    int (*pre_load)(void* session, void* user_data);
    int (*post_load)(void* session, void* user_data);
    int (*load_data)(void* session, const nmo_chunk* chunk, void* user_data);
    nmo_chunk* (*save_data)(void* session, void* user_data);
    int (*pre_save)(void* session, void* user_data);
    int (*post_save)(void* session, void* user_data);
    
    void* user_data;
} nmo_manager;
```

### 10.2 Manager Registry

```c
typedef struct nmo_manager_registry nmo_manager_registry;

// Create registry
nmo_manager_registry* nmo_manager_registry_create(nmo_allocator* allocator);

// Register manager
int nmo_manager_register(nmo_manager_registry* registry, nmo_manager* mgr);

// Find manager
nmo_manager* nmo_manager_find_by_guid(nmo_manager_registry* registry, nmo_guid guid);

void nmo_manager_registry_destroy(nmo_manager_registry* registry);
```

### 10.3 Manager Hooks

**Load Sequence:**
1. `pre_load`: Initialize before loading objects
2. `load_data`: Parse manager chunk (called during Phase 9)
3. `post_load`: Finalize after all objects loaded

**Save Sequence:**
1. `pre_save`: Prepare for saving
2. `save_data`: Serialize manager chunk (called during Phase 3)
3. `post_save`: Cleanup after save

### 10.4 Example Manager Implementation

```c
// Render manager GUID
const nmo_guid NMO_GUID_RENDER_MANAGER = { 0x6BED328B, 0x141F04, 0x4523A08C, 0x23052460 };

// Manager state
typedef struct render_manager_state {
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t bpp;
} render_manager_state;

// Pre-load hook
int render_manager_pre_load(void* session, void* user_data) {
    render_manager_state* state = (render_manager_state*)user_data;
    // Initialize state
    return NMO_OK;
}

// Load data hook
int render_manager_load_data(void* session, const nmo_chunk* chunk, void* user_data) {
    render_manager_state* state = (render_manager_state*)user_data;
    nmo_chunk_parser* parser = nmo_chunk_parser_create((nmo_chunk*)chunk);
    
    nmo_chunk_parser_read_dword(parser, &state->screen_width);
    nmo_chunk_parser_read_dword(parser, &state->screen_height);
    nmo_chunk_parser_read_dword(parser, &state->bpp);
    
    nmo_chunk_parser_destroy(parser);
    return NMO_OK;
}

// Save data hook
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

// Register manager
void register_render_manager(nmo_context* ctx) {
    render_manager_state* state = calloc(1, sizeof(render_manager_state));
    
    nmo_manager render_mgr = {
        .guid = NMO_GUID_RENDER_MANAGER,
        .name = "CKRenderManager",
        .category = NMO_PLUGIN_RENDER_DLL,
        .pre_load = render_manager_pre_load,
        .load_data = render_manager_load_data,
        .save_data = render_manager_save_data,
        .user_data = state
    };
    
    nmo_manager_register(ctx->manager_registry, &render_mgr);
}
```

---

## 11. Flags and Constants

### 11.1 Load Flags

```c
typedef enum {
    NMO_LOAD_DEFAULT            = 0,
    NMO_LOAD_DODIALOG           = 0x0001,
    NMO_LOAD_AUTOMATICMODE      = 0x0002,
    NMO_LOAD_CHECKDUPLICATES    = 0x0004,
    NMO_LOAD_AS_DYNAMIC_OBJECT  = 0x0008,
    NMO_LOAD_ONLYBEHAVIORS      = 0x0010,
    NMO_LOAD_CHECK_DEPENDENCIES = 0x0020,
} nmo_load_flags;
```

### 11.2 Object Creation Flags

```c
typedef enum {
    NMO_OBJECTCREATION_NONAMECHECK = 0x0001,
    NMO_OBJECTCREATION_REPLACE     = 0x0002,
    NMO_OBJECTCREATION_RENAME      = 0x0004,
    NMO_OBJECTCREATION_ASK         = 0x0008,
    NMO_OBJECTCREATION_DYNAMIC     = 0x0010,
} nmo_object_creation_flags;
```

### 11.3 Object Save Flags

```c
typedef enum {
    NMO_OBJECT_ONLYFORFILEREFERENCE = 0x0001,  // Reference-only (bit 23 in file_id)
    NMO_OBJECT_SKIPLOAD             = 0x0002,
} nmo_object_save_flags;

#define NMO_OBJECT_REFERENCE_FLAG 0x00800000  // Bit 23 in object ID
```

### 11.4 File Write Mode

```c
typedef enum {
    NMO_WRITE_MODE_UNCOMPRESSED       = 0x0000,
    NMO_WRITE_MODE_COMPRESSED         = 0x0001,
    NMO_WRITE_MODE_CHUNKCOMPRESSED    = 0x0002,
    NMO_WRITE_MODE_EXTERNAL_BITMAPS   = 0x0004,
    NMO_WRITE_MODE_SHARED_BITMAPS     = 0x0008,
} nmo_file_write_mode;
```

### 11.5 Plugin Categories

```c
typedef enum {
    NMO_PLUGIN_BEHAVIOR_DLL  = 0,
    NMO_PLUGIN_MANAGER_DLL   = 1,
    NMO_PLUGIN_RENDER_DLL    = 2,
    NMO_PLUGIN_SOUND_DLL     = 3,
} nmo_plugin_category;
```

### 11.6 Class IDs

```c
typedef enum {
    NMO_CID_OBJECT          = 0x00000001,
    NMO_CID_BEOBJECT        = 0x00000002,
    NMO_CID_SCENEOBJECT     = 0x00000003,
    NMO_CID_3DENTITY        = 0x00000004,
    NMO_CID_3DOBJECT        = 0x00000005,
    NMO_CID_CAMERA          = 0x00000006,
    NMO_CID_LIGHT           = 0x00000007,
    NMO_CID_MESH            = 0x00000008,
    NMO_CID_MATERIAL        = 0x00000009,
    NMO_CID_TEXTURE         = 0x0000000A,
    NMO_CID_BEHAVIOR        = 0x00000014,
    NMO_CID_PARAMETER       = 0x00000015,
} nmo_class_id;
```

---

## 12. Module Structure

### 12.1 Dependency Graph

```
┌─────────────────────────────────────────────────────────────┐
│                    APPLICATION LAYER                        │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────────┐
│                    nmo_context                              │
│  Global state, ref-counted, thread-safe                     │
└─┬───────────────────────────────────────────────────────┬───┘
  │                                                       │
┌─▼───────────────────────┐         ┌────────────────────▼────┐
│  nmo_session            │         │  nmo_plugin_registry    │
│  - Load/save workflows  │         │  - Plugin metadata      │
└─┬───────────────────────┘         └─────────────────────────┘
  │
  ├──▶  nmo_parser (Load Pipeline)
  └──▶  nmo_builder (Save Pipeline)

┌─────────────────────────────────────────────────────────────┐
│                      SCHEMA LAYER                           │
│  - nmo_schema_registry (pre-generated schemas)              │
│  - nmo_validator                                            │
│  - nmo_migrator                                             │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────────┐
│                      FORMAT LAYER                           │
│  - nmo_header (Part0/Part1/Header1)                         │
│  - nmo_chunk (CKStateChunk)                                 │
│  - nmo_object                                               │
│  - nmo_manager                                              │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────────┐
│                        IO LAYER                             │
│  - nmo_io (base interface)                                  │
│  - nmo_io_file, nmo_io_memory                               │
│  - nmo_io_compressed, nmo_io_checksum                       │
│  - nmo_txn (transactional)                                  │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────────┐
│                       CORE LAYER                            │
│  - nmo_allocator, nmo_arena                                 │
│  - nmo_error, nmo_logger                                    │
│  - nmo_guid                                                 │
└─────────────────────────────────────────────────────────────┘
```

### 12.2 Module Responsibilities

| Layer | Module | Responsibility | Thread Safety |
|-------|--------|----------------|---------------|
| Core | nmo_allocator | Memory allocation abstraction | User-defined |
| | nmo_arena | Fast session-local allocator | Single-threaded |
| | nmo_error | Structured error handling | Single-threaded |
| | nmo_logger | Diagnostic output | Thread-safe |
| | nmo_guid | GUID operations | Stateless |
| IO | nmo_io | I/O abstraction | User-defined |
| | nmo_io_file | File system I/O | Thread-safe |
| | nmo_io_memory | Memory buffer I/O | Thread-safe |
| | nmo_io_compressed | Compression decorator | Thread-safe |
| | nmo_io_checksum | Checksum decorator | Thread-safe |
| | nmo_txn | Transactional I/O | Thread-safe |
| Format | nmo_header | Header parsing/building | Single-threaded |
| | nmo_chunk | Chunk serialization | Single-threaded |
| | nmo_object | Object metadata | Single-threaded |
| | nmo_manager | Manager data | Single-threaded |
| Schema | nmo_schema_registry | Schema lookup | Thread-safe (RW lock) |
| | nmo_validator | Validation operations | Single-threaded |
| | nmo_migrator | Version migration | Single-threaded |
| Session | nmo_parser | Load pipeline | Single-threaded |
| | nmo_builder | Save pipeline | Single-threaded |
| | nmo_id_remap | ID remapping | Single-threaded |
| Application | nmo_context | Global state | Thread-safe (ref-counted) |
| | nmo_session | Operation state | Single-threaded |

### 12.3 File Organization

```
libnmo/
├── include/
│   ├── nmo.h                    (umbrella header)
│   ├── nmo_types.h
│   ├── core/
│   │   ├── nmo_allocator.h
│   │   ├── nmo_arena.h
│   │   ├── nmo_error.h
│   │   ├── nmo_logger.h
│   │   └── nmo_guid.h
│   ├── io/
│   │   ├── nmo_io.h
│   │   ├── nmo_io_file.h
│   │   ├── nmo_io_memory.h
│   │   ├── nmo_io_compressed.h
│   │   ├── nmo_io_checksum.h
│   │   └── nmo_txn.h
│   ├── format/
│   │   ├── nmo_header.h
│   │   ├── nmo_chunk.h
│   │   ├── nmo_object.h
│   │   └── nmo_manager.h
│   ├── schema/
│   │   ├── nmo_schema.h
│   │   ├── nmo_schema_registry.h
│   │   ├── nmo_validator.h
│   │   └── nmo_migrator.h
│   ├── session/
│   │   ├── nmo_parser.h
│   │   ├── nmo_builder.h
│   │   └── nmo_id_remap.h
│   └── app/
│       ├── nmo_context.h
│       └── nmo_session.h
│
├── src/
│   ├── core/           (allocator.c, arena.c, error.c, logger.c, guid.c)
│   ├── io/             (io.c, io_file.c, io_memory.c, io_compressed.c, io_checksum.c, txn.c)
│   ├── format/         (header.c, chunk.c, object.c, manager.c)
│   ├── schema/         (schema.c, schema_registry.c, validator.c, migrator.c)
│   ├── builtin_schemas/ (nmo_builtin_schemas.c - pre-generated)
│   ├── session/        (parser.c, builder.c, id_remap.c)
│   └── app/            (context.c, session.c)
│
├── tools/              (nmo-inspect, nmo-validate, nmo-convert, nmo-diff)
├── tests/              (unit/, integration/, fuzz/)
└── CMakeLists.txt
```

---

## 13. Build System

### 13.1 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.15)
project(libnmo VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options
option(NMO_BUILD_TESTS "Build tests" ON)
option(NMO_BUILD_TOOLS "Build CLI tools" ON)
option(NMO_BUILD_EXAMPLES "Build examples" ON)
option(NMO_ENABLE_SIMD "Enable SIMD optimizations" ON)

# Dependencies
find_package(ZLIB REQUIRED)

# Source files
set(NMO_SOURCES
    # Core
    src/core/allocator.c
    src/core/arena.c
    src/core/error.c
    src/core/logger.c
    src/core/guid.c
    
    # IO
    src/io/io.c
    src/io/io_file.c
    src/io/io_memory.c
    src/io/io_compressed.c
    src/io/io_checksum.c
    src/io/txn.c
    
    # Format
    src/format/header.c
    src/format/chunk.c
    src/format/object.c
    src/format/manager.c
    
    # Schema (pre-generated)
    src/schema/schema.c
    src/schema/schema_registry.c
    src/schema/validator.c
    src/schema/migrator.c
    src/builtin_schemas/nmo_builtin_schemas.c
    
    # Session
    src/session/parser.c
    src/session/builder.c
    src/session/id_remap.c
    
    # Application
    src/app/context.c
    src/app/session.c
)

# Platform-specific sources
if(WIN32)
    list(APPEND NMO_SOURCES src/io/txn_windows.c)
else()
    list(APPEND NMO_SOURCES src/io/txn_posix.c)
endif()

# SIMD sources
if(NMO_ENABLE_SIMD)
    list(APPEND NMO_SOURCES 
        src/core/adler32_simd.c
        src/core/compression_simd.c)
endif()

# Library
add_library(nmo ${NMO_SOURCES})

target_include_directories(nmo PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/builtin_schemas>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(nmo PUBLIC ZLIB::ZLIB)

# Compiler flags
if(MSVC)
    target_compile_options(nmo PRIVATE /W4 /WX)
else()
    target_compile_options(nmo PRIVATE -Wall -Wextra -Werror)
endif()

# SIMD flags
if(NMO_ENABLE_SIMD)
    if(MSVC)
        target_compile_options(nmo PRIVATE /arch:AVX2)
    else()
        target_compile_options(nmo PRIVATE -mavx2 -msse4.2)
    endif()
    target_compile_definitions(nmo PRIVATE NMO_SIMD_ENABLED)
endif()

# Tests
if(NMO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Tools
if(NMO_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# Examples
if(NMO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Installation
install(TARGETS nmo
    EXPORT nmoTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ 
    DESTINATION include
    FILES_MATCHING PATTERN "*.h")

install(FILES src/builtin_schemas/nmo_builtin_schemas.h
    DESTINATION include)

install(EXPORT nmoTargets
    FILE nmoTargets.cmake
    NAMESPACE nmo::
    DESTINATION lib/cmake/nmo)
```

### 13.2 Building

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Install
cmake --install build --prefix /usr/local

# Run tests
cd build && ctest
```

---

## 14. Implementation Examples

### 14.1 Complete File Inspector

```c
#include "nmo.h"
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.nmo>\n", argv[0]);
        return 1;
    }
    
    // Create context
    nmo_context_desc ctx_desc = {
        .allocator = NULL,  // Use default
        .logger = nmo_logger_stderr(),
        .thread_pool_size = 4
    };
    
    nmo_context* ctx = nmo_context_create(&ctx_desc);
    if (!ctx) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }
    
    // Register built-in schemas
    nmo_schema_registry_add_builtin(ctx->schema_registry);
    
    // Create session
    nmo_session* session = nmo_session_create(ctx);
    if (!session) {
        fprintf(stderr, "Failed to create session\n");
        nmo_context_release(ctx);
        return 1;
    }
    
    // Load file
    nmo_result result = nmo_load_file(session, argv[1], NMO_LOAD_DEFAULT);
    if (result.code != NMO_OK) {
        fprintf(stderr, "Load failed: %s\n", result.error->message);
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return 1;
    }
    
    // Print file info
    nmo_file_info info = nmo_session_get_file_info(session);
    printf("File: %s\n", argv[1]);
    printf("Format Version: %u\n", info.file_version);
    printf("Object Count: %u\n", info.object_count);
    printf("Manager Count: %u\n", info.manager_count);
    printf("Compressed: %s\n", 
           info.write_mode & NMO_WRITE_MODE_COMPRESSED ? "yes" : "no");
    
    // List objects
    printf("\nObjects:\n");
    nmo_object_iterator* it = nmo_session_iterate_objects(session);
    while (nmo_iterator_has_next(it)) {
        nmo_object* obj = nmo_iterator_next(it);
        const nmo_schema_descriptor* schema = 
            nmo_schema_registry_find_by_id(ctx->schema_registry, obj->class_id);
        
        printf("  %s (%s)\n", 
               obj->name, 
               schema ? schema->class_name : "Unknown");
    }
    nmo_iterator_destroy(it);
    
    // Cleanup
    nmo_session_destroy(session);
    nmo_context_release(ctx);
    
    return 0;
}
```

### 14.2 File Converter

```c
#include "nmo.h"
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input.nmo> <output.nmo>\n", argv[0]);
        return 1;
    }
    
    const char* input_path = argv[1];
    const char* output_path = argv[2];
    
    // Create context
    nmo_context* ctx = nmo_context_create(&(nmo_context_desc){
        .allocator = NULL,
        .logger = nmo_logger_stderr(),
        .thread_pool_size = 4
    });
    
    nmo_schema_registry_add_builtin(ctx->schema_registry);
    
    // Create session
    nmo_session* session = nmo_session_create(ctx);
    
    // Load
    printf("Loading: %s\n", input_path);
    nmo_result result = nmo_load_file(session, input_path, NMO_LOAD_DEFAULT);
    if (result.code != NMO_OK) {
        fprintf(stderr, "Load failed: %s\n", result.error->message);
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return 1;
    }
    
    // Create save builder
    nmo_save_builder* builder = nmo_save_builder_create(session);
    nmo_save_builder_add_all(builder);
    
    // Configure save options
    nmo_save_options opts = {
        .file_version = 8,
        .write_mode = NMO_WRITE_MODE_COMPRESSED,
        .compress_level = 6
    };
    nmo_save_builder_set_options(builder, &opts);
    
    // Save
    printf("Saving: %s\n", output_path);
    result = nmo_save_builder_execute(builder, output_path);
    if (result.code != NMO_OK) {
        fprintf(stderr, "Save failed: %s\n", result.error->message);
    } else {
        printf("Success\n");
    }
    
    // Cleanup
    nmo_save_builder_destroy(builder);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
    
    return result.code == NMO_OK ? 0 : 1;
}
```

### 14.3 Custom Manager

```c
// Custom game manager
typedef struct game_manager_state {
    int level;
    float difficulty;
} game_manager_state;

int game_manager_load_data(void* session, const nmo_chunk* chunk, void* user_data) {
    game_manager_state* state = (game_manager_state*)user_data;
    nmo_chunk_parser* parser = nmo_chunk_parser_create((nmo_chunk*)chunk);
    
    uint32_t level;
    nmo_chunk_parser_read_dword(parser, &level);
    state->level = (int)level;
    
    nmo_chunk_parser_read_float(parser, &state->difficulty);
    
    nmo_chunk_parser_destroy(parser);
    return NMO_OK;
}

nmo_chunk* game_manager_save_data(void* session, void* user_data) {
    game_manager_state* state = (game_manager_state*)user_data;
    nmo_session* s = (nmo_session*)session;
    
    nmo_chunk_writer* writer = nmo_chunk_writer_create(s->arena);
    nmo_chunk_writer_start(writer, 0, CHUNK_VERSION4);
    
    nmo_chunk_writer_write_dword(writer, (uint32_t)state->level);
    nmo_chunk_writer_write_float(writer, state->difficulty);
    
    return nmo_chunk_writer_finalize(writer);
}

void register_game_manager(nmo_context* ctx) {
    game_manager_state* state = calloc(1, sizeof(game_manager_state));
    
    nmo_manager mgr = {
        .guid = {0x12345678, 0x90ABCDEF},
        .name = "GameManager",
        .category = NMO_PLUGIN_MANAGER_DLL,
        .load_data = game_manager_load_data,
        .save_data = game_manager_save_data,
        .user_data = state
    };
    
    nmo_manager_register(ctx->manager_registry, &mgr);
}
```

---

## 15. Testing and Tools

### 15.1 Testing Infrastructure

**Unit Tests**
- Core layer: allocator, arena, error, logger
- IO layer: file, memory, compressed, checksum
- Format layer: header, chunk parsing/writing
- Schema layer: registry, validator, migrator

**Integration Tests**
- Round-trip tests (load → save → load)
- Version migration (v2 → v8)
- Multi-file transactions
- Manager hooks

**Fuzz Tests**
- Header parsing
- Chunk parsing
- Decompression
- ID remapping

### 15.2 CLI Tools

**nmo-inspect**
```bash
nmo-inspect file.nmo                 # Text output
nmo-inspect --format=json file.nmo   # JSON output
nmo-inspect --verbose file.nmo       # Verbose output
```

**nmo-validate**
```bash
nmo-validate file.nmo                # Validate file
nmo-validate --strict file.nmo       # Strict mode
```

**nmo-convert**
```bash
nmo-convert input.nmo output.nmo --version=8
nmo-convert input.nmo output.nmo --compress
```

**nmo-diff**
```bash
nmo-diff file1.nmo file2.nmo         # Compare files
```

### 15.3 Example Test

```c
#include "nmo.h"
#include "test_framework.h"

TEST(chunk_parser, read_dword) {
    nmo_arena* arena = nmo_arena_create(&nmo_allocator_default(), 4096);
    
    // Create test chunk
    uint32_t data[] = {42, 123, 456};
    nmo_chunk* chunk = nmo_arena_alloc(arena, sizeof(nmo_chunk), alignof(nmo_chunk));
    chunk->data = data;
    chunk->data_size = 3;
    chunk->arena = arena;
    
    // Parse
    nmo_chunk_parser* parser = nmo_chunk_parser_create(chunk);
    
    uint32_t value;
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_dword(parser, &value));
    ASSERT_EQ(42, value);
    
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_dword(parser, &value));
    ASSERT_EQ(123, value);
    
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_dword(parser, &value));
    ASSERT_EQ(456, value);
    
    // EOF
    ASSERT_EQ(NMO_ERR_BUFFER_OVERRUN, nmo_chunk_parser_read_dword(parser, &value));
    
    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}
```

---

## Summary

libnmo provides a complete, production-ready library for Virtools file manipulation:

**Features:**
- Load and save .nmo/.cmo/.vmo files (v2-v9)
- Schema-driven serialization (pre-generated C code)
- ID remapping and object management
- Manager system with hooks
- Composable IO with compression and checksums
- Transactional saves with rollback
- Validation and migration
- Comprehensive error handling
- Thread-safe context and registries
- Efficient arena allocation

**Architecture:**
- Layered design with clear dependencies
- No circular dependencies
- Explicit ownership semantics
- Composable IO stack
- Extensible through managers and validators

**Quality:**
- Bounds-checked parsing
- Error chains with causal relationships
- Comprehensive testing (unit, integration, fuzz)
- CLI tools for inspection and validation
- Production-ready build system

For binary format details, see `VIRTOOLS_FILE_FORMAT_SPEC.md`.  
For runtime behaviors, see `VIRTOOLS_IMPLEMENTATION_GUIDE.md`.
