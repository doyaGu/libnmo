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

#define DEV_SECTION_HEADER           0x01000000u
#define DEV_SECTION_OPERATIONS       0x02000000u
#define DEV_SECTION_LINKS            0x03000000u
#define DEV_SECTION_COMMENTS         0x08000000u
#define DEV_SECTION_UNKNOWN_FLAG     0x0A000000u
#define DEV_SECTION_SCRIPT_HEADER    0x07000000u

/* ================================================================
 * Internal helpers - forward declarations
 * ================================================================ */

static nmo_status_t parse_script_header(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    uint32_t version,
    bool use_sectioned,
    nmo_interface_script_header_t *out);

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

static nmo_status_t parse_graph_io(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
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

static uint32_t behavior_section_id(size_t behavior_index, uint32_t base)
{
    return (uint32_t)behavior_index + base;
}

/* use_dev_interface_layout removed — layout is auto-detected via
 * identifier probing.  use_sectioned is passed down explicitly. */

/* ================================================================
 * Public API
 * ================================================================ */

nmo_status_t nmo_interface_chunk_parse(
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

    /* Read version: the interface sub-chunk's data area contains an
     * inline identifier chain. Match reference: SeekIdentifier(0xB0000001),
     * then SeekIdentifier(1). Both are tried; last successful one wins. */
    uint32_t version = 0;
    if (nmo_chunk_seek_identifier(chunk, 0xB0000001u) == NMO_OK) {
        st = nmo_chunk_read_dword(chunk, &version);
        NMO_RETURN_IF_ERROR(st);
    }
    if (nmo_chunk_seek_identifier(chunk, 1u) == NMO_OK) {
        st = nmo_chunk_read_dword(chunk, &version);
        NMO_RETURN_IF_ERROR(st);
    }

    if (version < NMO_INTERFACE_VERSION_MIN || version > NMO_INTERFACE_VERSION_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "interface chunk version 0x%08X out of range "
                         "[0x%02X, 0x%02X]",
                         version, NMO_INTERFACE_VERSION_MIN,
                         NMO_INTERFACE_VERSION_MAX);
    }

    /* Detect sectioned layout: Dev.exe writes script-type identifiers
     * (2 = script, 3 = non-script) before the behavior count.  Standard
     * Virtools files only have version identifiers (1 / 0xB0000001).
     * If we find identifier 2 or 3, the file uses sectioned layout. */
    bool use_sectioned = false;
    bool found = false;
    st = optional_seek_identifier(chunk, 2u, &found);
    NMO_RETURN_IF_ERROR(st);
    if (found) {
        use_sectioned = true;
    } else {
        st = optional_seek_identifier(chunk, 3u, &found);
        NMO_RETURN_IF_ERROR(st);
        if (found) use_sectioned = true;
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
    out->sub_count = (total_count > 1) ? (size_t)(total_count - 1) : 0;

    /* Seek to script header section (sectioned layout only).
     * For inline layout the header follows sequentially. */
    if (use_sectioned) {
        (void)optional_seek_identifier(chunk,
            DEV_SECTION_SCRIPT_HEADER, &found);
    }
    st = parse_script_header(chunk, arena, version, use_sectioned,
                             &out->script);
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
                (void)optional_seek_identifier(chunk,
                    DEV_SECTION_HEADER + layout_index, &found);
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

/* ================================================================
 * Script header
 * ================================================================ */

static nmo_status_t parse_script_header(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    uint32_t version,
    bool use_sectioned,
    nmo_interface_script_header_t *out)
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
    memset(&bitmap_desc, 0, sizeof(bitmap_desc));

    st = nmo_chunk_read_bitmap_legacy(chunk, &bitmap_desc, &bitmap_pixels);
    NMO_RETURN_IF_ERROR(st);

    if (bitmap_pixels && bitmap_desc.width > 0 && bitmap_desc.height > 0) {
        out->snapshot_data = bitmap_pixels;
        out->snapshot_size = (size_t)bitmap_desc.width * (size_t)bitmap_desc.height * 4;
    } else {
        out->snapshot_data = NULL;
        out->snapshot_size = 0;
    }

    if (version >= 0x14 && !use_sectioned) {
        st = nmo_chunk_read_dword(chunk, &out->color);
        NMO_RETURN_IF_ERROR(st);
    } else {
        /* Dev.exe 2.5 LoadInterfaceData does not consume a color dword on this
         * v0x12+ path after CKStateChunk::ReadBitmap. The UI header color is
         * initialized from editor defaults instead. */
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
         * Each section is behind its own SeekIdentifier.
         * Params and graph IO live in separate section IDs
         * (0x04/0x05/0x06/0x09) that we don't parse yet. */
        bool found = false;

        st = optional_seek_identifier(chunk,
            behavior_section_id(behavior_index, DEV_SECTION_LINKS), &found);
        NMO_RETURN_IF_ERROR(st);
        if (found) {
            st = parse_links(chunk, arena, out, true);
            NMO_RETURN_IF_ERROR(st);
        }

        st = optional_seek_identifier(chunk,
            behavior_section_id(behavior_index, DEV_SECTION_OPERATIONS), &found);
        NMO_RETURN_IF_ERROR(st);
        if (found) {
            st = parse_operations(chunk, arena, out);
            NMO_RETURN_IF_ERROR(st);
        }

        st = optional_seek_identifier(chunk,
            behavior_section_id(behavior_index, DEV_SECTION_COMMENTS), &found);
        NMO_RETURN_IF_ERROR(st);
        if (found) {
            st = parse_comments(chunk, arena, version, out);
            NMO_RETURN_IF_ERROR(st);
        }

        memset(&out->params, 0, sizeof(out->params));
        out->graph_io = NULL;

        st = optional_seek_identifier(chunk,
            behavior_section_id(behavior_index, DEV_SECTION_UNKNOWN_FLAG),
            &found);
        NMO_RETURN_IF_ERROR(st);
        if (found) {
            int32_t ignored = 0;
            st = nmo_chunk_read_int(chunk, &ignored);
            NMO_RETURN_IF_ERROR(st);
        }

        return NMO_OK;
    }

    /* ---- Inline layout (standard Virtools files) ----
     * Body sections are sequential: links, ops, comments, params, graph IO. */

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
        st = parse_parameters(chunk, arena, version, &out->params);
        NMO_RETURN_IF_ERROR(st);
    } else {
        memset(&out->params, 0, sizeof(out->params));
    }

    out->graph_io = NULL;
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

        /* text (arena-allocated by chunk reader; returns size_t, 0 on empty/error) */
        char *text = NULL;
        nmo_chunk_read_string(chunk, &text);
        c->text = text;

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
    size_t *out_count)
{
    nmo_status_t st;
    int32_t count = 0;
    st = nmo_chunk_read_int(chunk, &count);
    NMO_RETURN_IF_ERROR(st);

    if (count < 0 || count > 100000) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "interface chunk: graph IO array count %d out of range", count);
    }
    *out_count = (size_t)count;
    if (count == 0) { *out_array = NULL; return NMO_OK; }

    *out_array = nmo_arena_alloc(arena, (size_t)count * sizeof(int32_t), alignof(int32_t));
    if (!*out_array) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "interface chunk: cannot allocate graph IO array");
    }
    for (int32_t i = 0; i < count; i++) {
        st = nmo_chunk_read_int(chunk, &(*out_array)[i]);
        NMO_RETURN_IF_ERROR(st);
        int32_t ignored = 0;
        st = nmo_chunk_read_int(chunk, &ignored);
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
    st = parse_graph_io_array(chunk, arena, &out->inward_inputs, &out->inward_input_count);
    NMO_RETURN_IF_ERROR(st);
    st = parse_graph_io_array(chunk, arena, &out->outward_inputs, &out->outward_input_count);
    NMO_RETURN_IF_ERROR(st);
    st = parse_graph_io_array(chunk, arena, &out->inward_outputs, &out->inward_output_count);
    NMO_RETURN_IF_ERROR(st);
    st = parse_graph_io_array(chunk, arena, &out->outward_outputs, &out->outward_output_count);
    NMO_RETURN_IF_ERROR(st);
    return NMO_OK;
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
