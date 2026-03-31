# Ownership Rules

## Naming Conventions

libnmo uses naming conventions to communicate ownership semantics.
All pointer-returning public API functions follow one of these patterns:

| Pattern | Ownership | Caller Responsibility |
|---------|-----------|----------------------|
| `*_create()` | **Owned** | Must call matching `*_destroy()` or `*_release()` |
| `*_clone()` | **Owned** (new copy) | Must call matching `*_destroy()` |
| `*_take()` | **Transfer** | Caller receives ownership; source pointer nullified |
| `*_detach()` | **Transfer** | Caller receives ownership; container no longer owns |
| `*_get_*()` | **Borrowed** | Must NOT destroy; valid until parent is destroyed |
| `*_find_*()` | **Borrowed** | Must NOT destroy; valid until parent is modified |
| `*_alloc()` | **Arena/pool** | Freed when arena/pool is destroyed; no explicit free |

## Ownership Tags (Debug Mode)

The `nmo_ownership_tag_t` enum (`include/core/nmo_ownership.h`) tracks
allocation origin at runtime in debug builds:

```c
NMO_OWNERSHIP_UNKNOWN   // Not yet classified
NMO_OWNERSHIP_ARENA     // Arena-allocated (freed with arena)
NMO_OWNERSHIP_HEAP      // Heap-allocated (explicit free required)
NMO_OWNERSHIP_EXTERNAL  // Externally managed (caller owns)
```

Debug assertions via `NMO_OWNERSHIP_EXPECT()` and
`NMO_OWNERSHIP_ASSERT_VALID()` catch misuse at runtime.

## Reference Counting

`nmo_context_t` uses atomic reference counting:

```c
nmo_context_t *ctx = nmo_context_create(&desc);  // refcount = 1
nmo_context_retain(ctx);                          // refcount = 2
nmo_context_release(ctx);                         // refcount = 1
nmo_context_release(ctx);                         // refcount = 0, freed
```

`retain`/`release` are thread-safe. All other context operations require
caller synchronization.

## Arena Lifetime

All `nmo_arena_alloc()` returns are valid until the arena is destroyed
or rewound past the allocation point. Rules:

- Session arena: lives for the session lifetime
- Chunk arenas: live for the chunk's lifetime
- `nmo_arena_mark()` / `nmo_arena_rewind()`: LIFO scoped deallocation

Do NOT free arena-allocated pointers individually.

## Session Ownership

```
nmo_context_t (refcounted)
  |
  +-- nmo_session_t (owned, single-threaded)
        |
        +-- nmo_arena_t (owned, session lifetime)
        +-- nmo_object_repository_t (owned)
        |     +-- nmo_object_t[] (arena-allocated, borrowed via get/find)
        +-- nmo_file_state_t (embedded struct)
        |     +-- .manager_data (borrowed, arena-allocated during load)
        |     +-- .plugin_deps (borrowed, arena-allocated during load)
        +-- nmo_shadow_storage_t (owned)
        +-- nmo_object_index_t (lazy, owned)
```

## Object Repository

```c
// Add: repository takes ownership, nullifies caller's pointer
nmo_object_repository_add(repo, &obj);  // obj becomes NULL

// Find: returns borrowed pointer (valid until repo modified)
nmo_object_t *obj = nmo_object_repository_find_by_id(repo, id);

// Take: caller receives ownership back
nmo_object_t *obj = nmo_object_repository_take(repo, id);
```

## Extension Host

When registering type or manager contributions, the extension host
deep-copies all strings and descriptors. Plugin arena can be freed
after registration without affecting the type registry.

## File State (Round-Trip Metadata)

`nmo_session_get_file_state()` returns a borrowed view of:
- `info`: file format metadata (versions, counts, write mode)
- `manager_data`: serialized manager chunks (arena-allocated)
- `plugin_deps`: plugin dependency table (arena-allocated)

All pointers within are arena-allocated during load and valid for
the session lifetime. Setters populate fields incrementally during
the load pipeline.
