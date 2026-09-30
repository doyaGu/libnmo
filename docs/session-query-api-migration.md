# Object Query API Migration

Object lookup goes through `nmo_object_query_t` in `include/object/nmo_object_query.h`.
The narrow session lookup helpers no longer exist:

- `nmo_session_find_by_name()`
- `nmo_session_find_by_guid()`
- `nmo_session_get_objects_by_class()`
- `nmo_session_count_objects_by_class()`

Neither do the `nmo_session_query_*()` functions, `nmo_session_find_object_by_name()` and
`nmo_session_count_objects()` that an earlier revision of this document pointed to. Queries now
run against a document, or against an explicit `nmo_object_query_context_t`.

| Entry point                    | Takes                         | Use for                                  |
|--------------------------------|-------------------------------|------------------------------------------|
| `nmo_object_query_find_first()` | `nmo_document_t *`            | First match; `NMO_ERR_NOT_FOUND` if none |
| `nmo_object_query_count()`      | `nmo_document_t *`            | Number of matches                        |
| `nmo_object_query_resolve_one()` | `nmo_document_t *`, `nmo_object_selector_t` | Resolve one object by id or name within class constraints |
| `nmo_object_query_collect()`    | `nmo_object_query_context_t *`, arena | All matches, returned in caller-owned arena memory |
| `nmo_object_query_iterate()`    | `nmo_object_query_context_t *`, visitor | Visit matches without collecting        |

The query struct fields are `object_id`, `class_id`, `include_derived_classes`,
`has_type_guid` / `type_guid`, `name` / `name_mode` / `name_case_insensitive`, and an optional
`predicate` callback. `name_mode` is one of `NMO_OBJECT_QUERY_NAME_EXACT`, `_SUBSTRING`,
`_WILDCARD` or `_REGEX`; `NMO_OBJECT_QUERY_NAME_NONE` (0) disables name matching.

## Exact Name

```c
nmo_object_query_t query = {
    .name = "Camera",
    .name_mode = NMO_OBJECT_QUERY_NAME_EXACT
};

nmo_object_t *object = NULL;
nmo_status_t status =
    nmo_object_query_find_first(document, &query, &object, NULL);
```

The last argument is an optional out-parameter for the match's position and may be `NULL`.
Combine `name`, `class_id` and `has_type_guid` in one query when you need a combined
predicate.

## Type GUID

Query by exact object `type_guid`:

```c
nmo_object_query_t query = {
    .has_type_guid = true,
    .type_guid = guid
};

nmo_object_t *object = NULL;
nmo_status_t status =
    nmo_object_query_find_first(document, &query, &object, NULL);
```

A null GUID filter intentionally matches no objects.

## Class Collect

`collect` works on a query context rather than a document. The context carries the repository,
an optional query index, and the type registry:

```c
nmo_object_query_context_t qctx = {
    .repository = repository,
    .index = NULL,                 // optional retained index
    .registry = registry
};
nmo_object_query_t query = {
    .class_id = cid,
    .include_derived_classes = false
};

nmo_object_t **objects = NULL;
size_t count = 0;
nmo_status_t status =
    nmo_object_query_collect(&qctx, &query, arena, &objects, &count, NULL);
```

The returned array lives in `arena` and is valid until the arena is reset or destroyed. Set
`include_derived_classes = true` when the old call site expected derived classes to be included.

## Class Count

```c
nmo_object_query_t query = {
    .class_id = cid,
    .include_derived_classes = false
};

size_t count = 0;
nmo_status_t status = nmo_object_query_count(document, &query, &count);
```

For the total object count, pass a `NULL` query: `nmo_object_query_count(document, NULL, &count)`
returns the repository's object count without scanning through the query engine.

## Query Index

A retained `nmo_object_query_index_t` speeds up repeated queries:

```c
nmo_object_query_index_t *index =
    nmo_object_query_index_create(repository, registry, NULL);
nmo_object_query_index_attach_repository_observer(index);   // keep it fresh on mutations
```

Put it in `nmo_object_query_context_t.index`. `nmo_object_query_index_invalidate()` and
`_trim()` take `NMO_OBJECT_QUERY_INDEX_*` flags to drop parts of the index.

## Updating Type GUIDs

For objects that are not yet in a repository, `nmo_object_set_type_guid()` is
still valid:

```c
nmo_object_set_type_guid(object, guid);
nmo_object_repository_add(repository, &object);
```

For repository-owned objects, update through the repository so retained query
indexes are invalidated automatically:

```c
nmo_object_repository_set_type_guid(repository, object_id, guid);
```
