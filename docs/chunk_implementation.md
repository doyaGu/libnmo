# CKStateChunk Implementation

## Overview

This document describes the implementation of the CKStateChunk core structure and binary format for the libnmo project. CKStateChunk is the fundamental serialization container in Virtools, storing DWORD-aligned data with optional object IDs, sub-chunks, and manager references.

## Files Modified

- `/home/user/libnmo/include/format/nmo_chunk.h` - Header with structure and API definitions
- `/home/user/libnmo/src/format/chunk.c` - Implementation with serialization/deserialization

## Structure Definition

### Chunk Options

```c
typedef enum {
    NMO_CHUNK_OPTION_IDS      = 0x01,  /* Contains object ID references */
    NMO_CHUNK_OPTION_MAN      = 0x02,  /* Contains manager int refs */
    NMO_CHUNK_OPTION_CHN      = 0x04,  /* Contains sub-chunks */
    NMO_CHUNK_OPTION_FILE     = 0x08,  /* Written with file context */
    NMO_CHUNK_OPTION_ALLOWDYN = 0x10,  /* Allow dynamic objects */
    NMO_CHUNK_OPTION_LISTBIG  = 0x20,  /* Lists in big endian (unused) */
    NMO_CHUNK_DONTDELETE_PTR  = 0x40,  /* Data not owned by chunk */
} nmo_chunk_options;
```

### Main Chunk Structure

```c
typedef struct nmo_chunk {
    /* Identity */
    nmo_class_id       class_id;         /* Object class ID */
    uint32_t          data_version;     /* Custom version per class */
    uint32_t          chunk_version;    /* Chunk format version (7) */
    uint8_t           chunk_class_id;   /* Legacy class ID (8-bit) */
    uint32_t          chunk_options;    /* Option flags */

    /* Data buffer (DWORD-aligned) */
    uint32_t*         data;             /* Payload buffer */
    size_t            data_size;        /* Size in DWORDs (not bytes!) */
    size_t            data_capacity;    /* Capacity in DWORDs */

    /* Optional tracking lists */
    uint32_t*         ids;              /* Object ID list */
    size_t            id_count;
    size_t            id_capacity;

    struct nmo_chunk** chunks;          /* Sub-chunk list */
    size_t            chunk_count;
    size_t            chunk_capacity;

    uint32_t*         managers;         /* Manager int list */
    size_t            manager_count;
    size_t            manager_capacity;

    /* Compression info (for statistics) */
    size_t            uncompressed_size;
    size_t            compressed_size;
    int               is_compressed;

    /* Memory management */
    nmo_arena*        arena;            /* Arena for allocations */
    int               owns_data;        /* Whether to free data */
} nmo_chunk;
```

## Binary Format

The Virtools chunk binary format is:

```
[4 bytes] Version Info - packed as single DWORD:
          versionInfo = (dataVersion | (chunkClassID << 8)) |
                       ((chunkVersion | (chunkOptions << 8)) << 16)
[4 bytes] Chunk Size - in DWORDs (not bytes!)
[Size*4 bytes] Data buffer
[Optional] IDs List: [4 bytes count][count * 4 bytes IDs]
[Optional] Chunks List: [4 bytes count][recursive chunks]
[Optional] Managers List: [4 bytes count][count * 4 bytes ints]
```

### Version Info Packing

The version info is a packed 32-bit value:
- Bits 0-7: `data_version`
- Bits 8-15: `chunk_class_id`
- Bits 16-23: `chunk_version`
- Bits 24-31: `chunk_options`

### Version Info Unpacking

```c
chunk->data_version = version_info & 0xFF;
chunk->chunk_class_id = (version_info >> 8) & 0xFF;
chunk->chunk_version = (version_info >> 16) & 0xFF;
chunk->chunk_options = (version_info >> 24) & 0xFF;
```

## API Functions

### Core Functions

#### nmo_chunk_create

```c
nmo_chunk* nmo_chunk_create(nmo_arena* arena);
```

Creates an empty chunk with default values:
- `chunk_version` = `NMO_CHUNK_VERSION_4` (7)
- `owns_data` = 1
- All other fields initialized to 0/NULL

**Returns:** New chunk or NULL on allocation failure

#### nmo_chunk_serialize

```c
nmo_result nmo_chunk_serialize(const nmo_chunk* chunk,
                                void** out_data,
                                size_t* out_size,
                                nmo_arena* arena);
```

Serializes a chunk to binary format according to the Virtools specification.

**Parameters:**
- `chunk` - Chunk to serialize (required)
- `out_data` - Output buffer pointer (required)
- `out_size` - Output size in bytes (required)
- `arena` - Arena for output buffer allocation (required)

**Returns:** `NMO_OK` on success, error code on failure

**Errors:**
- `NMO_ERR_INVALID_ARGUMENT` - NULL pointer arguments
- `NMO_ERR_NOMEM` - Allocation failure
- `NMO_ERR_BUFFER_OVERRUN` - Internal serialization error

#### nmo_chunk_deserialize

```c
nmo_result nmo_chunk_deserialize(const void* data,
                                  size_t size,
                                  nmo_arena* arena,
                                  nmo_chunk** out_chunk);
```

Deserializes a chunk from binary data.

**Parameters:**
- `data` - Input binary data (required)
- `size` - Input data size in bytes (required)
- `arena` - Arena for allocations (required)
- `out_chunk` - Output chunk pointer (required)

**Returns:** `NMO_OK` on success, error code on failure

**Errors:**
- `NMO_ERR_INVALID_ARGUMENT` - NULL pointer arguments
- `NMO_ERR_BUFFER_OVERRUN` - Invalid or truncated data
- `NMO_ERR_NOMEM` - Allocation failure

#### nmo_chunk_destroy

```c
void nmo_chunk_destroy(nmo_chunk* chunk);
```

Destroys a chunk. Since chunks use arena allocation, this is mostly a no-op. The arena itself handles cleanup.

### Helper Functions

The implementation also includes:
- `nmo_chunk_get_header()` - Get chunk metadata
- `nmo_chunk_get_id()` - Get chunk class ID
- `nmo_chunk_get_size()` - Get chunk data size in bytes
- `nmo_chunk_get_data()` - Get chunk data pointer
- `nmo_chunk_add_sub_chunk()` - Add a sub-chunk
- `nmo_chunk_get_sub_chunk_count()` - Get number of sub-chunks
- `nmo_chunk_get_sub_chunk()` - Get sub-chunk by index

## Usage Examples

### Basic Chunk Creation and Serialization

```c
/* Create arena */
nmo_arena* arena = nmo_arena_create(NULL, 0);

/* Create chunk */
nmo_chunk* chunk = nmo_chunk_create(arena);
chunk->data_version = 1;
chunk->chunk_class_id = 42;

/* Allocate and set data (2 DWORDs) */
chunk->data_size = 2;
chunk->data = nmo_arena_alloc(arena, 2 * sizeof(uint32_t), 4);
chunk->data[0] = 0xDEADBEEF;
chunk->data[1] = 0xCAFEBABE;

/* Serialize */
void* serialized_data = NULL;
size_t serialized_size = 0;
nmo_result result = nmo_chunk_serialize(chunk, &serialized_data,
                                        &serialized_size, arena);
if (result.code == NMO_OK) {
    /* Use serialized_data... */
}

/* Cleanup */
nmo_arena_destroy(arena);
```

### Chunk with Object IDs

```c
nmo_arena* arena = nmo_arena_create(NULL, 0);
nmo_chunk* chunk = nmo_chunk_create(arena);

/* Set options to include IDs */
chunk->chunk_options = NMO_CHUNK_OPTION_IDS;

/* Add object IDs */
chunk->id_count = 3;
chunk->ids = nmo_arena_alloc(arena, 3 * sizeof(uint32_t), 4);
chunk->ids[0] = 100;
chunk->ids[1] = 200;
chunk->ids[2] = 300;

/* Serialize... */
```

### Chunk with Sub-Chunks

```c
nmo_arena* arena = nmo_arena_create(NULL, 0);
nmo_chunk* parent = nmo_chunk_create(arena);

/* Set options to include sub-chunks */
parent->chunk_options = NMO_CHUNK_OPTION_CHN;

/* Create sub-chunks */
parent->chunk_count = 2;
parent->chunks = nmo_arena_alloc(arena, 2 * sizeof(nmo_chunk*), sizeof(void*));

parent->chunks[0] = nmo_chunk_create(arena);
parent->chunks[1] = nmo_chunk_create(arena);

/* Configure sub-chunks... */

/* Serialize (handles recursive serialization) */
void* data = NULL;
size_t size = 0;
nmo_chunk_serialize(parent, &data, &size, arena);
```

### Deserialization

```c
nmo_arena* arena = nmo_arena_create(NULL, 0);

/* Deserialize from binary data */
nmo_chunk* chunk = NULL;
nmo_result result = nmo_chunk_deserialize(binary_data, binary_size,
                                          arena, &chunk);
if (result.code == NMO_OK) {
    /* Access chunk data */
    printf("Data version: %u\n", chunk->data_version);
    printf("Class ID: %u\n", chunk->chunk_class_id);
    printf("Data size: %zu DWORDs\n", chunk->data_size);

    /* Access optional lists */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_IDS) {
        for (size_t i = 0; i < chunk->id_count; i++) {
            printf("ID[%zu]: %u\n", i, chunk->ids[i]);
        }
    }
}

nmo_arena_destroy(arena);
```

## Implementation Details

### Memory Management

All allocations are done through the arena allocator:
- Fast O(1) allocation
- Bulk deallocation when arena is destroyed
- No individual free operations needed
- Aligned allocations for DWORD alignment

### DWORD Alignment

All data buffers are DWORD (4-byte) aligned:
- `data_size` is in DWORDs, not bytes
- To get byte size: `data_size * 4`
- All allocations use 4-byte alignment

### Recursive Serialization

Sub-chunks are serialized recursively:
- Parent chunk writes sub-chunk count
- Each sub-chunk is serialized in order
- Deserialization mirrors this process

### Error Handling

All functions return `nmo_result` with proper error codes:
- `NMO_OK` - Success
- `NMO_ERR_NOMEM` - Memory allocation failure
- `NMO_ERR_BUFFER_OVERRUN` - Buffer overflow/underflow
- `NMO_ERR_INVALID_ARGUMENT` - NULL pointer or invalid arguments

## Testing

Comprehensive tests in `/home/user/libnmo/tests/unit/test_chunk_core.c`:

1. **test_chunk_create** - Basic chunk creation
2. **test_chunk_serialize_empty** - Empty chunk serialization
3. **test_chunk_roundtrip** - Full serialize/deserialize cycle
4. **test_chunk_with_subchunks** - Recursive chunk handling
5. **test_chunk_full_options** - All optional features

All tests pass successfully with strict compiler warnings enabled (`-Wall -Wextra -Werror`).

## Compliance

The implementation:
- Follows the Virtools CKStateChunk binary format specification
- Uses DWORD-aligned data buffers
- Correctly packs/unpacks version information
- Handles all optional features (IDs, sub-chunks, managers)
- Compiles without warnings with `-Wall -Wextra -Werror -pedantic`
- Uses arena allocation for memory management
- Provides comprehensive error handling
