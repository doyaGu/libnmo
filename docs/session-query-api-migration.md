# Session Query API Migration

Session-level object lookup now routes through `nmo_object_query_t` and the
`nmo_session_query_*()` APIs. The older narrow lookup APIs were removed from the
public session header:

- `nmo_session_find_by_name()`
- `nmo_session_find_by_guid()`
- `nmo_session_get_objects_by_class()`
- `nmo_session_count_objects_by_class()`

Use `nmo_session_find_object_by_name()` only for the existing exact-name
convenience path. All other filters should be expressed as an object query.

## Exact Name

```c
nmo_object_t *object = NULL;
nmo_status_t status =
    nmo_session_find_object_by_name(session, "Camera", &object);
```

For combined predicates, use an object query:

```c
nmo_object_query_t query = {
    .has_name = true,
    .name = "Camera",
    .name_match = NMO_OBJECT_QUERY_NAME_EXACT
};

nmo_object_t *object = NULL;
nmo_status_t status =
    nmo_session_query_first(session, &query, &object, NULL);
```

## Type GUID

There is no session GUID convenience helper. Query by exact object `type_guid`
through `nmo_object_query_t`:

```c
nmo_object_query_t query = {
    .has_type_guid = true,
    .type_guid = guid
};

nmo_object_t *object = NULL;
nmo_status_t status =
    nmo_session_query_first(session, &query, &object, NULL);
```

A null GUID filter intentionally matches no objects.

## Class Collect

```c
nmo_object_query_t query = {
    .class_id = cid,
    .include_derived_classes = false
};

nmo_object_t **objects = NULL;
size_t count = 0;
nmo_status_t status =
    nmo_session_query_collect(session, &query, arena, &objects, &count);
```

Set `include_derived_classes = true` when the old call site expected derived
classes to be included.

## Class Count

Use the query result's matched count:

```c
nmo_object_query_t query = {
    .class_id = cid,
    .include_derived_classes = false
};
nmo_object_query_result_t result = {0};

nmo_status_t status =
    nmo_session_query_objects(session, &query, NULL, NULL, &result);
size_t count = result.matched;
```

For the total object count, use `nmo_session_count_objects()`. It is backed by
the repository object count and does not scan through the query engine.

## Updating Type GUIDs

For objects that are not yet in a repository, `nmo_object_set_type_guid()` is
still valid:

```c
nmo_object_set_type_guid(object, guid);
nmo_object_repository_add(repository, &object);
```

For repository-owned objects, update through the repository so retained indexes
and session query indexes are invalidated automatically:

```c
nmo_object_repository_set_type_guid(repository, object_id, guid);
```
