/**
 * @file interface_chunk.c
 * @brief CKBehavior interface chunk parser implementation
 */

#include "format/nmo_interface_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"

#include <string.h>

/* ================================================================
 * Section identifiers for v >= 0x0D sectioned layout
 *
 * CK2's CKBehavior::SaveInterfaceData writes each header and body
 * section behind its own identifier.  SeekIdentifier jumps to the
 * position in the identifier chain, so reads are non-sequential.
 * ================================================================ */

/* Values from Dev.exe reverse-engineering (VirtoolsScriptDeobfuscation).
 * InterfaceDataSectionId(layoutIndex, base) = base + layoutIndex. */
#define DEV_SECTION_HEADER           0xB0010000u
#define DEV_SECTION_OPERATIONS       0xB0020000u
#define DEV_SECTION_LINKS            0xB0030000u
#define DEV_SECTION_LOCAL_PARAMS     0xB0040000u
#define DEV_SECTION_GRAPH_INPUTS     0xB0050000u
#define DEV_SECTION_GRAPH_OUTPUTS    0xB0060000u
#define DEV_SECTION_SCRIPT_HEADER    0xB0070000u
#define DEV_SECTION_COMMENTS         0xB0080000u
#define DEV_SECTION_SHARED_PARAMS    0xB0090000u
#define DEV_SECTION_UNKNOWN_FLAG     0xB00A0000u
#define DEV_SECTION_TOP_MARKER       0xB0000001u
#define DEV_SECTION_SCRIPT_MARKER    0xB0000002u
#define DEV_SECTION_GRAPH_MARKER     0xB0000000u
#define DEV_LEGACY_TOP_MARKER        1u
#define DEV_LEGACY_SCRIPT_MARKER     2u
#define DEV_LEGACY_GRAPH_MARKER      3u
#define DEV_UNKNOWN_FLAG_VALUE       0

#define interface_is_sectioned(data) \
    (((data)->format_flags & NMO_INTERFACE_FORMAT_SECTIONED) != 0u)
#define interface_root_is_graph(data) \
    (((data)->format_flags & NMO_INTERFACE_FORMAT_ROOT_GRAPH) != 0u)

/* ================================================================
 * Internal helpers
 * ================================================================ */

static nmo_status_t parse_script_header(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    uint32_t version,
    bool use_sectioned,
    nmo_interface_script_header_t *out,
    uint32_t *format_flags);

static nmo_status_t parse_sub_behavior(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    uint32_t version,
    const nmo_interface_parse_ctx_t *ctx,
    size_t behavior_index,
    nmo_interface_behavior_t *out);

static nmo_status_t parse_body(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    uint32_t version,
    const nmo_interface_parse_ctx_t *ctx,
    nmo_object_id_t behavior_id,
    size_t behavior_index,
    bool is_script,
    bool use_sectioned,
    nmo_interface_body_t *out);

static nmo_status_t parse_parameters(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    uint32_t version,
    nmo_interface_param_set_t *params);

static nmo_status_t parse_parameter_section(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    uint32_t version,
    bool shared,
    nmo_interface_param_set_t *params);

static nmo_status_t parse_graph_io(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_interface_graph_io_t *out);

static nmo_status_t parse_graph_io_section(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    bool outputs,
    nmo_interface_graph_io_t *out);

static nmo_status_t parse_links(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_interface_body_t *body,
    bool split_type_and_highlight);

static nmo_status_t parse_operations(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_interface_body_t *body);

static nmo_status_t parse_comments(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    uint32_t version,
    nmo_interface_body_t *body);

static nmo_status_t parse_extra_data(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_interface_extra_t *out);

static bool folded_script_body_is_omitted(nmo_chunk_t *chunk);

static nmo_status_t optional_seek_identifier(
    nmo_chunk_t *chunk,
    uint32_t id,
    bool *found)
{
    if (!chunk || !found) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "interface chunk: optional seek NULL argument");
    }

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    nmo_chunk_parser_state_t saved_state;
    if (state) {
        saved_state = *state;
    }

    nmo_status_t st = nmo_chunk_seek_identifier(chunk, id);
    if (st == NMO_OK) {
        *found = true;
        return NMO_OK;
    }

    if (state) {
        *state = saved_state;
    }
    *found = false;

    if (st == NMO_ERR_NOT_FOUND) {
        return NMO_OK;
    }

    return st;
}

static bool interface_version_is_supported(uint32_t version)
{
    return version >= NMO_INTERFACE_VERSION_MIN &&
           version <= NMO_INTERFACE_VERSION_MAX;
}

static uint32_t behavior_section_id(size_t behavior_index, uint32_t base)
{
    return (uint32_t)behavior_index + base;
}

static size_t interface_identifier_remaining_dwords(
    const nmo_chunk_t *chunk)
{
    if (!chunk || !chunk->parser_state) return 0;

    const nmo_chunk_parser_state_t *state =
        (const nmo_chunk_parser_state_t *)chunk->parser_state;
    const uint32_t *data =
        NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    size_t next_pos = chunk->data.count;
    if (state->prev_identifier_pos + 1u < chunk->data.count) {
        const uint32_t candidate = data[state->prev_identifier_pos + 1u];
        if (candidate != 0 && candidate <= chunk->data.count) {
            next_pos = candidate;
        }
    }
    if (next_pos < state->current_pos) return 0;
    return next_pos - state->current_pos;
}

/* ================================================================
 * Public API
 * ================================================================ */

static nmo_status_t nmo_interface_chunk_parse_impl(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    const nmo_interface_parse_ctx_t *ctx,
    nmo_interface_data_t *out)
{
    if (!chunk || !arena || !out) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "interface_chunk_parse: NULL argument");
    }

    memset(out, 0, sizeof(*out));

    nmo_status_t st = nmo_chunk_start_read(chunk);
    NMO_RETURN_IF_ERROR(st);

    /* Read version: Dev.exe probes the sectioned marker first, then the
     * legacy marker used by non-sectioned/inline interface chunks. */
    uint32_t version = 0;
    uint32_t candidate_version = 0;
    bool found = false;
    st = optional_seek_identifier(chunk, DEV_SECTION_TOP_MARKER, &found);
    NMO_RETURN_IF_ERROR(st);
    if (found) {
        st = nmo_chunk_read_dword(chunk, &candidate_version);
        NMO_RETURN_IF_ERROR(st);
        if (interface_version_is_supported(candidate_version)) {
            version = candidate_version;
        }
    }

    if (version == 0) {
        st = optional_seek_identifier(chunk, DEV_LEGACY_TOP_MARKER, &found);
        NMO_RETURN_IF_ERROR(st);
        if (found) {
            st = nmo_chunk_read_dword(chunk, &candidate_version);
            NMO_RETURN_IF_ERROR(st);
            version = candidate_version;
        }
    }

    if (!interface_version_is_supported(version)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "interface chunk version 0x%08X out of range "
                         "[0x%02X, 0x%02X]",
                         version, NMO_INTERFACE_VERSION_MIN,
                         NMO_INTERFACE_VERSION_MAX);
    }

    /* Detect sectioned layout.  Dev.exe writes script-type identifiers
     * before the behavior count.  Standard Virtools files only have
     * version identifiers (1 / 0xB0000001).
     *
     * The reference Save path writes DEV_SECTION_SCRIPT_MARKER (0xB0000002)
     * and DEV_SECTION_GRAPH_MARKER (0xB0000000), while its Load path seeks
     * plain 2/3.  We probe both sets for maximum compatibility. */
    uint32_t format_flags = 0;
    st = optional_seek_identifier(chunk, DEV_SECTION_SCRIPT_MARKER, &found);
    NMO_RETURN_IF_ERROR(st);
    if (!found) {
        st = optional_seek_identifier(chunk, DEV_LEGACY_SCRIPT_MARKER, &found);
        NMO_RETURN_IF_ERROR(st);
    }
    if (found) {
        format_flags |= NMO_INTERFACE_FORMAT_SECTIONED |
                        NMO_INTERFACE_FORMAT_SECTION_PRESENCE;
    } else {
        st = optional_seek_identifier(chunk, DEV_SECTION_GRAPH_MARKER, &found);
        NMO_RETURN_IF_ERROR(st);
        if (!found) {
            st = optional_seek_identifier(chunk, DEV_LEGACY_GRAPH_MARKER, &found);
            NMO_RETURN_IF_ERROR(st);
        }
        if (found) {
            format_flags |= NMO_INTERFACE_FORMAT_SECTIONED |
                            NMO_INTERFACE_FORMAT_SECTION_PRESENCE |
                            NMO_INTERFACE_FORMAT_ROOT_GRAPH;
        }
    }

    /* Read total behavior count */
    int32_t total_count = 0;
    st = nmo_chunk_read_int(chunk, &total_count);
    NMO_RETURN_IF_ERROR(st);

    if (total_count < 1 || total_count > 100000) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "interface chunk behavior count %d out of range",
                         total_count);
    }

    out->version = version;
    out->format_flags = format_flags;
    out->sub_count = (total_count > 1) ? (size_t)(total_count - 1) : 0;

    /* Seek to script header section (sectioned layout only).
     * For inline layout the header follows sequentially. */
    bool use_sectioned = (format_flags & NMO_INTERFACE_FORMAT_SECTIONED) != 0u;
    if (use_sectioned) {
        st = optional_seek_identifier(chunk,
            (format_flags & NMO_INTERFACE_FORMAT_ROOT_GRAPH)
                ? DEV_SECTION_HEADER
                : DEV_SECTION_SCRIPT_HEADER,
            &found);
        NMO_RETURN_IF_ERROR(st);
    }
    st = parse_script_header(chunk, arena, version, use_sectioned,
                             &out->script, &out->format_flags);
    NMO_RETURN_IF_ERROR(st);

    /* Script body */
    if (out->script.body.has_body) {
        st = parse_body(chunk, arena, version, ctx,
                        out->script.behavior_id, 0, true,
                        use_sectioned, &out->script.body);
        NMO_RETURN_IF_ERROR(st);
    }

    /* Sub-behavior loop */
    if (out->sub_count > 0) {
        out->subs = nmo_arena_alloc(arena,
            out->sub_count * sizeof(nmo_interface_behavior_t),
            alignof(nmo_interface_behavior_t));
        if (!out->subs) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "interface chunk: cannot allocate sub-behaviors");
        }
        memset(out->subs, 0, out->sub_count * sizeof(nmo_interface_behavior_t));

        for (size_t i = 0; i < out->sub_count; i++) {
            uint32_t layout_index = (uint32_t)(i + 1);
            if (use_sectioned) {
                st = optional_seek_identifier(chunk,
                    DEV_SECTION_HEADER + layout_index, &found);
                NMO_RETURN_IF_ERROR(st);
            }
            st = parse_sub_behavior(chunk, arena, version, ctx,
                                    layout_index, &out->subs[i]);
            NMO_RETURN_IF_ERROR(st);

            /* Sub-behavior body */
            if (out->subs[i].body.has_body) {
                st = parse_body(chunk, arena, version, ctx,
                                out->subs[i].behavior_id, layout_index,
                                false, use_sectioned, &out->subs[i].body);
                NMO_RETURN_IF_ERROR(st);
            }
        }
    } else {
        out->subs = NULL;
    }

    /* Extra data section */
    st = parse_extra_data(chunk, arena, &out->extra);
    NMO_RETURN_IF_ERROR(st);

    return NMO_OK;
}

nmo_status_t nmo_interface_chunk_parse(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    const nmo_interface_parse_ctx_t *ctx,
    nmo_interface_data_t *out)
{
    if (!out) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_interface_data_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    nmo_status_t status = nmo_interface_chunk_parse_impl(
        chunk, arena, ctx, &parsed);
    if (status != NMO_OK) {
        memset(out, 0, sizeof(*out));
        return status;
    }
    *out = parsed;
    return NMO_OK;
}

static nmo_status_t parse_parameter_section(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    uint32_t version,
    bool shared,
    nmo_interface_param_set_t *params)
{
    int32_t count = 0;
    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &count));
    if (count < 0 || count > 100000) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "interface chunk: sectioned parameter count %d out of range", count);
    }
    const size_t mapping_dwords = shared
        ? (version >= 0x15 ? 1u : 3u) : 0u;
    const size_t minimum_dwords_per_item = 3u + mapping_dwords;
    if ((size_t)count >
        interface_identifier_remaining_dwords(chunk) /
            minimum_dwords_per_item) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }

    nmo_interface_param_t **items = shared ? &params->shared : &params->locals;
    size_t *item_count = shared ? &params->shared_count : &params->local_count;
    *item_count = (size_t)count;
    *items = NULL;
    if (count == 0) return NMO_OK;

    *items = nmo_arena_alloc(arena, (size_t)count * sizeof(**items),
                             alignof(nmo_interface_param_t));
    if (!*items) return NMO_ERR_NOMEM;
    memset(*items, 0, (size_t)count * sizeof(**items));

    for (int32_t i = 0; i < count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &(*items)[i].h_pos));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &(*items)[i].v_pos));
    }
    for (int32_t i = 0; i < count; ++i) {
        int32_t style = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &style));
        (*items)[i].style = (uint32_t)style;
    }

    if (shared) {
        for (int32_t i = 0; i < count; ++i) {
            nmo_interface_param_t *item = &(*items)[i];
            if (version >= 0x15) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_object_id(
                    chunk, &item->mapping_tag0));
                item->source_id = item->mapping_tag0;
                item->mapping_field_count = 1;
            } else {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_object_id(
                    chunk, &item->mapping_tag0));
                NMO_RETURN_IF_ERROR(nmo_chunk_read_object_id(
                    chunk, &item->mapping_tag1));
                NMO_RETURN_IF_ERROR(nmo_chunk_read_int(
                    chunk, &item->mapping_value));
                item->source_id = item->mapping_tag1;
                item->mapping_field_count = 3;
            }
        }
    }
    return NMO_OK;
}

/* ================================================================
 * Script header
 * ================================================================ */

static nmo_status_t parse_script_header(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    uint32_t version,
    bool use_sectioned,
    nmo_interface_script_header_t *out,
    uint32_t *format_flags)
{
    (void)arena;
    nmo_status_t st;

    /* behavior_id */
    st = nmo_chunk_read_object_id(chunk, &out->behavior_id);
    NMO_RETURN_IF_ERROR(st);

    /* flags */
    st = nmo_chunk_read_dword(chunk, &out->flags);
    NMO_RETURN_IF_ERROR(st);

    /* script_index */
    st = nmo_chunk_read_dword(chunk, &out->script_index);
    NMO_RETURN_IF_ERROR(st);

    /* Common behavior rect position (read for all entries) */
    st = nmo_chunk_read_float(chunk, &out->h_pos);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_read_float(chunk, &out->v_pos);
    NMO_RETURN_IF_ERROR(st);

    /* Script-specific: h_start_pos, v_start_pos, v_size */
    st = nmo_chunk_read_float(chunk, &out->h_start_pos);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_read_float(chunk, &out->v_start_pos);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_read_float(chunk, &out->v_size);
    NMO_RETURN_IF_ERROR(st);

    /*
     * Bitmap (snapshot): legacy format, two ints for sizes, then payload.
     * For empty bitmaps both sizes are 0 and ReadBitmap returns immediately.
     * We use nmo_chunk_read_bitmap_legacy which handles all cases.
     * We store the raw pixel data if present, otherwise NULL/0.
     */
    nmo_image_desc_t bitmap_desc;
    uint8_t *bitmap_pixels = NULL;
    nmo_bitmap_properties_t bitmap_props;
    memset(&bitmap_desc, 0, sizeof(bitmap_desc));
    memset(&bitmap_props, 0, sizeof(bitmap_props));

    st = nmo_chunk_read_bitmap_legacy(chunk, &bitmap_desc, &bitmap_pixels,
                                      &bitmap_props);
    NMO_RETURN_IF_ERROR(st);

    out->snapshot_desc = bitmap_desc;
    out->snapshot_props = bitmap_props;
    if (bitmap_pixels && bitmap_desc.width > 0 && bitmap_desc.height > 0) {
        out->snapshot_data = bitmap_pixels;
        out->snapshot_size = (size_t)bitmap_desc.width * (size_t)bitmap_desc.height * 4u;
        out->has_snapshot = true;
    } else {
        memset(&out->snapshot_desc, 0, sizeof(out->snapshot_desc));
        memset(&out->snapshot_props, 0, sizeof(out->snapshot_props));
        out->snapshot_data = NULL;
        out->snapshot_size = 0;
        out->has_snapshot = false;
    }

    if (format_flags) {
        *format_flags &= ~NMO_INTERFACE_FORMAT_COLOR_PRESENT;
    }
    if (use_sectioned) {
        out->color = NMO_INTERFACE_DEFAULT_HEADER_COLOR;
    } else if (version >= 0x14) {
        st = nmo_chunk_read_dword(chunk, &out->color);
        NMO_RETURN_IF_ERROR(st);
        if (format_flags) {
            *format_flags |= NMO_INTERFACE_FORMAT_COLOR_PRESENT;
        }
    } else {
        out->color = 0;
    }

    /* Determine whether body is present (caller will parse it) */
    out->body.has_body = !(out->flags & NMO_INTERFACE_FLAG_HEADER_ONLY);
    if (out->body.has_body &&
        (out->flags & NMO_INTERFACE_FLAG_FOLDED) &&
        folded_script_body_is_omitted(chunk)) {
        out->body.has_body = false;
    }

    return NMO_OK;
}

static bool folded_script_body_is_omitted(nmo_chunk_t *chunk)
{
    size_t pos = nmo_chunk_get_position(chunk);
    if (pos == (size_t)-1) {
        return false;
    }

    size_t readable_dwords = nmo_chunk_get_data_size(chunk) / sizeof(uint32_t);
    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (state && state->data_size < readable_dwords) {
        readable_dwords = state->data_size;
    }

    if (pos >= readable_dwords) {
        return true;
    }

    if (!nmo_chunk_has_read_capacity(chunk, 1)) {
        return false;
    }

    uint32_t next = 0;
    if (nmo_chunk_read_dword(chunk, &next) != NMO_OK) {
        (void)nmo_chunk_goto(chunk, pos);
        return false;
    }
    (void)nmo_chunk_goto(chunk, pos);

    return next == NMO_INTERFACE_EXTRA_ID_V1 ||
           next == NMO_INTERFACE_EXTRA_ID_V2 ||
           next == NMO_INTERFACE_EXTRA_ID_V3;
}

/* ================================================================
 * Sub-behavior header + body
 * ================================================================ */

static nmo_status_t parse_sub_behavior(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    uint32_t version,
    const nmo_interface_parse_ctx_t *ctx,
    size_t behavior_index,
    nmo_interface_behavior_t *out)
{
    (void)arena; (void)version; (void)ctx; (void)behavior_index;
    nmo_status_t st;

    /* behavior_id */
    st = nmo_chunk_read_object_id(chunk, &out->behavior_id);
    NMO_RETURN_IF_ERROR(st);

    /* flags */
    st = nmo_chunk_read_dword(chunk, &out->flags);
    NMO_RETURN_IF_ERROR(st);

    /* depth */
    st = nmo_chunk_read_dword(chunk, &out->depth);
    NMO_RETURN_IF_ERROR(st);

    /* 6 floats: h_pos, v_pos, h_size, v_size, h_expand_size, v_expand_size */
    st = nmo_chunk_read_float(chunk, &out->h_pos);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_read_float(chunk, &out->v_pos);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_read_float(chunk, &out->h_size);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_read_float(chunk, &out->v_size);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_read_float(chunk, &out->h_expand_size);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_read_float(chunk, &out->v_expand_size);
    NMO_RETURN_IF_ERROR(st);

    /* Determine whether body is present (caller will parse it) */
    out->body.has_body = !(out->flags & NMO_INTERFACE_FLAG_HEADER_ONLY);

    return NMO_OK;
}

/* ================================================================
 * Body (shared between script and sub-behaviors)
 * ================================================================ */

static nmo_status_t parse_body(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    uint32_t version,
    const nmo_interface_parse_ctx_t *ctx,
    nmo_object_id_t behavior_id,
    size_t behavior_index,
    bool is_script,
    bool use_sectioned,
    nmo_interface_body_t *out)
{
    nmo_status_t st;

    if (use_sectioned) {
        /* ---- Sectioned layout (Dev.exe files) ----
         * Each body table is behind its own identifier. */
        bool found = false;

        out->has_links_section = false;
        out->has_operations_section = false;
        out->has_comments_section = false;
        out->has_unknown_flag_section = false;
        out->has_local_params_section = false;
        out->has_shared_params_section = false;
        out->has_graph_inputs_section = false;
        out->has_graph_outputs_section = false;
        out->unknown_flag = 0;

        st = optional_seek_identifier(chunk,
            behavior_section_id(behavior_index, DEV_SECTION_LINKS), &found);
        NMO_RETURN_IF_ERROR(st);
        if (found) {
            out->has_links_section = true;
            st = parse_links(chunk, arena, out, true);
            NMO_RETURN_IF_ERROR(st);
        }

        st = optional_seek_identifier(chunk,
            behavior_section_id(behavior_index, DEV_SECTION_OPERATIONS), &found);
        NMO_RETURN_IF_ERROR(st);
        if (found) {
            out->has_operations_section = true;
            st = parse_operations(chunk, arena, out);
            NMO_RETURN_IF_ERROR(st);
        }

        st = optional_seek_identifier(chunk,
            behavior_section_id(behavior_index, DEV_SECTION_COMMENTS), &found);
        NMO_RETURN_IF_ERROR(st);
        if (found) {
            out->has_comments_section = true;
            st = parse_comments(chunk, arena, version, out);
            NMO_RETURN_IF_ERROR(st);
        }

        memset(&out->params, 0, sizeof(out->params));
        out->has_params = false;
        out->graph_io = NULL;
        out->has_graph_io = false;

        st = optional_seek_identifier(chunk,
            behavior_section_id(behavior_index, DEV_SECTION_LOCAL_PARAMS), &found);
        NMO_RETURN_IF_ERROR(st);
        if (found) {
            out->has_local_params_section = true;
            out->has_params = true;
            st = parse_parameter_section(chunk, arena, version, false, &out->params);
            NMO_RETURN_IF_ERROR(st);
        }

        st = optional_seek_identifier(chunk,
            behavior_section_id(behavior_index, DEV_SECTION_SHARED_PARAMS), &found);
        NMO_RETURN_IF_ERROR(st);
        if (found) {
            out->has_shared_params_section = true;
            out->has_params = true;
            st = parse_parameter_section(chunk, arena, version, true, &out->params);
            NMO_RETURN_IF_ERROR(st);
        }

        st = optional_seek_identifier(chunk,
            behavior_section_id(behavior_index, DEV_SECTION_GRAPH_INPUTS), &found);
        NMO_RETURN_IF_ERROR(st);
        if (found) {
            out->has_graph_inputs_section = true;
            out->has_graph_io = true;
            out->graph_io = nmo_arena_alloc(arena, sizeof(*out->graph_io),
                                            alignof(nmo_interface_graph_io_t));
            if (!out->graph_io) return NMO_ERR_NOMEM;
            memset(out->graph_io, 0, sizeof(*out->graph_io));
            st = parse_graph_io_section(chunk, arena, false, out->graph_io);
            NMO_RETURN_IF_ERROR(st);
        }

        st = optional_seek_identifier(chunk,
            behavior_section_id(behavior_index, DEV_SECTION_GRAPH_OUTPUTS), &found);
        NMO_RETURN_IF_ERROR(st);
        if (found) {
            out->has_graph_outputs_section = true;
            out->has_graph_io = true;
            if (!out->graph_io) {
                out->graph_io = nmo_arena_alloc(arena, sizeof(*out->graph_io),
                                                alignof(nmo_interface_graph_io_t));
                if (!out->graph_io) return NMO_ERR_NOMEM;
                memset(out->graph_io, 0, sizeof(*out->graph_io));
            }
            st = parse_graph_io_section(chunk, arena, true, out->graph_io);
            NMO_RETURN_IF_ERROR(st);
        }

        st = optional_seek_identifier(chunk,
            behavior_section_id(behavior_index, DEV_SECTION_UNKNOWN_FLAG),
            &found);
        NMO_RETURN_IF_ERROR(st);
        if (found) {
            out->has_unknown_flag_section = true;
            st = nmo_chunk_read_int(chunk, &out->unknown_flag);
            NMO_RETURN_IF_ERROR(st);
        }

        return NMO_OK;
    }

    /* ---- Inline layout (standard Virtools files) ----
     * Body sections are sequential: links, ops, comments, params, graph IO. */

    out->has_links_section = true;
    out->has_operations_section = true;
    out->has_comments_section = true;
    out->has_unknown_flag_section = false;
    out->unknown_flag = 0;

    st = parse_links(chunk, arena, out, false);
    NMO_RETURN_IF_ERROR(st);

    st = parse_operations(chunk, arena, out);
    NMO_RETURN_IF_ERROR(st);

    st = parse_comments(chunk, arena, version, out);
    NMO_RETURN_IF_ERROR(st);

    bool is_building_block = false;
    if (ctx && ctx->is_building_block) {
        is_building_block = ctx->is_building_block(behavior_id, ctx->user_data);
    }

    if (!is_building_block) {
        out->has_params = true;
        st = parse_parameters(chunk, arena, version, &out->params);
        NMO_RETURN_IF_ERROR(st);
    } else {
        out->has_params = false;
        memset(&out->params, 0, sizeof(out->params));
    }

    out->graph_io = NULL;
    out->has_graph_io = false;
    if (!is_script) {
        bool need_graph_io = false;
        if (version == NMO_INTERFACE_VERSION_MIN) {
            need_graph_io = true;
        } else if (ctx && !is_building_block) {
            need_graph_io = true;
        }
        if (need_graph_io) {
            nmo_interface_graph_io_t *gio = nmo_arena_alloc(
                arena, sizeof(nmo_interface_graph_io_t),
                alignof(nmo_interface_graph_io_t));
            if (!gio) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "interface chunk: cannot allocate graph IO");
            }
            memset(gio, 0, sizeof(*gio));
            st = parse_graph_io(chunk, arena, gio);
            NMO_RETURN_IF_ERROR(st);
            out->graph_io = gio;
            out->has_graph_io = true;
        }
    }

    return NMO_OK;
}

/* ================================================================
 * Links
 * ================================================================ */

static nmo_status_t parse_links(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_interface_body_t *body,
    bool split_type_and_highlight)
{
    nmo_status_t st;

    int32_t count = 0;
    st = nmo_chunk_read_int(chunk, &count);
    NMO_RETURN_IF_ERROR(st);

    if (count < 0 || count > 100000) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "interface chunk: link count %d out of range", count);
    }
    const size_t minimum_dwords_per_link =
        split_type_and_highlight ? 10u : 9u;
    if ((size_t)count >
        interface_identifier_remaining_dwords(chunk) /
            minimum_dwords_per_link) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    body->link_count = (size_t)count;

    if (body->link_count == 0) {
        body->links = NULL;
        return NMO_OK;
    }

    body->links = nmo_arena_alloc(arena,
        body->link_count * sizeof(nmo_interface_link_t),
        alignof(nmo_interface_link_t));
    if (!body->links) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "interface chunk: cannot allocate links");
    }
    memset(body->links, 0, body->link_count * sizeof(nmo_interface_link_t));

    for (size_t i = 0; i < body->link_count; i++) {
        nmo_interface_link_t *link = &body->links[i];

        if (split_type_and_highlight) {
            int32_t highlight = 0;
            int32_t link_type = 0;
            st = nmo_chunk_read_int(chunk, &highlight);
            NMO_RETURN_IF_ERROR(st);
            st = nmo_chunk_read_int(chunk, &link_type);
            NMO_RETURN_IF_ERROR(st);
            link->type = (uint32_t)link_type;
            link->highlight = highlight != 0;
        } else {
            /* raw_type: low 16 bits = type, bit 0x10000 = highlight */
            uint32_t raw_type = 0;
            st = nmo_chunk_read_dword(chunk, &raw_type);
            NMO_RETURN_IF_ERROR(st);

            link->type = raw_type & 0xFFFF;
            link->highlight = (raw_type & NMO_INTERFACE_LINK_HIGHLIGHT_FLAG) != 0;
        }

        /* link_id */
        st = nmo_chunk_read_object_id(chunk, &link->link_id);
        NMO_RETURN_IF_ERROR(st);

        /* start endpoint */
        st = nmo_chunk_read_object_id(chunk, &link->start.id);
        NMO_RETURN_IF_ERROR(st);
        st = nmo_chunk_read_int(chunk, &link->start.index);
        NMO_RETURN_IF_ERROR(st);
        st = nmo_chunk_read_dword(chunk, &link->start.type);
        NMO_RETURN_IF_ERROR(st);

        /* routing points */
        int32_t point_count = 0;
        st = nmo_chunk_read_int(chunk, &point_count);
        NMO_RETURN_IF_ERROR(st);

        if (point_count < 0 || point_count > 10000) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "interface chunk: point count %d out of range", point_count);
        }
        const size_t remaining_dwords =
            interface_identifier_remaining_dwords(chunk);
        if (remaining_dwords < 3u ||
            (size_t)point_count > (remaining_dwords - 3u) / 2u) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        link->point_count = (size_t)point_count;

        if (link->point_count > 0) {
            link->points = nmo_arena_alloc(arena,
                link->point_count * 2 * sizeof(float),
                alignof(float));
            if (!link->points) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "interface chunk: cannot allocate link points");
            }

            for (size_t j = 0; j < link->point_count; j++) {
                st = nmo_chunk_read_float(chunk, &link->points[j * 2]);
                NMO_RETURN_IF_ERROR(st);
                st = nmo_chunk_read_float(chunk, &link->points[j * 2 + 1]);
                NMO_RETURN_IF_ERROR(st);
            }
        } else {
            link->points = NULL;
        }

        /* end endpoint */
        st = nmo_chunk_read_object_id(chunk, &link->end.id);
        NMO_RETURN_IF_ERROR(st);
        st = nmo_chunk_read_int(chunk, &link->end.index);
        NMO_RETURN_IF_ERROR(st);
        st = nmo_chunk_read_dword(chunk, &link->end.type);
        NMO_RETURN_IF_ERROR(st);
    }

    return NMO_OK;
}

/* ================================================================
 * Operations
 * ================================================================ */

static nmo_status_t parse_operations(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_interface_body_t *body)
{
    nmo_status_t st;

    int32_t count = 0;
    st = nmo_chunk_read_int(chunk, &count);
    NMO_RETURN_IF_ERROR(st);

    if (count < 0 || count > 100000) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "interface chunk: operation count %d out of range", count);
    }
    if ((size_t)count > interface_identifier_remaining_dwords(chunk) / 3u) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    body->operation_count = (size_t)count;

    if (body->operation_count == 0) {
        body->operations = NULL;
        return NMO_OK;
    }

    body->operations = nmo_arena_alloc(arena,
        body->operation_count * sizeof(nmo_interface_operation_t),
        alignof(nmo_interface_operation_t));
    if (!body->operations) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "interface chunk: cannot allocate operations");
    }

    for (size_t i = 0; i < body->operation_count; i++) {
        st = nmo_chunk_read_object_id(chunk, &body->operations[i].id);
        NMO_RETURN_IF_ERROR(st);
        st = nmo_chunk_read_float(chunk, &body->operations[i].h_pos);
        NMO_RETURN_IF_ERROR(st);
        st = nmo_chunk_read_float(chunk, &body->operations[i].v_pos);
        NMO_RETURN_IF_ERROR(st);
    }

    return NMO_OK;
}

/* ================================================================
 * Comments
 * ================================================================ */

static nmo_status_t read_interface_string(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    const char **out)
{
    if (!chunk || !arena || !out) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "interface chunk: string reader NULL argument");
    }

    size_t start_pos = nmo_chunk_get_position(chunk);
    if (start_pos == (size_t)-1) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "interface chunk: string reader has no parser state");
    }

    uint32_t size = 0;
    nmo_status_t st = nmo_chunk_read_dword(chunk, &size);
    NMO_RETURN_IF_ERROR(st);

    if (size == 0) {
        *out = NULL;
        return NMO_OK;
    }

    size_t payload_dwords = ((size_t)size + 3u) / 4u;
    if (!nmo_chunk_has_read_capacity(chunk, payload_dwords)) {
        (void)nmo_chunk_goto(chunk, start_pos);
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "interface chunk: truncated string payload");
    }

    char *text = nmo_arena_alloc(arena, (size_t)size, 1);
    if (!text) {
        (void)nmo_chunk_goto(chunk, start_pos);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "interface chunk: cannot allocate string");
    }

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    memcpy(text, &data[state->current_pos], (size_t)size);
    text[size - 1u] = '\0';
    state->current_pos += payload_dwords;

    *out = text;
    return NMO_OK;
}

static nmo_status_t parse_comments(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    uint32_t version,
    nmo_interface_body_t *body)
{
    nmo_status_t st;

    int32_t count = 0;
    st = nmo_chunk_read_int(chunk, &count);
    NMO_RETURN_IF_ERROR(st);

    if (count < 0 || count > 100000) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "interface chunk: comment count %d out of range", count);
    }
    const size_t minimum_dwords_per_comment = version >= 0x16 ? 6u : 5u;
    if ((size_t)count >
        interface_identifier_remaining_dwords(chunk) /
            minimum_dwords_per_comment) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    body->comment_count = (size_t)count;

    if (body->comment_count == 0) {
        body->comments = NULL;
        return NMO_OK;
    }

    body->comments = nmo_arena_alloc(arena,
        body->comment_count * sizeof(nmo_interface_comment_t),
        alignof(nmo_interface_comment_t));
    if (!body->comments) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "interface chunk: cannot allocate comments");
    }
    memset(body->comments, 0, body->comment_count * sizeof(nmo_interface_comment_t));

    for (size_t i = 0; i < body->comment_count; i++) {
        nmo_interface_comment_t *c = &body->comments[i];

        /* rect: left, top, right, bottom */
        st = nmo_chunk_read_float(chunk, &c->left);
        NMO_RETURN_IF_ERROR(st);
        st = nmo_chunk_read_float(chunk, &c->top);
        NMO_RETURN_IF_ERROR(st);
        st = nmo_chunk_read_float(chunk, &c->right);
        NMO_RETURN_IF_ERROR(st);
        st = nmo_chunk_read_float(chunk, &c->bottom);
        NMO_RETURN_IF_ERROR(st);

        st = read_interface_string(chunk, arena, &c->text);
        NMO_RETURN_IF_ERROR(st);

        /* style flags (v >= 0x16) */
        if (version >= 0x16) {
            st = nmo_chunk_read_dword(chunk, &c->style_flags);
            NMO_RETURN_IF_ERROR(st);
        } else {
            c->style_flags = 0;
        }
    }

    return NMO_OK;
}

/* ================================================================
 * Parameters (local + shared) — inline layout only
 * ================================================================ */

static nmo_status_t parse_parameters(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    uint32_t version,
    nmo_interface_param_set_t *params)
{
    nmo_status_t st;

    int32_t local_count = 0;
    st = nmo_chunk_read_int(chunk, &local_count);
    NMO_RETURN_IF_ERROR(st);

    if (local_count < 0 || local_count > 100000) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "interface chunk: local param count %d out of range", local_count);
    }
    params->local_count = (size_t)local_count;

    if (params->local_count > 0) {
        params->locals = nmo_arena_alloc(arena,
            params->local_count * sizeof(nmo_interface_param_t),
            alignof(nmo_interface_param_t));
        if (!params->locals) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "interface chunk: cannot allocate local params");
        }
        memset(params->locals, 0, params->local_count * sizeof(nmo_interface_param_t));

        for (size_t i = 0; i < params->local_count; i++) {
            st = nmo_chunk_read_int(chunk, &params->locals[i].h_pos);
            NMO_RETURN_IF_ERROR(st);
            st = nmo_chunk_read_int(chunk, &params->locals[i].v_pos);
            NMO_RETURN_IF_ERROR(st);
        }
        for (size_t i = 0; i < params->local_count; i++) {
            int32_t style = 0;
            st = nmo_chunk_read_int(chunk, &style);
            NMO_RETURN_IF_ERROR(st);
            params->locals[i].style = (uint32_t)style;
            params->locals[i].source_id = 0;
        }
    } else {
        params->locals = NULL;
    }

    int32_t shared_count = 0;
    st = nmo_chunk_read_int(chunk, &shared_count);
    NMO_RETURN_IF_ERROR(st);

    if (shared_count < 0 || shared_count > 100000) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "interface chunk: shared param count %d out of range", shared_count);
    }
    params->shared_count = (size_t)shared_count;

    if (params->shared_count > 0) {
        params->shared = nmo_arena_alloc(arena,
            params->shared_count * sizeof(nmo_interface_param_t),
            alignof(nmo_interface_param_t));
        if (!params->shared) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "interface chunk: cannot allocate shared params");
        }
        memset(params->shared, 0, params->shared_count * sizeof(nmo_interface_param_t));

        for (size_t i = 0; i < params->shared_count; i++) {
            st = nmo_chunk_read_int(chunk, &params->shared[i].h_pos);
            NMO_RETURN_IF_ERROR(st);
            st = nmo_chunk_read_int(chunk, &params->shared[i].v_pos);
            NMO_RETURN_IF_ERROR(st);
        }
        for (size_t i = 0; i < params->shared_count; i++) {
            int32_t style = 0;
            st = nmo_chunk_read_int(chunk, &style);
            NMO_RETURN_IF_ERROR(st);
            params->shared[i].style = (uint32_t)style;
        }
        for (size_t i = 0; i < params->shared_count; i++) {
            if (version >= 0x15) {
                st = nmo_chunk_read_object_id(chunk, &params->shared[i].source_id);
                NMO_RETURN_IF_ERROR(st);
            } else {
                nmo_object_id_t ignored_id = 0;
                int32_t ignored_int = 0;
                st = nmo_chunk_read_object_id(chunk, &ignored_id);
                NMO_RETURN_IF_ERROR(st);
                st = nmo_chunk_read_object_id(chunk, &params->shared[i].source_id);
                NMO_RETURN_IF_ERROR(st);
                st = nmo_chunk_read_int(chunk, &ignored_int);
                NMO_RETURN_IF_ERROR(st);
            }
        }
    } else {
        params->shared = NULL;
    }

    return NMO_OK;
}

/* ================================================================
 * Graph IO (port ordering) — inline layout only
 * ================================================================ */

static nmo_status_t parse_graph_io_array(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    int32_t **out_array,
    size_t *out_count,
    int32_t **out_tags)
{
    nmo_status_t st;
    int32_t count = 0;
    st = nmo_chunk_read_int(chunk, &count);
    NMO_RETURN_IF_ERROR(st);

    if (count < 0 || count > 100000) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "interface chunk: graph IO array count %d out of range", count);
    }
    if ((size_t)count > interface_identifier_remaining_dwords(chunk) / 2u) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    *out_count = (size_t)count;
    if (count == 0) {
        *out_array = NULL;
        if (out_tags) *out_tags = NULL;
        return NMO_OK;
    }

    *out_array = nmo_arena_alloc(arena, (size_t)count * sizeof(int32_t), alignof(int32_t));
    if (!*out_array) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "interface chunk: cannot allocate graph IO array");
    }
    if (out_tags) {
        *out_tags = nmo_arena_alloc(arena, (size_t)count * sizeof(int32_t),
                                    alignof(int32_t));
        if (!*out_tags) return NMO_ERR_NOMEM;
    }
    for (int32_t i = 0; i < count; i++) {
        st = nmo_chunk_read_int(chunk, &(*out_array)[i]);
        NMO_RETURN_IF_ERROR(st);
        int32_t ignored = 0;
        st = nmo_chunk_read_int(chunk, out_tags ? &(*out_tags)[i] : &ignored);
        NMO_RETURN_IF_ERROR(st);
    }
    return NMO_OK;
}

static nmo_status_t parse_graph_io(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_interface_graph_io_t *out)
{
    nmo_status_t st;
    st = parse_graph_io_array(chunk, arena, &out->inward_inputs,
                              &out->inward_input_count, &out->inward_input_tags);
    NMO_RETURN_IF_ERROR(st);
    st = parse_graph_io_array(chunk, arena, &out->outward_inputs,
                              &out->outward_input_count, &out->outward_input_tags);
    NMO_RETURN_IF_ERROR(st);
    st = parse_graph_io_array(chunk, arena, &out->inward_outputs,
                              &out->inward_output_count, &out->inward_output_tags);
    NMO_RETURN_IF_ERROR(st);
    st = parse_graph_io_array(chunk, arena, &out->outward_outputs,
                              &out->outward_output_count, &out->outward_output_tags);
    NMO_RETURN_IF_ERROR(st);
    return NMO_OK;
}

static nmo_status_t parse_graph_io_section(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    bool outputs,
    nmo_interface_graph_io_t *out)
{
    if (outputs) {
        NMO_RETURN_IF_ERROR(parse_graph_io_array(
            chunk, arena, &out->inward_outputs, &out->inward_output_count,
            &out->inward_output_tags));
        return parse_graph_io_array(
            chunk, arena, &out->outward_outputs, &out->outward_output_count,
            &out->outward_output_tags);
    }
    NMO_RETURN_IF_ERROR(parse_graph_io_array(
        chunk, arena, &out->inward_inputs, &out->inward_input_count,
        &out->inward_input_tags));
    return parse_graph_io_array(
        chunk, arena, &out->outward_inputs, &out->outward_input_count,
        &out->outward_input_tags);
}

/* ================================================================
 * Extra data section
 * ================================================================ */

static bool extra_sub_has_id2(int32_t value1)
{
    return value1 == 2 || value1 == 3 ||
           value1 == 8 || value1 == 9 ||
           value1 == 10 || value1 == 11;
}

static uint32_t match_extra_identifier(uint32_t value)
{
    if (value == NMO_INTERFACE_EXTRA_ID_V3) return 3;
    if (value == NMO_INTERFACE_EXTRA_ID_V2) return 2;
    if (value == NMO_INTERFACE_EXTRA_ID_V1) return 1;
    return 0;
}

static nmo_status_t parse_extra_data(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_interface_extra_t *out)
{
    nmo_status_t st;

    /*
     * Extra data follows immediately after all behaviors.
     * Try seek_identifier first (works for proper identifier chains).
     * If that fails, try reading the next DWORD directly; it may be
     * the identifier value followed by its next-pointer.
     */
    uint32_t extra_version = 0;

    nmo_chunk_parser_state_t *ps = nmo_chunk_get_parser_state(chunk);
    nmo_chunk_parser_state_t saved_ps;
    if (ps) { saved_ps = *ps; }

    st = nmo_chunk_seek_identifier(chunk, NMO_INTERFACE_EXTRA_ID_V3);
    if (st == NMO_OK) {
        extra_version = 3;
    } else {
        if (ps) { *ps = saved_ps; }
        st = nmo_chunk_seek_identifier(chunk, NMO_INTERFACE_EXTRA_ID_V2);
        if (st == NMO_OK) {
            extra_version = 2;
        } else {
            if (ps) { *ps = saved_ps; }
            st = nmo_chunk_seek_identifier(chunk, NMO_INTERFACE_EXTRA_ID_V1);
            if (st == NMO_OK) {
                extra_version = 1;
            } else {
                if (ps) { *ps = saved_ps; }
            }
        }
    }

    /* Fallback: peek at current position for identifier value */
    if (extra_version == 0 && nmo_chunk_has_read_capacity(chunk, 2)) {
        uint32_t peek_val = 0;
        size_t pos_before = nmo_chunk_get_position(chunk);
        st = nmo_chunk_read_dword(chunk, &peek_val);
        if (st == NMO_OK) {
            extra_version = match_extra_identifier(peek_val);
            if (extra_version > 0) {
                /* Skip the next-pointer DWORD */
                st = nmo_chunk_skip(chunk, 1);
                if (st != NMO_OK) extra_version = 0;
            } else {
                /* Not an identifier, restore position */
                nmo_chunk_goto(chunk, pos_before);
            }
        }
    }

    if (extra_version == 0) {
        out->present = false;
        out->version = 0;
        out->entries = NULL;
        out->entry_count = 0;
        return NMO_OK;
    }

    out->present = true;
    out->version = extra_version;

    /* Read entry count */
    int32_t entry_count = 0;
    st = nmo_chunk_read_int(chunk, &entry_count);
    NMO_RETURN_IF_ERROR(st);

    if (entry_count < 0 || entry_count > 100000) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "interface chunk: extra entry count %d out of range",
                         entry_count);
    }
    out->entry_count = (size_t)entry_count;

    if (out->entry_count == 0) {
        out->entries = NULL;
        return NMO_OK;
    }

    out->entries = nmo_arena_alloc(arena,
        out->entry_count * sizeof(nmo_interface_extra_entry_t),
        alignof(nmo_interface_extra_entry_t));
    if (!out->entries) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "interface chunk: cannot allocate extra entries");
    }
    memset(out->entries, 0, out->entry_count * sizeof(nmo_interface_extra_entry_t));

    for (size_t i = 0; i < out->entry_count; i++) {
        nmo_interface_extra_entry_t *entry = &out->entries[i];

        /* type */
        st = nmo_chunk_read_dword(chunk, &entry->type);
        NMO_RETURN_IF_ERROR(st);

        /* type-specific fields */
        switch (entry->type) {
        case 1: /* fall through */
        case 2:
            st = nmo_chunk_read_object_id(chunk, &entry->id1);
            NMO_RETURN_IF_ERROR(st);
            break;
        case 3:
            st = nmo_chunk_read_object_id(chunk, &entry->id1);
            NMO_RETURN_IF_ERROR(st);
            st = nmo_chunk_read_object_id(chunk, &entry->id2);
            NMO_RETURN_IF_ERROR(st);
            break;
        case 4:
            st = nmo_chunk_read_int(chunk, &entry->value);
            NMO_RETURN_IF_ERROR(st);
            break;
        default:
            break;
        }

        /* Sub-entries for version >= 2 */
        if (extra_version >= 2) {
            int32_t sub_count = 0;
            st = nmo_chunk_read_int(chunk, &sub_count);
            NMO_RETURN_IF_ERROR(st);

            if (sub_count < 0 || sub_count > 100000) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "interface chunk: extra sub-entry count %d out of range",
                                 sub_count);
            }
            entry->sub_count = (size_t)sub_count;

            if (entry->sub_count > 0) {
                entry->sub_entries = nmo_arena_alloc(arena,
                    entry->sub_count * sizeof(nmo_interface_extra_sub_t),
                    alignof(nmo_interface_extra_sub_t));
                if (!entry->sub_entries) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                     "interface chunk: cannot allocate extra sub-entries");
                }
                memset(entry->sub_entries, 0,
                    entry->sub_count * sizeof(nmo_interface_extra_sub_t));

                for (size_t j = 0; j < entry->sub_count; j++) {
                    nmo_interface_extra_sub_t *sub = &entry->sub_entries[j];

                    st = nmo_chunk_read_int(chunk, &sub->value1);
                    NMO_RETURN_IF_ERROR(st);

                    /* Version 2 adjustment: value1 >= 4 -> value1 += 2 */
                    if (extra_version == 2 && sub->value1 >= 4) {
                        sub->value1 += 2;
                    }

                    st = nmo_chunk_read_int(chunk, &sub->value2);
                    NMO_RETURN_IF_ERROR(st);

                    st = nmo_chunk_read_object_id(chunk, &sub->id1);
                    NMO_RETURN_IF_ERROR(st);

                    if (extra_sub_has_id2(sub->value1)) {
                        st = nmo_chunk_read_object_id(chunk, &sub->id2);
                        NMO_RETURN_IF_ERROR(st);
                        sub->data = NULL;
                        sub->data_size = 0;
                    } else {
                        sub->id2 = 0;
                        st = nmo_chunk_read_buffer(chunk,
                            &sub->data, &sub->data_size);
                        NMO_RETURN_IF_ERROR(st);
                    }
                }
            } else {
                entry->sub_entries = NULL;
            }
        } else {
            entry->sub_entries = NULL;
            entry->sub_count = 0;
        }
    }

    return NMO_OK;
}

/* ================================================================
 * Writer helpers
 * ================================================================ */

static nmo_status_t write_empty_legacy_bitmap(nmo_chunk_t *chunk)
{
    nmo_status_t st;
    st = nmo_chunk_write_int(chunk, 0);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_int(chunk, 0);
    NMO_RETURN_IF_ERROR(st);
    return NMO_OK;
}

static nmo_status_t write_endpoint_data(nmo_chunk_t *chunk,
                                        const nmo_interface_endpoint_t *ep)
{
    nmo_status_t st;
    st = nmo_chunk_write_object_id(chunk, ep->id);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_int(chunk, ep->index);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_dword(chunk, ep->type);
    NMO_RETURN_IF_ERROR(st);
    return NMO_OK;
}

static nmo_status_t write_links(nmo_chunk_t *chunk,
                                const nmo_interface_body_t *body,
                                bool split_type_and_highlight)
{
    nmo_status_t st;
    st = nmo_chunk_write_int(chunk, (int32_t)body->link_count);
    NMO_RETURN_IF_ERROR(st);

    for (size_t i = 0; i < body->link_count; i++) {
        const nmo_interface_link_t *link = &body->links[i];

        if (split_type_and_highlight) {
            st = nmo_chunk_write_int(chunk, link->highlight ? 1 : 0);
            NMO_RETURN_IF_ERROR(st);
            st = nmo_chunk_write_int(chunk, (int32_t)link->type);
            NMO_RETURN_IF_ERROR(st);
        } else {
            uint32_t raw_type = link->type;
            if (link->highlight) {
                raw_type |= NMO_INTERFACE_LINK_HIGHLIGHT_FLAG;
            }
            st = nmo_chunk_write_dword(chunk, raw_type);
            NMO_RETURN_IF_ERROR(st);
        }

        st = nmo_chunk_write_object_id(chunk, link->link_id);
        NMO_RETURN_IF_ERROR(st);

        st = write_endpoint_data(chunk, &link->start);
        NMO_RETURN_IF_ERROR(st);

        st = nmo_chunk_write_int(chunk, (int32_t)link->point_count);
        NMO_RETURN_IF_ERROR(st);

        for (size_t j = 0; j < link->point_count; j++) {
            st = nmo_chunk_write_float(chunk, link->points[j * 2]);
            NMO_RETURN_IF_ERROR(st);
            st = nmo_chunk_write_float(chunk, link->points[j * 2 + 1]);
            NMO_RETURN_IF_ERROR(st);
        }

        st = write_endpoint_data(chunk, &link->end);
        NMO_RETURN_IF_ERROR(st);
    }

    return NMO_OK;
}

static nmo_status_t write_operations(nmo_chunk_t *chunk,
                                     const nmo_interface_body_t *body)
{
    nmo_status_t st;
    st = nmo_chunk_write_int(chunk, (int32_t)body->operation_count);
    NMO_RETURN_IF_ERROR(st);

    for (size_t i = 0; i < body->operation_count; i++) {
        st = nmo_chunk_write_object_id(chunk, body->operations[i].id);
        NMO_RETURN_IF_ERROR(st);
        st = nmo_chunk_write_float(chunk, body->operations[i].h_pos);
        NMO_RETURN_IF_ERROR(st);
        st = nmo_chunk_write_float(chunk, body->operations[i].v_pos);
        NMO_RETURN_IF_ERROR(st);
    }

    return NMO_OK;
}

static nmo_status_t write_comments(nmo_chunk_t *chunk,
                                   uint32_t version,
                                   const nmo_interface_body_t *body)
{
    nmo_status_t st;
    st = nmo_chunk_write_int(chunk, (int32_t)body->comment_count);
    NMO_RETURN_IF_ERROR(st);

    for (size_t i = 0; i < body->comment_count; i++) {
        const nmo_interface_comment_t *c = &body->comments[i];
        st = nmo_chunk_write_float(chunk, c->left);
        NMO_RETURN_IF_ERROR(st);
        st = nmo_chunk_write_float(chunk, c->top);
        NMO_RETURN_IF_ERROR(st);
        st = nmo_chunk_write_float(chunk, c->right);
        NMO_RETURN_IF_ERROR(st);
        st = nmo_chunk_write_float(chunk, c->bottom);
        NMO_RETURN_IF_ERROR(st);

        st = nmo_chunk_write_string(chunk, c->text);
        NMO_RETURN_IF_ERROR(st);

        if (version >= 0x16) {
            st = nmo_chunk_write_dword(chunk, c->style_flags);
            NMO_RETURN_IF_ERROR(st);
        }
    }

    return NMO_OK;
}

static nmo_status_t write_parameters(nmo_chunk_t *chunk,
                                     uint32_t version,
                                     const nmo_interface_param_set_t *params)
{
    nmo_status_t st;

    /* Local parameters */
    st = nmo_chunk_write_int(chunk, (int32_t)params->local_count);
    NMO_RETURN_IF_ERROR(st);

    for (size_t i = 0; i < params->local_count; i++) {
        st = nmo_chunk_write_int(chunk, params->locals[i].h_pos);
        NMO_RETURN_IF_ERROR(st);
        st = nmo_chunk_write_int(chunk, params->locals[i].v_pos);
        NMO_RETURN_IF_ERROR(st);
    }
    for (size_t i = 0; i < params->local_count; i++) {
        st = nmo_chunk_write_int(chunk, (int32_t)params->locals[i].style);
        NMO_RETURN_IF_ERROR(st);
    }

    /* Shared parameters */
    st = nmo_chunk_write_int(chunk, (int32_t)params->shared_count);
    NMO_RETURN_IF_ERROR(st);

    for (size_t i = 0; i < params->shared_count; i++) {
        st = nmo_chunk_write_int(chunk, params->shared[i].h_pos);
        NMO_RETURN_IF_ERROR(st);
        st = nmo_chunk_write_int(chunk, params->shared[i].v_pos);
        NMO_RETURN_IF_ERROR(st);
    }
    for (size_t i = 0; i < params->shared_count; i++) {
        st = nmo_chunk_write_int(chunk, (int32_t)params->shared[i].style);
        NMO_RETURN_IF_ERROR(st);
    }
    for (size_t i = 0; i < params->shared_count; i++) {
        if (version >= 0x15) {
            nmo_object_id_t tag = params->shared[i].mapping_field_count == 1
                ? params->shared[i].mapping_tag0 : params->shared[i].source_id;
            st = nmo_chunk_write_object_id(chunk, tag);
            NMO_RETURN_IF_ERROR(st);
        } else {
            /* Legacy 3-field format: ignored_id, source_id, ignored_int */
            nmo_object_id_t tag0 = params->shared[i].mapping_field_count == 3
                ? params->shared[i].mapping_tag0 : 0;
            nmo_object_id_t tag1 = params->shared[i].mapping_field_count == 3
                ? params->shared[i].mapping_tag1 : params->shared[i].source_id;
            int32_t value = params->shared[i].mapping_field_count == 3
                ? params->shared[i].mapping_value : 0;
            st = nmo_chunk_write_object_id(chunk, tag0);
            NMO_RETURN_IF_ERROR(st);
            st = nmo_chunk_write_object_id(chunk, tag1);
            NMO_RETURN_IF_ERROR(st);
            st = nmo_chunk_write_int(chunk, value);
            NMO_RETURN_IF_ERROR(st);
        }
    }

    return NMO_OK;
}

static nmo_status_t write_parameter_section(
    nmo_chunk_t *chunk,
    uint32_t version,
    bool shared,
    const nmo_interface_param_set_t *params)
{
    const nmo_interface_param_t *items = shared ? params->shared : params->locals;
    size_t count = shared ? params->shared_count : params->local_count;
    NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, (int32_t)count));
    for (size_t i = 0; i < count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, items[i].h_pos));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, items[i].v_pos));
    }
    for (size_t i = 0; i < count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, (int32_t)items[i].style));
    }
    if (shared) {
        for (size_t i = 0; i < count; ++i) {
            if (version >= 0x15) {
                nmo_object_id_t tag = items[i].mapping_field_count == 1
                    ? items[i].mapping_tag0 : items[i].source_id;
                NMO_RETURN_IF_ERROR(nmo_chunk_write_object_id(chunk, tag));
            } else {
                nmo_object_id_t tag0 = items[i].mapping_field_count == 3
                    ? items[i].mapping_tag0 : 0;
                nmo_object_id_t tag1 = items[i].mapping_field_count == 3
                    ? items[i].mapping_tag1 : items[i].source_id;
                int32_t value = items[i].mapping_field_count == 3
                    ? items[i].mapping_value : 0;
                NMO_RETURN_IF_ERROR(nmo_chunk_write_object_id(chunk, tag0));
                NMO_RETURN_IF_ERROR(nmo_chunk_write_object_id(chunk, tag1));
                NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, value));
            }
        }
    }
    return NMO_OK;
}

static nmo_status_t write_graph_io(nmo_chunk_t *chunk,
                                   const nmo_interface_graph_io_t *gio)
{
    nmo_status_t st;

    /* 4 arrays, each: count + (value, marker) pairs.
     * Markers: -1 for inputs, +1 for outputs. */
    struct {
        const int32_t *data;
        size_t count;
        const int32_t *tags;
        int32_t default_tag;
    } arrays[4] = {
        { gio->inward_inputs, gio->inward_input_count, gio->inward_input_tags, -1 },
        { gio->outward_inputs, gio->outward_input_count, gio->outward_input_tags, -1 },
        { gio->inward_outputs, gio->inward_output_count, gio->inward_output_tags, 1 },
        { gio->outward_outputs, gio->outward_output_count, gio->outward_output_tags, 1 },
    };

    for (int a = 0; a < 4; a++) {
        st = nmo_chunk_write_int(chunk, (int32_t)arrays[a].count);
        NMO_RETURN_IF_ERROR(st);
        for (size_t i = 0; i < arrays[a].count; i++) {
            st = nmo_chunk_write_int(chunk, arrays[a].data[i]);
            NMO_RETURN_IF_ERROR(st);
            st = nmo_chunk_write_int(chunk, arrays[a].tags
                ? arrays[a].tags[i] : arrays[a].default_tag);
            NMO_RETURN_IF_ERROR(st);
        }
    }

    return NMO_OK;
}

static nmo_status_t write_graph_io_section(
    nmo_chunk_t *chunk,
    bool outputs,
    const nmo_interface_graph_io_t *gio)
{
    const int32_t *values[2];
    const int32_t *tags[2];
    size_t counts[2];
    int32_t default_tag = outputs ? 1 : -1;
    if (outputs) {
        values[0] = gio ? gio->inward_outputs : NULL;
        values[1] = gio ? gio->outward_outputs : NULL;
        tags[0] = gio ? gio->inward_output_tags : NULL;
        tags[1] = gio ? gio->outward_output_tags : NULL;
        counts[0] = gio ? gio->inward_output_count : 0;
        counts[1] = gio ? gio->outward_output_count : 0;
    } else {
        values[0] = gio ? gio->inward_inputs : NULL;
        values[1] = gio ? gio->outward_inputs : NULL;
        tags[0] = gio ? gio->inward_input_tags : NULL;
        tags[1] = gio ? gio->outward_input_tags : NULL;
        counts[0] = gio ? gio->inward_input_count : 0;
        counts[1] = gio ? gio->outward_input_count : 0;
    }
    for (size_t a = 0; a < 2; ++a) {
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, (int32_t)counts[a]));
        for (size_t i = 0; i < counts[a]; ++i) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, values[a][i]));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_int(
                chunk, tags[a] ? tags[a][i] : default_tag));
        }
    }
    return NMO_OK;
}

/* ================================================================
 * Script header writer
 * ================================================================ */

static nmo_status_t write_script_header(
    nmo_chunk_t *chunk,
    const nmo_interface_data_t *data)
{
    nmo_status_t st;
    const nmo_interface_script_header_t *hdr = &data->script;

    st = nmo_chunk_write_object_id(chunk, hdr->behavior_id);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_dword(chunk, hdr->flags);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_dword(chunk, hdr->script_index);
    NMO_RETURN_IF_ERROR(st);

    st = nmo_chunk_write_float(chunk, hdr->h_pos);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_float(chunk, hdr->v_pos);
    NMO_RETURN_IF_ERROR(st);

    st = nmo_chunk_write_float(chunk, hdr->h_start_pos);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_float(chunk, hdr->v_start_pos);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_float(chunk, hdr->v_size);
    NMO_RETURN_IF_ERROR(st);

    /* Bitmap (snapshot): use original codec properties for round-trip fidelity */
    if (hdr->has_snapshot && hdr->snapshot_data) {
        nmo_image_desc_t desc = hdr->snapshot_desc;
        desc.image_data = (uint8_t *)hdr->snapshot_data;
        st = nmo_chunk_write_bitmap_legacy(chunk, &desc, &hdr->snapshot_props);
        NMO_RETURN_IF_ERROR(st);
    } else {
        st = write_empty_legacy_bitmap(chunk);
        NMO_RETURN_IF_ERROR(st);
    }

    /* Dev 2.5 sectioned headers omit color and use the editor default. */
    if (data->version >= 0x14 && !interface_is_sectioned(data)) {
        st = nmo_chunk_write_dword(chunk, hdr->color);
        NMO_RETURN_IF_ERROR(st);
    }

    return NMO_OK;
}

/* ================================================================
 * Sub-behavior header writer
 * ================================================================ */

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
    st = nmo_chunk_write_float(chunk, sub->v_expand_size);
    NMO_RETURN_IF_ERROR(st);

    return NMO_OK;
}

/* ================================================================
 * Body writer
 * ================================================================ */

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
    uint32_t version = data->version;

    if (interface_is_sectioned(data)) {
        /* ---- Sectioned layout (Dev.exe files) ---- */
        bool preserve = (data->format_flags & NMO_INTERFACE_FORMAT_SECTION_PRESENCE) != 0;
#define WRITE_SECTION_IF(_present, _base, _call) \
        do { \
            if (!preserve || (_present)) { \
                NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier( \
                    chunk, behavior_section_id(behavior_index, (_base)))); \
                NMO_RETURN_IF_ERROR((_call)); \
            } \
        } while (0)

        WRITE_SECTION_IF(body->has_links_section, DEV_SECTION_LINKS,
                         write_links(chunk, body, true));
        WRITE_SECTION_IF(body->has_operations_section, DEV_SECTION_OPERATIONS,
                         write_operations(chunk, body));
        WRITE_SECTION_IF(body->has_comments_section, DEV_SECTION_COMMENTS,
                         write_comments(chunk, version, body));
        WRITE_SECTION_IF(body->has_local_params_section, DEV_SECTION_LOCAL_PARAMS,
                         write_parameter_section(chunk, version, false, &body->params));
        WRITE_SECTION_IF(body->has_shared_params_section, DEV_SECTION_SHARED_PARAMS,
                         write_parameter_section(chunk, version, true, &body->params));
        WRITE_SECTION_IF(body->has_graph_inputs_section, DEV_SECTION_GRAPH_INPUTS,
                         write_graph_io_section(chunk, false, body->graph_io));
        WRITE_SECTION_IF(body->has_graph_outputs_section, DEV_SECTION_GRAPH_OUTPUTS,
                         write_graph_io_section(chunk, true, body->graph_io));
        if (!preserve || body->has_unknown_flag_section) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier(
                chunk, behavior_section_id(behavior_index, DEV_SECTION_UNKNOWN_FLAG)));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_int(
                chunk, body->has_unknown_flag_section
                    ? body->unknown_flag : DEV_UNKNOWN_FLAG_VALUE));
        }
#undef WRITE_SECTION_IF

        return NMO_OK;
    }

    /* ---- Inline layout ---- */
    st = write_links(chunk, body, false);
    NMO_RETURN_IF_ERROR(st);

    st = write_operations(chunk, body);
    NMO_RETURN_IF_ERROR(st);

    st = write_comments(chunk, version, body);
    NMO_RETURN_IF_ERROR(st);

    (void)ctx; (void)behavior_id; (void)is_script;

    if (body->has_params) {
        st = write_parameters(chunk, version, &body->params);
        NMO_RETURN_IF_ERROR(st);
    }

    if (body->has_graph_io && body->graph_io) {
        st = write_graph_io(chunk, body->graph_io);
        NMO_RETURN_IF_ERROR(st);
    }

    return NMO_OK;
}

/* ================================================================
 * Extra data writer
 * ================================================================ */

static nmo_status_t write_extra_data(nmo_chunk_t *chunk,
                                     const nmo_interface_extra_t *extra)
{
    nmo_status_t st;

    if (!extra->present) {
        return NMO_OK;
    }

    /* Write identifier */
    uint32_t id;
    switch (extra->version) {
    case 3: id = NMO_INTERFACE_EXTRA_ID_V3; break;
    case 2: id = NMO_INTERFACE_EXTRA_ID_V2; break;
    default: id = NMO_INTERFACE_EXTRA_ID_V1; break;
    }

    /* Write as raw DWORDs (identifier value + next-pointer=0) to avoid
     * corrupting the identifier chain that write_identifier back-patches. */
    st = nmo_chunk_write_dword(chunk, id);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_dword(chunk, 0);
    NMO_RETURN_IF_ERROR(st);

    /* Entry count */
    st = nmo_chunk_write_int(chunk, (int32_t)extra->entry_count);
    NMO_RETURN_IF_ERROR(st);

    for (size_t i = 0; i < extra->entry_count; i++) {
        const nmo_interface_extra_entry_t *entry = &extra->entries[i];

        st = nmo_chunk_write_dword(chunk, entry->type);
        NMO_RETURN_IF_ERROR(st);

        switch (entry->type) {
        case 1: /* fall through */
        case 2:
            st = nmo_chunk_write_object_id(chunk, entry->id1);
            NMO_RETURN_IF_ERROR(st);
            break;
        case 3:
            st = nmo_chunk_write_object_id(chunk, entry->id1);
            NMO_RETURN_IF_ERROR(st);
            st = nmo_chunk_write_object_id(chunk, entry->id2);
            NMO_RETURN_IF_ERROR(st);
            break;
        case 4:
            st = nmo_chunk_write_int(chunk, entry->value);
            NMO_RETURN_IF_ERROR(st);
            break;
        default:
            break;
        }

        /* Sub-entries for version >= 2 */
        if (extra->version >= 2) {
            st = nmo_chunk_write_int(chunk, (int32_t)entry->sub_count);
            NMO_RETURN_IF_ERROR(st);

            for (size_t j = 0; j < entry->sub_count; j++) {
                const nmo_interface_extra_sub_t *sub = &entry->sub_entries[j];

                /* Reverse v2 adjustment: value1 >= 6 -> value1 - 2 */
                int32_t write_value1 = sub->value1;
                if (extra->version == 2 && write_value1 >= 6) {
                    write_value1 -= 2;
                }

                st = nmo_chunk_write_int(chunk, write_value1);
                NMO_RETURN_IF_ERROR(st);
                st = nmo_chunk_write_int(chunk, sub->value2);
                NMO_RETURN_IF_ERROR(st);
                st = nmo_chunk_write_object_id(chunk, sub->id1);
                NMO_RETURN_IF_ERROR(st);

                if (extra_sub_has_id2(sub->value1)) {
                    st = nmo_chunk_write_object_id(chunk, sub->id2);
                    NMO_RETURN_IF_ERROR(st);
                } else {
                    st = nmo_chunk_write_buffer(chunk,
                        sub->data, sub->data_size);
                    NMO_RETURN_IF_ERROR(st);
                }
            }
        }
    }

    return NMO_OK;
}

/* ================================================================
 * Public writer
 * ================================================================ */

static nmo_status_t nmo_interface_chunk_write_internal(
    nmo_chunk_t *chunk,
    const nmo_interface_data_t *data,
    const nmo_interface_parse_ctx_t *ctx)
{
    if (!chunk || !data) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "interface_chunk_write: NULL argument");
    }

    nmo_status_t st = nmo_chunk_start_write(chunk);
    NMO_RETURN_IF_ERROR(st);

    /* Version identifier */
    st = nmo_chunk_write_identifier(chunk,
        interface_is_sectioned(data) ? DEV_SECTION_TOP_MARKER : DEV_LEGACY_TOP_MARKER);
    NMO_RETURN_IF_ERROR(st);
    st = nmo_chunk_write_dword(chunk, data->version);
    NMO_RETURN_IF_ERROR(st);

    /* Script marker for sectioned layout */
    if (interface_is_sectioned(data)) {
        st = nmo_chunk_write_identifier(chunk,
            interface_root_is_graph(data)
                ? DEV_SECTION_GRAPH_MARKER
                : DEV_SECTION_SCRIPT_MARKER);
        NMO_RETURN_IF_ERROR(st);
    }

    /* Total behavior count */
    int32_t total_count = (int32_t)(1 + data->sub_count);
    st = nmo_chunk_write_int(chunk, total_count);
    NMO_RETURN_IF_ERROR(st);

    /* Script header section identifier (sectioned only) */
    if (interface_is_sectioned(data)) {
        st = nmo_chunk_write_identifier(chunk,
            interface_root_is_graph(data)
                ? DEV_SECTION_HEADER
                : DEV_SECTION_SCRIPT_HEADER);
        NMO_RETURN_IF_ERROR(st);
    }

    /* Script header */
    st = write_script_header(chunk, data);
    NMO_RETURN_IF_ERROR(st);

    /* Script body */
    if (data->script.body.has_body) {
        st = write_body(chunk, data, ctx, &data->script.body,
                        data->script.behavior_id, 0, true);
        NMO_RETURN_IF_ERROR(st);
    }

    /* Sub-behaviors */
    for (size_t i = 0; i < data->sub_count; i++) {
        uint32_t layout_index = (uint32_t)(i + 1);

        if (interface_is_sectioned(data)) {
            st = nmo_chunk_write_identifier(chunk,
                DEV_SECTION_HEADER + layout_index);
            NMO_RETURN_IF_ERROR(st);
        }

        st = write_sub_header(chunk, &data->subs[i]);
        NMO_RETURN_IF_ERROR(st);

        if (data->subs[i].body.has_body) {
            st = write_body(chunk, data, ctx, &data->subs[i].body,
                            data->subs[i].behavior_id, layout_index, false);
            NMO_RETURN_IF_ERROR(st);
        }
    }

    /* Extra data */
    st = write_extra_data(chunk, &data->extra);
    NMO_RETURN_IF_ERROR(st);

    nmo_chunk_close(chunk);
    return NMO_OK;
}

nmo_status_t nmo_interface_chunk_write(
    nmo_chunk_t *chunk,
    const nmo_interface_data_t *data,
    const nmo_interface_parse_ctx_t *ctx)
{
    if (!chunk || !data || !chunk->arena) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_chunk_t *staged = nmo_chunk_create(chunk->arena);
    if (!staged) return NMO_ERR_NOMEM;
    staged->class_id = chunk->class_id;
    staged->data_version = chunk->data_version;
    staged->chunk_version = chunk->chunk_version;
    staged->chunk_class_id = chunk->chunk_class_id;
    staged->chunk_options = chunk->chunk_options;
    staged->file_context = chunk->file_context;

    nmo_status_t result = nmo_interface_chunk_write_internal(
        staged, data, ctx);
    if (result != NMO_OK) return result;
    *chunk = *staged;
    return NMO_OK;
}
