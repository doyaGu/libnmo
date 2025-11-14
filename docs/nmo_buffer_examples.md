/**
 * @file nmo_buffer_example.md
 * @brief Usage examples for nmo_buffer API
 */

# nmo_buffer Usage Examples

## Basic Buffer Operations

### 1. Initialize a Buffer

```c
nmo_arena_t *arena = nmo_arena_create(1024);
nmo_buffer_t buffer;

// Initialize with element size and initial capacity
nmo_buffer_init(&buffer, sizeof(uint32_t), 16, arena);
```

### 2. Append Elements

```c
// Append single element
uint32_t value = 42;
nmo_buffer_append(&buffer, &value);

// Append multiple elements
uint32_t values[] = {1, 2, 3, 4, 5};
nmo_buffer_append_array(&buffer, values, 5);
```

### 2.5 In-Place Extend

```c
// Reserve space for 3 new elements and write directly
uint32_t *slots = NULL;
nmo_result_t extend_result = NMO_BUFFER_EXTEND(&buffer, 3, slots);
if (extend_result.code == NMO_OK) {
    slots[0] = 100;
    slots[1] = 200;
    slots[2] = 300;
}
```

### 3. Access Elements

```c
// Get element at index
uint32_t *ptr = (uint32_t *)nmo_buffer_get(&buffer, 2);
if (ptr) {
    printf("Element 2: %u\n", *ptr);
}

// Direct access to data
uint32_t *data = (uint32_t *)buffer.data;
for (size_t i = 0; i < buffer.count; i++) {
    printf("%u ", data[i]);
}
```

### 4. Reserve Capacity

```c
// Reserve space for 100 elements (avoids reallocation)
nmo_buffer_reserve(&buffer, 100);
```

### 5. Clone a Buffer

```c
nmo_buffer_t clone;
nmo_buffer_clone(&buffer, &clone, arena);
// clone now contains a deep copy of buffer
```

## Using Typed Macros

```c
// Type-safe initialization
nmo_buffer_t int_buffer = NMO_BUFFER_INIT(int, 10, arena);

// Type-safe access
int *value = NMO_BUFFER_GET(int, &int_buffer, 0);

// Type-safe append
int num = 123;
NMO_BUFFER_APPEND(int, &int_buffer, &num);

// Get typed data pointer
int *data_ptr = NMO_BUFFER_DATA(int, &int_buffer);
```

## Chunk-Specific Examples

### Accessing Chunk Data

```c
nmo_chunk_t *chunk = nmo_chunk_create(arena);

// Old style (using macros for compatibility):
uint32_t *data = NMO_CHUNK_DATA(chunk);
size_t size = NMO_CHUNK_DATA_SIZE(chunk);

// New style (direct buffer access):
uint32_t *data2 = (uint32_t *)chunk->data_buffer.data;
size_t size2 = chunk->data_buffer.count;
```

### Adding Data to Chunk

```c
// Append data using buffer API
uint32_t value = 0x12345678;
nmo_buffer_append(&chunk->data_buffer, &value);

// Reserve space before bulk operations
nmo_buffer_reserve(&chunk->data_buffer, 100);

// Bulk append
uint32_t values[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
nmo_buffer_append_array(&chunk->data_buffer, values, 10);

// Insert at specific index
uint32_t new_value = 0xCAFEBABE;
nmo_buffer_insert(&chunk->data_buffer, 2, &new_value);

// Remove and read back a value
uint32_t removed = 0;
nmo_buffer_remove(&chunk->data_buffer, 5, &removed);
printf("Removed value: 0x%X\n", removed);

// Pop last element
uint32_t last = 0;
nmo_buffer_pop(&chunk->data_buffer, &last);
```

### Working with IDs

```c
// Add object ID
uint32_t object_id = 42;
nmo_buffer_append(&chunk->id_buffer, &object_id);

// Access IDs
uint32_t *ids = NMO_CHUNK_IDS(chunk);
size_t id_count = NMO_CHUNK_ID_COUNT(chunk);
for (size_t i = 0; i < id_count; i++) {
    printf("ID[%zu] = %u\n", i, ids[i]);
}
```

### Working with Sub-Chunks

```c
// Create sub-chunk
nmo_chunk_t *subchunk = nmo_chunk_create(arena);
// ... configure subchunk ...

// Add sub-chunk (stores pointer)
nmo_buffer_append(&chunk->chunk_buffer, &subchunk);

// Access sub-chunks
nmo_chunk_t **chunks = NMO_CHUNK_CHUNKS(chunk);
size_t chunk_count = NMO_CHUNK_CHUNK_COUNT(chunk);
for (size_t i = 0; i < chunk_count; i++) {
    printf("Sub-chunk[%zu] = %p\n", i, (void *)chunks[i]);
}
```

### Working with Manager IDs

```c
// Add manager ID
uint32_t manager_id = 5;
nmo_buffer_append(&chunk->manager_buffer, &manager_id);

// Access managers
uint32_t *managers = NMO_CHUNK_MANAGERS(chunk);
size_t manager_count = NMO_CHUNK_MANAGER_COUNT(chunk);
```

### Front/Back Helpers

```c
uint32_t *first_value = NMO_BUFFER_FRONT(uint32_t, &chunk->data_buffer);
uint32_t *last_value = NMO_BUFFER_BACK(uint32_t, &chunk->data_buffer);
if (last_value != NULL) {
    printf("First: %u, Last: %u\n", *first_value, *last_value);
}
```

## Custom Buffer Types

```c
// For custom structures
typedef struct {
    int x, y, z;
} Point3D;

nmo_buffer_t points;
nmo_buffer_init(&points, sizeof(Point3D), 10, arena);

Point3D p = {1, 2, 3};
nmo_buffer_append(&points, &p);

Point3D *first = (Point3D *)nmo_buffer_get(&points, 0);
printf("Point: (%d, %d, %d)\n", first->x, first->y, first->z);
```

## Growth Strategy

The buffer uses exponential growth:
- Starts with 4 elements (if initial_capacity is 0)
- Doubles capacity when full
- Example: 4 → 8 → 16 → 32 → 64 → 128...

```c
nmo_buffer_t buffer;
nmo_buffer_init(&buffer, sizeof(int), 0, arena);

// First append triggers allocation of 4 elements
int val = 1;
nmo_buffer_append(&buffer, &val);
printf("Capacity: %zu\n", buffer.capacity); // 4

// Continue appending...
for (int i = 2; i <= 5; i++) {
    nmo_buffer_append(&buffer, &i);
}
printf("Capacity: %zu\n", buffer.capacity); // 8 (doubled)
```

## Error Handling

```c
nmo_result_t result = nmo_buffer_append(&buffer, &value);
if (result.code != NMO_OK) {
    fprintf(stderr, "Failed to append: %s\n", result.error->message);
    return -1;
}
```

## Memory Management

- All allocations come from the provided arena
- No explicit cleanup needed (arena handles it)
- Buffers don't own the arena
- Multiple buffers can share the same arena

```c
nmo_arena_t *arena = nmo_arena_create(4096);

nmo_buffer_t buf1, buf2, buf3;
nmo_buffer_init(&buf1, sizeof(int), 10, arena);
nmo_buffer_init(&buf2, sizeof(float), 20, arena);
nmo_buffer_init(&buf3, sizeof(double), 5, arena);

// ... use buffers ...

// Cleanup (destroys all buffers at once)
nmo_arena_destroy(arena);
```

## Performance Tips

1. **Pre-allocate**: Use `nmo_buffer_reserve()` if you know the size
2. **Batch operations**: Use `nmo_buffer_append_array()` for multiple elements
3. **Avoid reallocation**: Reserve enough capacity upfront
4. **Direct access**: For read-only iteration, access `buffer.data` directly

```c
// Good: Reserve once
nmo_buffer_reserve(&buffer, 1000);
for (int i = 0; i < 1000; i++) {
    nmo_buffer_append(&buffer, &i);
}

// Even better: Batch append
int values[1000];
for (int i = 0; i < 1000; i++) values[i] = i;
nmo_buffer_append_array(&buffer, values, 1000);

// Or extend and write directly
int *slots = NULL;
if (NMO_BUFFER_EXTEND(&buffer, 1000, slots).code == NMO_OK) {
    for (int i = 0; i < 1000; i++) {
        slots[i] = i;
    }
}
```
