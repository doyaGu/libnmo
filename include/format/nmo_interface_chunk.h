/**
 * @file nmo_interface_chunk.h
 * @brief CKBehavior interface chunk parser for editor UI metadata
 *
 * Parses the binary interface chunk that stores behavior graph layout:
 * positions, sizes, links, operations, comments, parameters, and
 * extra editor metadata.
 */

#ifndef NMO_INTERFACE_CHUNK_H
#define NMO_INTERFACE_CHUNK_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "format/nmo_image.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk_t;

/* ================================================================
 * Constants
 * ================================================================ */

/* Endpoint types */
#define NMO_INTERFACE_ENDPOINT_POUT_SHORTCUT  5
#define NMO_INTERFACE_ENDPOINT_PIN            7
#define NMO_INTERFACE_ENDPOINT_POUT           8
#define NMO_INTERFACE_ENDPOINT_PLOCAL         9
#define NMO_INTERFACE_ENDPOINT_TARGET_PIN     10
#define NMO_INTERFACE_ENDPOINT_BIN            12
#define NMO_INTERFACE_ENDPOINT_BOUT           13
#define NMO_INTERFACE_ENDPOINT_START_BIN      26

/* Link types (low 16 bits of raw type field) */
#define NMO_INTERFACE_LINK_BEHAVIOR           1
#define NMO_INTERFACE_LINK_PARAMETER          2
#define NMO_INTERFACE_LINK_HIGHLIGHT_FLAG     0x10000u

/* Parameter display styles */
#define NMO_INTERFACE_PARAM_STYLE_NAME        0x200u
#define NMO_INTERFACE_PARAM_STYLE_COLLAPSED   0x400u
#define NMO_INTERFACE_PARAM_STYLE_NAMEVALUE   0x1000u
#define NMO_INTERFACE_PARAM_STYLE_VALUE       0x2000u

/* Comment style flags (v >= 0x16) */
#define NMO_INTERFACE_COMMENT_COLLAPSED       0x1u
#define NMO_INTERFACE_COMMENT_LOCKED          0x2u
#define NMO_INTERFACE_COMMENT_TRANSPARENT     0x4u

/* Behavior header flags */
#define NMO_INTERFACE_FLAG_FOLDED             0x200u
#define NMO_INTERFACE_FLAG_HEADER_ONLY        0x8000u

/* Extra data identifiers (searched in order: v3, v2, v1) */
#define NMO_INTERFACE_EXTRA_ID_V1             0xA12312F5u
#define NMO_INTERFACE_EXTRA_ID_V2             0xA12312F6u
#define NMO_INTERFACE_EXTRA_ID_V3             0xA12312F7u

/* Version range */
#define NMO_INTERFACE_VERSION_MIN             0x12u
#define NMO_INTERFACE_VERSION_MAX             0x16u

/* ================================================================
 * Data structures
 * ================================================================ */

/* --- Endpoint (link start/end) --- */

typedef struct nmo_interface_endpoint {
    nmo_object_id_t id;
    int32_t index;
    uint32_t type;                          /* NMO_INTERFACE_ENDPOINT_* */
} nmo_interface_endpoint_t;

/* --- Link --- */

typedef struct nmo_interface_link {
    uint32_t type;                          /* raw & 0xFFFF: 1=behavior, 2=parameter */
    bool highlight;                         /* raw & 0x10000 */
    nmo_object_id_t link_id;
    nmo_interface_endpoint_t start;
    size_t point_count;
    float *points;                          /* [point_count * 2] h,v pairs */
    nmo_interface_endpoint_t end;
} nmo_interface_link_t;

/* --- Operation position --- */

typedef struct nmo_interface_operation {
    nmo_object_id_t id;
    float h_pos, v_pos;
} nmo_interface_operation_t;

/* --- Comment --- */

typedef struct nmo_interface_comment {
    float left, top, right, bottom;
    const char *text;                       /* arena-allocated */
    uint32_t style_flags;                   /* v >= 0x16, else 0 */
} nmo_interface_comment_t;

/* --- Parameters (local + shared) --- */

typedef struct nmo_interface_param {
    int32_t h_pos, v_pos;                   /* grid indices, not pixels */
    uint32_t style;                         /* NMO_INTERFACE_PARAM_STYLE_* */
    nmo_object_id_t source_id;              /* shared only; 0 for locals */
} nmo_interface_param_t;

typedef struct nmo_interface_param_set {
    nmo_interface_param_t *locals;
    size_t local_count;
    nmo_interface_param_t *shared;
    size_t shared_count;
} nmo_interface_param_set_t;

/* --- Graph IO (port ordering for graph behaviors) --- */

typedef struct nmo_interface_graph_io {
    int32_t *inward_inputs;        size_t inward_input_count;
    int32_t *outward_inputs;       size_t outward_input_count;
    int32_t *inward_outputs;       size_t inward_output_count;
    int32_t *outward_outputs;      size_t outward_output_count;
} nmo_interface_graph_io_t;

/* --- Body (shared between script and sub-behaviors) --- */

typedef struct nmo_interface_body {
    bool has_body;                          /* false if flags & 0x8000 */

    nmo_interface_link_t *links;
    size_t link_count;

    nmo_interface_operation_t *operations;
    size_t operation_count;

    nmo_interface_comment_t *comments;
    size_t comment_count;

    nmo_interface_param_set_t params;       /* empty for building blocks */
    bool has_params;                        /* false for BBs in inline mode */

    nmo_interface_graph_io_t *graph_io;     /* non-BB, non-script only */
    bool has_graph_io;                      /* true when graph_io was present */

    /* Section presence flags (sectioned layout only).
     * Distinguish absent section from present-but-empty. */
    bool has_links_section;
    bool has_operations_section;
    bool has_comments_section;
    bool has_unknown_flag_section;
    int32_t unknown_flag;
} nmo_interface_body_t;

/* --- Script header (root behavior, entry 0) --- */

typedef struct nmo_interface_script_header {
    nmo_object_id_t behavior_id;
    uint32_t flags;
    uint32_t script_index;
    float h_pos, v_pos;                     /* behavior rect position */
    float h_start_pos, v_start_pos;         /* script start position */
    float v_size;
    nmo_image_desc_t snapshot_desc;         /* decoded bitmap descriptor */
    void *snapshot_data;                    /* decoded pixels, NULL for empty */
    size_t snapshot_size;                   /* byte size of snapshot_data */
    bool has_snapshot;                      /* true when snapshot_desc has image data */
    uint32_t color;                         /* v >= 0x14, else 0 */
    nmo_interface_body_t body;              /* empty if flags & 0x8000 */
} nmo_interface_script_header_t;

/* --- Sub-behavior --- */

typedef struct nmo_interface_behavior {
    nmo_object_id_t behavior_id;
    uint32_t flags;
    uint32_t depth;
    float h_pos, v_pos;
    float h_size, v_size;
    float h_expand_size, v_expand_size;
    nmo_interface_body_t body;              /* empty if flags & 0x8000 */
} nmo_interface_behavior_t;

/* --- Extra data sub-entry --- */

typedef struct nmo_interface_extra_sub {
    int32_t value1;
    int32_t value2;
    nmo_object_id_t id1;
    nmo_object_id_t id2;                    /* when value1 in {2,3,8,9,10,11} */
    void *data;                             /* otherwise: raw buffer */
    size_t data_size;
} nmo_interface_extra_sub_t;

/* --- Extra data entry --- */

typedef struct nmo_interface_extra_entry {
    uint32_t type;                          /* 1-4 */
    nmo_object_id_t id1;
    nmo_object_id_t id2;                    /* type 3 only */
    int32_t value;                          /* type 4 only */
    nmo_interface_extra_sub_t *sub_entries;  /* version >= 2 */
    size_t sub_count;
} nmo_interface_extra_entry_t;

/* --- Extra data section --- */

typedef struct nmo_interface_extra {
    bool present;
    uint32_t version;                       /* 1, 2, or 3 */
    nmo_interface_extra_entry_t *entries;
    size_t entry_count;
} nmo_interface_extra_t;

/* --- Top level --- */

typedef struct nmo_interface_data {
    uint32_t version;                       /* 0x12-0x16 */
    bool sectioned_layout;                  /* true = Dev.exe sectioned, false = inline */
    nmo_interface_script_header_t script;   /* root behavior (entry 0) */
    size_t sub_count;                       /* total_count - 1 */
    nmo_interface_behavior_t *subs;         /* [sub_count] */
    nmo_interface_extra_t extra;            /* extra data section */
} nmo_interface_data_t;

/* ================================================================
 * Parse context
 * ================================================================ */

/**
 * @brief Parse context providing per-behavior metadata.
 *
 * Required for graph IO parsing. May be NULL; graph IO sections
 * will be skipped.
 */
typedef struct nmo_interface_parse_ctx {
    bool (*is_building_block)(nmo_object_id_t id, void *user_data);
    void *user_data;
    /* Layout (inline vs sectioned) is auto-detected from the identifier
     * chain — no caller flag needed. */
} nmo_interface_parse_ctx_t;

/* ================================================================
 * Public API
 * ================================================================ */

/**
 * @brief Parse interface chunk binary data into structured form.
 *
 * All allocations from arena.
 * Rejects version < 0x12 or > 0x16 with NMO_ERR_INVALID_FORMAT.
 *
 * @param chunk     Sub-chunk containing interface data
 * @param arena     Arena for allocations
 * @param ctx       Parse context (may be NULL)
 * @param out       Output structure
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_status_t nmo_interface_chunk_parse(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    const nmo_interface_parse_ctx_t *ctx,
    nmo_interface_data_t *out);

/**
 * @brief Serialize structured interface data into an InterfaceChunk.
 *
 * Writes from nmo_interface_data_t only.  This function must not copy a
 * previously loaded raw InterfaceChunk as its implementation.
 *
 * @param chunk  Target chunk to write
 * @param data   Structured InterfaceChunk data
 * @param ctx    Context for building-block decisions in inline layouts
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_status_t nmo_interface_chunk_write(
    nmo_chunk_t *chunk,
    const nmo_interface_data_t *data,
    const nmo_interface_parse_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* NMO_INTERFACE_CHUNK_H */
