# Interface Chunk Structured Write Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a real structured InterfaceChunk writer so libnmo can serialize `nmo_interface_data_t` back to CKStateChunk data without copying the original InterfaceChunk as a shortcut.

**Architecture:** `nmo_interface_chunk_parse()` remains the only reader and `nmo_interface_chunk_write()` becomes the symmetric writer. Raw InterfaceChunk bytes may be used only as an oracle in tests and diagnostics, not as the save implementation for successfully parsed interface data. CKBehavior file save should prefer the structured writer when `interface_data` exists; parse/write failures must surface as errors instead of being hidden by raw-chunk fallback.

**Tech Stack:** C17, existing `nmo_chunk_*` parser/writer primitives, `nmo_arena_t`, `test_interface_chunk.exe`, `test_round_trip`, CTest on `build_debug`.

---

## Non-Negotiables

- Do not implement save by calling `nmo_chunk_write_sub_chunk(out_chunk, state->interface_chunk)` when `state->interface_data` is available.
- Use the raw subchunk only to build failing tests, byte diff reports, and oracle comparisons.
- Preserve Dev 2.5 sectioned semantics: split `highlight` and `linkType`; do not write inline-only color in sectioned headers; do not invent control points.
- Preserve inline legacy semantics: encode `highlight` in the raw link type flag and write local/shared params plus graph IO in the inline order.
- If a parsed field is not represented in `nmo_interface_data_t`, add a structured field before writing it. Do not silently omit it.
- `nmo_behavior_serialize()` must fail when it is asked to save a behavior with InterfaceChunk data that cannot be structurally written.

---

## File Map

- `include/format/nmo_interface_chunk.h`
  - Add the public writer API and any missing structured metadata needed by the writer.

- `src/format/interface_chunk.c`
  - Implement the writer next to the parser, using matching helpers for header, body, links, operations, comments, parameters, graph IO, and extra data.

- `src/object/builtin/ckbehavior_schemas.c`
  - Change CKBehavior file serialization to write from `interface_data` when present.
  - Keep raw chunks only as load-time oracle/debug data, not as the normal save path.

- `include/object/builtin/nmo_behavior_schemas.h`
  - Document that `interface_data` is the authoritative representation after successful parse.

- `tests/unit/test_interface_chunk.c`
  - Add parser/writer byte and structural round-trip tests.

- `tests/round_trip` or existing round-trip test target
  - Ensure real-file load/save uses the structured InterfaceChunk writer.

---

### Task 1: Add writer API and missing structured metadata

**Files:**
- Modify: `include/format/nmo_interface_chunk.h`

- [ ] **Step 1: Add snapshot metadata needed for writing**

Replace the snapshot fields in `nmo_interface_script_header_t`:

```c
void *snapshot_data;                    /* raw bitmap bytes */
size_t snapshot_size;
```

with:

```c
nmo_image_desc_t snapshot_desc;         /* decoded bitmap descriptor */
void *snapshot_data;                    /* decoded ARGB/RGBA pixels, NULL for empty */
size_t snapshot_size;                   /* byte size of snapshot_data */
bool has_snapshot;                      /* true when snapshot_desc carries image data */
```

Add the required include above the structure declarations:

```c
#include "format/nmo_image.h"
```

- [ ] **Step 2: Add section presence metadata for sectioned layout**

Extend `nmo_interface_body_t`:

```c
bool has_links_section;
bool has_operations_section;
bool has_comments_section;
bool has_unknown_flag_section;
int32_t unknown_flag;
```

These flags distinguish a section that was absent from a section that was present with count `0`. The writer must use them for Dev sectioned byte-level tests.

- [ ] **Step 3: Add writer declaration**

After `nmo_interface_chunk_parse()` declare:

```c
/**
 * @brief Serialize structured interface data into an InterfaceChunk.
 *
 * Writes from nmo_interface_data_t only. This function must not copy a
 * previously loaded raw InterfaceChunk as its implementation.
 *
 * @param chunk Target chunk to write
 * @param data  Structured InterfaceChunk data
 * @param ctx   Context for building-block decisions in inline layouts
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_status_t nmo_interface_chunk_write(
    nmo_chunk_t *chunk,
    const nmo_interface_data_t *data,
    const nmo_interface_parse_ctx_t *ctx);
```

- [ ] **Step 4: Build and verify the expected link failure**

Run:

```powershell
cmake --build build_debug --target test_interface_chunk
```

Expected:

```text
build succeeds
```

The API is declared but unused, so no link failure should occur yet.

---

### Task 2: Update parser to populate writer-critical metadata

**Files:**
- Modify: `src/format/interface_chunk.c`
- Modify: `tests/unit/test_interface_chunk.c`

- [ ] **Step 1: Preserve snapshot descriptor in parser**

In `parse_script_header()`, after `nmo_chunk_read_bitmap_legacy()` succeeds, populate the new fields:

```c
out->snapshot_desc = bitmap_desc;
if (bitmap_pixels && bitmap_desc.width > 0 && bitmap_desc.height > 0) {
    out->snapshot_data = bitmap_pixels;
    out->snapshot_size =
        (size_t)bitmap_desc.width * (size_t)bitmap_desc.height * 4u;
    out->has_snapshot = true;
} else {
    memset(&out->snapshot_desc, 0, sizeof(out->snapshot_desc));
    out->snapshot_data = NULL;
    out->snapshot_size = 0;
    out->has_snapshot = false;
}
```

- [ ] **Step 2: Populate section presence flags**

In sectioned `parse_body()`, set these flags only when the matching identifier is found:

```c
out->has_links_section = false;
out->has_operations_section = false;
out->has_comments_section = false;
out->has_unknown_flag_section = false;
out->unknown_flag = 0;
```

For links:

```c
if (found) {
    out->has_links_section = true;
    st = parse_links(chunk, arena, out, true);
    NMO_RETURN_IF_ERROR(st);
}
```

For operations:

```c
if (found) {
    out->has_operations_section = true;
    st = parse_operations(chunk, arena, out);
    NMO_RETURN_IF_ERROR(st);
}
```

For comments:

```c
if (found) {
    out->has_comments_section = true;
    st = parse_comments(chunk, arena, version, out);
    NMO_RETURN_IF_ERROR(st);
}
```

For the unknown flag section:

```c
if (found) {
    out->has_unknown_flag_section = true;
    st = nmo_chunk_read_int(chunk, &out->unknown_flag);
    NMO_RETURN_IF_ERROR(st);
}
```

For inline layout, set `has_links_section`, `has_operations_section`, and `has_comments_section` to `true` because the sections are implicit and sequential.

- [ ] **Step 3: Add parser metadata regression test**

Add a sectioned test that builds a body with links section present and comments section absent, then asserts:

```c
ASSERT_TRUE(data.script.body.has_links_section);
ASSERT_FALSE(data.script.body.has_comments_section);
ASSERT_FALSE(data.script.body.has_unknown_flag_section);
```

Register it:

```c
REGISTER_TEST(interface_chunk, dev_layout_tracks_section_presence);
```

- [ ] **Step 4: Run test**

Run:

```powershell
cmake --build build_debug --target test_interface_chunk
.\build_debug\tests\unit\test_interface_chunk.exe
```

Expected:

```text
all interface_chunk tests pass
```

---

### Task 3: Add byte comparison helpers and minimal writer test

**Files:**
- Modify: `tests/unit/test_interface_chunk.c`

- [ ] **Step 1: Add chunk byte comparison helper**

Add near the existing helper functions:

```c
static void assert_chunk_dwords_equal(nmo_chunk_t *expected, nmo_chunk_t *actual) {
    ASSERT_NOT_NULL(expected);
    ASSERT_NOT_NULL(actual);
    ASSERT_EQ((int)expected->data.count, (int)actual->data.count);

    uint32_t *expected_data = NMO_ARENA_ARRAY_DATA(uint32_t, &expected->data);
    uint32_t *actual_data = NMO_ARENA_ARRAY_DATA(uint32_t, &actual->data);
    for (size_t i = 0; i < expected->data.count; i++) {
        ASSERT_EQ((int)expected_data[i], (int)actual_data[i]);
    }
}
```

- [ ] **Step 2: Add minimal inline byte-level writer test**

Add:

```c
TEST(interface_chunk, write_minimal_inline_byte_round_trip) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *src = build_minimal_chunk(arena);
    ASSERT_NOT_NULL(src);

    nmo_interface_data_t data;
    memset(&data, 0, sizeof(data));
    nmo_status_t st = nmo_interface_chunk_parse(src, arena, NULL, &data);
    ASSERT_EQ(NMO_OK, st);

    nmo_chunk_t *dst = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(dst);
    st = nmo_interface_chunk_write(dst, &data, NULL);
    ASSERT_EQ(NMO_OK, st);

    assert_chunk_dwords_equal(src, dst);

    nmo_arena_destroy(arena);
}
```

Register it:

```c
REGISTER_TEST(interface_chunk, write_minimal_inline_byte_round_trip);
```

- [ ] **Step 3: Verify it fails before implementation**

Run:

```powershell
cmake --build build_debug --target test_interface_chunk
```

Expected before implementation:

```text
unresolved external symbol nmo_interface_chunk_write
```

---

### Task 4: Implement writer skeleton, identifiers, and headers

**Files:**
- Modify: `src/format/interface_chunk.c`

- [ ] **Step 1: Add forward declarations**

Add near parser helper declarations:

```c
static nmo_status_t write_script_header(
    nmo_chunk_t *chunk,
    const nmo_interface_data_t *data);

static nmo_status_t write_sub_header(
    nmo_chunk_t *chunk,
    const nmo_interface_behavior_t *sub);

static nmo_status_t write_empty_legacy_bitmap(nmo_chunk_t *chunk);
```

- [ ] **Step 2: Implement empty bitmap writer**

Do not call `nmo_chunk_write_bitmap_legacy()` for empty snapshots; it rejects `desc->image_data == NULL`. Use the same empty representation currently used by tests:

```c
static nmo_status_t write_empty_legacy_bitmap(nmo_chunk_t *chunk)
{
    nmo_status_t st = nmo_chunk_write_int(chunk, 0);
    NMO_RETURN_IF_ERROR(st);
    return nmo_chunk_write_int(chunk, 0);
}
```

- [ ] **Step 3: Implement header writers**

Add:

```c
static nmo_status_t write_script_header(
    nmo_chunk_t *chunk,
    const nmo_interface_data_t *data)
{
    const nmo_interface_script_header_t *sh = &data->script;
    nmo_status_t st;

    st = nmo_chunk_write_object_id(chunk, sh->behavior_id);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_dword(chunk, sh->flags);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_dword(chunk, sh->script_index);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_float(chunk, sh->h_pos);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_float(chunk, sh->v_pos);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_float(chunk, sh->h_start_pos);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_float(chunk, sh->v_start_pos);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_float(chunk, sh->v_size);
    NMO_RETURN_IF_ERROR(st);

    if (sh->has_snapshot && sh->snapshot_data) {
        nmo_image_desc_t desc = sh->snapshot_desc;
        desc.image_data = (uint8_t *)sh->snapshot_data;
        st = nmo_chunk_write_bitmap_legacy(chunk, &desc, NULL);
        NMO_RETURN_IF_ERROR(st);
    } else {
        st = write_empty_legacy_bitmap(chunk);
        NMO_RETURN_IF_ERROR(st);
    }

    if (data->version >= 0x14 && !data->sectioned_layout) {
        st = nmo_chunk_write_dword(chunk, sh->color);
        NMO_RETURN_IF_ERROR(st);
    }

    return NMO_OK;
}

static nmo_status_t write_sub_header(
    nmo_chunk_t *chunk,
    const nmo_interface_behavior_t *sub)
{
    nmo_status_t st;

    st = nmo_chunk_write_object_id(chunk, sub->behavior_id);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_dword(chunk, sub->flags);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_dword(chunk, sub->depth);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_float(chunk, sub->h_pos);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_float(chunk, sub->v_pos);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_float(chunk, sub->h_size);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_float(chunk, sub->v_size);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_float(chunk, sub->h_expand_size);
    NMO_RETURN_IF_ERROR(st);
    return nmo_chunk_write_float(chunk, sub->v_expand_size);
}
```

- [ ] **Step 4: Implement public writer skeleton**

Add:

```c
nmo_status_t nmo_interface_chunk_write(
    nmo_chunk_t *chunk,
    const nmo_interface_data_t *data,
    const nmo_interface_parse_ctx_t *ctx)
{
    (void)ctx;
    if (!chunk || !data) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "interface_chunk_write: NULL argument");
    }

    nmo_status_t st = nmo_chunk_start_write(chunk);
    NMO_RETURN_IF_ERROR(st);

    st = nmo_chunk_write_identifier(chunk, 1u);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_dword(chunk, data->version);
    NMO_RETURN_IF_ERROR(st);

    if (data->sectioned_layout) {
        st = nmo_chunk_write_identifier(chunk, DEV_SECTION_SCRIPT_MARKER);
        NMO_RETURN_IF_ERROR(st);
    }

    st = nmo_chunk_write_int(chunk, (int32_t)(data->sub_count + 1u));
    NMO_RETURN_IF_ERROR(st);

    if (data->sectioned_layout) {
        st = nmo_chunk_write_identifier(chunk, DEV_SECTION_SCRIPT_HEADER);
        NMO_RETURN_IF_ERROR(st);
    }
    st = write_script_header(chunk, data);
    NMO_RETURN_IF_ERROR(st);

    nmo_chunk_close(chunk);
    return NMO_OK;
}
```

- [ ] **Step 5: Verify minimal byte test passes**

Run:

```powershell
cmake --build build_debug --target test_interface_chunk
.\build_debug\tests\unit\test_interface_chunk.exe
```

Expected:

```text
write_minimal_inline_byte_round_trip passes
```

---

### Task 5: Implement body writer

**Files:**
- Modify: `src/format/interface_chunk.c`
- Modify: `tests/unit/test_interface_chunk.c`

- [ ] **Step 1: Add link byte-level test**

Use the existing `parse_links` synthetic layout as the source. After parsing, write with `nmo_interface_chunk_write()` and call:

```c
assert_chunk_dwords_equal(src, dst);
```

Register:

```c
REGISTER_TEST(interface_chunk, write_links_inline_byte_round_trip);
```

- [ ] **Step 2: Implement endpoint and link writer**

Add:

```c
static nmo_status_t write_endpoint(nmo_chunk_t *chunk,
    const nmo_interface_endpoint_t *endpoint)
{
    nmo_status_t st = nmo_chunk_write_object_id(chunk, endpoint->id);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_int(chunk, endpoint->index);
    NMO_RETURN_IF_ERROR(st);
    return nmo_chunk_write_dword(chunk, endpoint->type);
}

static nmo_status_t write_links(
    nmo_chunk_t *chunk,
    const nmo_interface_body_t *body,
    bool split_type_and_highlight)
{
    nmo_status_t st = nmo_chunk_write_int(chunk, (int32_t)body->link_count);
    NMO_RETURN_IF_ERROR(st);

    for (size_t i = 0; i < body->link_count; i++) {
        const nmo_interface_link_t *link = &body->links[i];
        if (split_type_and_highlight) {
            st = nmo_chunk_write_int(chunk, link->highlight ? 1 : 0);
            NMO_RETURN_IF_ERROR(st);
            st = nmo_chunk_write_int(chunk, (int32_t)link->type);
            NMO_RETURN_IF_ERROR(st);
        } else {
            uint32_t raw_type = link->type & 0xFFFFu;
            if (link->highlight) raw_type |= NMO_INTERFACE_LINK_HIGHLIGHT_FLAG;
            st = nmo_chunk_write_dword(chunk, raw_type);
            NMO_RETURN_IF_ERROR(st);
        }

        st = nmo_chunk_write_object_id(chunk, link->link_id);
        NMO_RETURN_IF_ERROR(st);
        st = write_endpoint(chunk, &link->start);
        NMO_RETURN_IF_ERROR(st);
        st = nmo_chunk_write_int(chunk, (int32_t)link->point_count);
        NMO_RETURN_IF_ERROR(st);
        for (size_t j = 0; j < link->point_count; j++) {
            st = nmo_chunk_write_float(chunk, link->points[j * 2u]);
            NMO_RETURN_IF_ERROR(st);
            st = nmo_chunk_write_float(chunk, link->points[j * 2u + 1u]);
            NMO_RETURN_IF_ERROR(st);
        }
        st = write_endpoint(chunk, &link->end);
        NMO_RETURN_IF_ERROR(st);
    }

    return NMO_OK;
}
```

- [ ] **Step 3: Implement operations, comments, parameters, graph IO writers**

Add writers matching existing parse order:

```c
static nmo_status_t write_operations(nmo_chunk_t *chunk,
    const nmo_interface_body_t *body);
static nmo_status_t write_comments(nmo_chunk_t *chunk,
    uint32_t version, const nmo_interface_body_t *body);
static nmo_status_t write_parameters(nmo_chunk_t *chunk,
    uint32_t version, const nmo_interface_param_set_t *params);
static nmo_status_t write_graph_io(nmo_chunk_t *chunk,
    const nmo_interface_graph_io_t *graph_io);
```

Implement each by writing exactly the inverse of the matching parser:

- `write_operations`: count, then object id, `h_pos`, `v_pos`.
- `write_comments`: count, four floats, `nmo_chunk_write_string(text)`, and `style_flags` only for version `>= 0x16`.
- `write_parameters`: local count, local coordinates, local styles, shared count, shared coordinates, shared styles, shared source ids; for version `< 0x15`, emit the legacy three-field shared source tuple.
- `write_graph_io`: four arrays in parser order, each as count followed by `(index, marker)` pairs.

- [ ] **Step 4: Implement `write_body()`**

Add:

```c
static nmo_status_t write_body(
    nmo_chunk_t *chunk,
    const nmo_interface_data_t *data,
    const nmo_interface_parse_ctx_t *ctx,
    const nmo_interface_body_t *body,
    nmo_object_id_t behavior_id,
    size_t behavior_index,
    bool is_script)
{
    nmo_status_t st;

    if (data->sectioned_layout) {
        if (body->has_links_section) {
            st = nmo_chunk_write_identifier(chunk,
                behavior_section_id(behavior_index, DEV_SECTION_LINKS));
            NMO_RETURN_IF_ERROR(st);
            st = write_links(chunk, body, true);
            NMO_RETURN_IF_ERROR(st);
        }
        if (body->has_operations_section) {
            st = nmo_chunk_write_identifier(chunk,
                behavior_section_id(behavior_index, DEV_SECTION_OPERATIONS));
            NMO_RETURN_IF_ERROR(st);
            st = write_operations(chunk, body);
            NMO_RETURN_IF_ERROR(st);
        }
        if (body->has_comments_section) {
            st = nmo_chunk_write_identifier(chunk,
                behavior_section_id(behavior_index, DEV_SECTION_COMMENTS));
            NMO_RETURN_IF_ERROR(st);
            st = write_comments(chunk, data->version, body);
            NMO_RETURN_IF_ERROR(st);
        }
        if (body->has_unknown_flag_section) {
            st = nmo_chunk_write_identifier(chunk,
                behavior_section_id(behavior_index, DEV_SECTION_UNKNOWN_FLAG));
            NMO_RETURN_IF_ERROR(st);
            st = nmo_chunk_write_int(chunk, body->unknown_flag);
            NMO_RETURN_IF_ERROR(st);
        }
        return NMO_OK;
    }

    st = write_links(chunk, body, false);
    NMO_RETURN_IF_ERROR(st);
    st = write_operations(chunk, body);
    NMO_RETURN_IF_ERROR(st);
    st = write_comments(chunk, data->version, body);
    NMO_RETURN_IF_ERROR(st);

    bool is_building_block = false;
    if (ctx && ctx->is_building_block) {
        is_building_block = ctx->is_building_block(behavior_id, ctx->user_data);
    }
    if (!is_building_block) {
        st = write_parameters(chunk, data->version, &body->params);
        NMO_RETURN_IF_ERROR(st);
    }
    if (!is_script && body->graph_io) {
        st = write_graph_io(chunk, body->graph_io);
        NMO_RETURN_IF_ERROR(st);
    }

    return NMO_OK;
}
```

Wire it into `nmo_interface_chunk_write()` for script body.

- [ ] **Step 5: Verify body tests pass**

Run:

```powershell
cmake --build build_debug --target test_interface_chunk
.\build_debug\tests\unit\test_interface_chunk.exe
```

Expected:

```text
write_links_inline_byte_round_trip passes
```

---

### Task 6: Implement sub-behavior and extra data writer

**Files:**
- Modify: `src/format/interface_chunk.c`
- Modify: `tests/unit/test_interface_chunk.c`

- [ ] **Step 1: Add sub-behavior byte-level test**

Use the same synthetic data shape as `parse_sub_behaviors`, then assert:

```c
assert_chunk_dwords_equal(src, dst);
```

Register:

```c
REGISTER_TEST(interface_chunk, write_sub_behaviors_inline_byte_round_trip);
```

- [ ] **Step 2: Add extra data byte-level tests**

For each existing extra data parser test, add write-back byte equality:

```c
TEST(interface_chunk, write_extra_data_v3_byte_round_trip) { /* parse, write, assert */ }
TEST(interface_chunk, write_extra_data_v2_byte_round_trip) { /* parse, write, assert */ }
TEST(interface_chunk, write_extra_data_v1_byte_round_trip) { /* parse, write, assert */ }
```

Register all three.

- [ ] **Step 3: Implement extra writer**

Implement:

```c
static nmo_status_t write_extra_data(
    nmo_chunk_t *chunk,
    const nmo_interface_extra_t *extra);
```

Rules:

- If `extra->present == false`, write nothing.
- Version `3` identifier is `NMO_INTERFACE_EXTRA_ID_V3`.
- Version `2` identifier is `NMO_INTERFACE_EXTRA_ID_V2`.
- Version `1` identifier is `NMO_INTERFACE_EXTRA_ID_V1`.
- Write entry count.
- For entry type `1` or `2`, write `id1`.
- For entry type `3`, write `id1`, `id2`.
- For entry type `4`, write `value`.
- For version `>= 2`, write sub-count and each sub-entry.
- For version `2`, reverse the parser's `value1 += 2` adjustment by writing `value1 - 2` when stored `value1 >= 6`.
- For sub-entry `value1` in `{2,3,8,9,10,11}`, write `id2`.
- Otherwise write `data` using `nmo_chunk_write_buffer()`.

- [ ] **Step 4: Wire sub-behavior and extra writer into public writer**

After script body in `nmo_interface_chunk_write()`:

```c
for (size_t i = 0; i < data->sub_count; i++) {
    size_t behavior_index = i + 1u;
    if (data->sectioned_layout) {
        st = nmo_chunk_write_identifier(chunk,
            behavior_section_id(behavior_index, DEV_SECTION_HEADER));
        NMO_RETURN_IF_ERROR(st);
    }
    st = write_sub_header(chunk, &data->subs[i]);
    NMO_RETURN_IF_ERROR(st);
    if (data->subs[i].body.has_body) {
        st = write_body(chunk, data, ctx, &data->subs[i].body,
                        data->subs[i].behavior_id, behavior_index, false);
        NMO_RETURN_IF_ERROR(st);
    }
}

st = write_extra_data(chunk, &data->extra);
NMO_RETURN_IF_ERROR(st);
```

- [ ] **Step 5: Verify sub-behavior and extra tests pass**

Run:

```powershell
cmake --build build_debug --target test_interface_chunk
.\build_debug\tests\unit\test_interface_chunk.exe
```

Expected:

```text
all interface_chunk tests pass
```

---

### Task 7: Add Dev sectioned byte-level writer tests

**Files:**
- Modify: `tests/unit/test_interface_chunk.c`

- [ ] **Step 1: Add sectioned links test**

Use a sectioned chunk with:

- version identifier `1`
- `DEV_SECTION_SCRIPT_MARKER`
- `DEV_SECTION_SCRIPT_HEADER`
- `DEV_SECTION_LINKS`
- one link with split highlight/type fields
- empty control point list

After parse and write:

```c
ASSERT_TRUE(data.sectioned_layout);
assert_chunk_dwords_equal(src, dst);
```

Register:

```c
REGISTER_TEST(interface_chunk, write_sectioned_links_byte_round_trip);
```

- [ ] **Step 2: Add sectioned comments test**

Use a sectioned chunk with one v0x16 comment and nonzero `style_flags`. After parse and write:

```c
assert_chunk_dwords_equal(src, dst);
```

Register:

```c
REGISTER_TEST(interface_chunk, write_sectioned_comments_byte_round_trip);
```

- [ ] **Step 3: Run sectioned tests**

Run:

```powershell
cmake --build build_debug --target test_interface_chunk
.\build_debug\tests\unit\test_interface_chunk.exe
```

Expected:

```text
write_sectioned_links_byte_round_trip passes
write_sectioned_comments_byte_round_trip passes
```

---

### Task 8: Integrate structured writer into CKBehavior save

**Files:**
- Modify: `src/object/builtin/ckbehavior_schemas.c`
- Modify: `include/object/builtin/nmo_behavior_schemas.h`
- Modify: `tests/unit/test_interface_chunk.c`

- [ ] **Step 1: Update behavior serialization**

In `nmo_behavior_serialize()`, replace raw InterfaceChunk writing:

```c
if (in_state->interface_chunk) {
    result = nmo_chunk_write_sub_chunk(out_chunk, in_state->interface_chunk);
    if (result != NMO_OK) return result;
} else {
    result = nmo_chunk_write_dword(out_chunk, 0);
    if (result != NMO_OK) return result;
}
```

with:

```c
if (in_state->interface_data) {
    nmo_chunk_t *interface_out = nmo_chunk_create(out_chunk->arena);
    if (!interface_out) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Cannot allocate InterfaceChunk output");
    }
    result = nmo_interface_chunk_write(interface_out,
                                       in_state->interface_data,
                                       NULL);
    if (result != NMO_OK) return result;
    result = nmo_chunk_write_sub_chunk(out_chunk, interface_out);
    if (result != NMO_OK) return result;
} else if (in_state->has_interface) {
    NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                     "Behavior has InterfaceChunk but no parsed interface_data");
}
```

This intentionally does not fall back to `interface_chunk`.

- [ ] **Step 2: Update header comments**

Change `nmo_behavior_state_t::interface_chunk` documentation to:

```c
nmo_chunk_t *interface_chunk;          /**< Original InterfaceChunk oracle retained for diagnostics */
```

Change `interface_data` documentation to:

```c
nmo_interface_data_t *interface_data;  /**< Structured InterfaceChunk data used for save after successful parse */
```

- [ ] **Step 3: Add integration test that save path uses structured data**

Add a test that loads `data/BBSamples/Collisions/Prevent Collision.cmo`, finds file id `250`, modifies a harmless structured UI field, saves, reloads, and observes the changed field in the reloaded `interface_data`.

Use:

```c
((nmo_behavior_state_t *)state)->interface_data->script.h_start_pos += 1.0f;
```

Then save through the existing session save helper used by other round-trip tests and reload the saved file. The reloaded script `h_start_pos` must match the changed value. If this test still passes when `interface_data` is ignored and `interface_chunk` is copied, the test is invalid.

- [ ] **Step 4: Run integration test**

Run:

```powershell
cmake --build build_debug --target test_interface_chunk
.\build_debug\tests\unit\test_interface_chunk.exe
```

Expected:

```text
structured save-path integration test passes
```

---

### Task 9: Real-sample byte-level oracle

**Files:**
- Modify: `tests/unit/test_interface_chunk.c`
- Modify existing round-trip tests if they currently only compare whole-file success.

- [ ] **Step 1: Add real-sample InterfaceChunk byte-level loop**

Load at least these files when present:

```text
data/BBSamples/3D Transformations/Look At.cmo
data/BBSamples/Collisions/Prevent Collision.cmo
```

For each non-building-block CKBehavior with `interface_data`:

1. Use original raw `state->interface_chunk` as oracle input only.
2. Call `nmo_interface_chunk_write()` into a new chunk.
3. Compare dword counts and dword values.
4. Fail with object id/file id/name on first mismatch.

The test must not save by copying `state->interface_chunk`.

- [ ] **Step 2: Run the targeted oracle test**

Run:

```powershell
cmake --build build_debug --target test_interface_chunk
.\build_debug\tests\unit\test_interface_chunk.exe
```

Expected:

```text
real-sample InterfaceChunk writer byte-level oracle passes
```

- [ ] **Step 3: Run full regression**

Run:

```powershell
ctest --test-dir build_debug --output-on-failure
```

Expected:

```text
100% tests passed, 0 tests failed out of 139+
```

---

## Acceptance Criteria

- `nmo_interface_chunk_write()` exists and writes from `nmo_interface_data_t`.
- CKBehavior save uses `interface_data` for successfully parsed InterfaceChunks.
- Existing raw chunks are not used as the normal save implementation.
- Inline and Dev sectioned tests cover behavior links, parameter links, highlights, endpoints, empty and non-empty control point lists, operations, comments, local/shared params, graph IO, sub-behaviors, and extra data.
- Real-sample writer oracle passes on curated BBSamples before claiming byte-level correctness.
- Full CTest passes after structured writer integration.

---

## Self-Review

- Spec coverage: This plan now explicitly pushes structured writer implementation and treats raw chunks only as oracle input.
- Placeholder scan: The plan contains no placeholder work items or vague "write tests later" steps.
- Type consistency: The plan uses existing libnmo symbols and names the new public API consistently as `nmo_interface_chunk_write()`.

---

## Execution Handoff

Plan revised and saved to `docs/superpowers/plans/2026-04-12-interface-chunk-write.md`.

Recommended execution: use `superpowers:executing-plans` in this workspace and complete tasks in order. Do not skip the byte-level oracle tasks; they are the guardrail that prevents the writer from becoming a parse-equivalent but Dev-incompatible serializer.
