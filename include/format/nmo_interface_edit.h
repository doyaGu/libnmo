/**
 * @file nmo_interface_edit.h
 * @brief Lookup and mutation helpers for nmo_interface_data_t
 *
 * Lookup functions navigate the heterogeneous script/sub-behavior tree
 * by object ID. Mutation functions handle arena-allocated array growth
 * and removal for comments and link routing points.
 *
 * Simple field writes (position, color, fold, style, highlight) are
 * done directly on the public struct fields — see the spec for examples.
 *
 * Version-gated features: the writer silently skips fields unsupported
 * by data->version (e.g., color requires v>=0x14 inline, comment
 * style_flags requires v>=0x16). This API does not enforce version
 * checks — callers should verify data->version before setting gated
 * fields.
 */

#ifndef NMO_INTERFACE_EDIT_H
#define NMO_INTERFACE_EDIT_H

#include "format/nmo_interface_chunk.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * Lookups
 * ================================================================ */

/**
 * @brief Find sub-behavior by behavior_id.
 * @return Pointer to sub-behavior, or NULL if not found.
 */
NMO_API nmo_interface_behavior_t *nmo_interface_find_sub(
    nmo_interface_data_t *data,
    nmo_object_id_t behavior_id);

/**
 * @brief Find body for a given behavior_id.
 *
 * Checks script header first, then sub-behaviors.
 * Returns NULL if not found or if has_body is false.
 */
NMO_API nmo_interface_body_t *nmo_interface_find_body(
    nmo_interface_data_t *data,
    nmo_object_id_t behavior_id);

/**
 * @brief Find link by link_id across all bodies (script + all subs).
 * Skips bodies where has_body is false.
 * @return Pointer to link, or NULL if not found.
 */
NMO_API nmo_interface_link_t *nmo_interface_find_link(
    nmo_interface_data_t *data,
    nmo_object_id_t link_id);

/**
 * @brief Find link by link_id within a single body.
 * @return Pointer to link, or NULL if not found.
 */
NMO_API nmo_interface_link_t *nmo_interface_body_find_link(
    nmo_interface_body_t *body,
    nmo_object_id_t link_id);

/**
 * @brief Find operation by ID across all bodies (script + all subs).
 * Skips bodies where has_body is false.
 * @return Pointer to operation, or NULL if not found.
 */
NMO_API nmo_interface_operation_t *nmo_interface_find_operation(
    nmo_interface_data_t *data,
    nmo_object_id_t op_id);

/**
 * @brief Find operation by ID within a single body.
 * @return Pointer to operation, or NULL if not found.
 */
NMO_API nmo_interface_operation_t *nmo_interface_body_find_operation(
    nmo_interface_body_t *body,
    nmo_object_id_t op_id);

/* ================================================================
 * Comment mutations
 * ================================================================ */

/**
 * @brief Add a comment to a body.
 *
 * Text is duplicated into the arena (NULL stored as-is, written as empty).
 * Grows the comments array (old array abandoned in arena).
 * Sets has_comments_section=true for sectioned layout bodies.
 *
 * @param body         Target body (returns NMO_ERR_INVALID_STATE if has_body is false)
 * @param arena        Arena for allocations
 * @param text         Comment text (NULL allowed)
 * @param left,top,right,bottom  Comment rectangle
 * @param style_flags  Style flags (only written for version >= 0x16)
 * @param out_index    Output: index of new comment (may be NULL)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_interface_body_add_comment(
    nmo_interface_body_t *body,
    nmo_arena_t *arena,
    const char *text,
    float left, float top, float right, float bottom,
    uint32_t style_flags,
    size_t *out_index);

/**
 * @brief Remove comment at index. Shifts remaining elements.
 *
 * When the last comment is removed, sets comments=NULL and comment_count=0.
 *
 * @param body   Target body (returns NMO_ERR_INVALID_STATE if has_body is false)
 * @param index  Comment index (must be < comment_count)
 * @return NMO_OK on success, NMO_ERR_OUT_OF_BOUNDS if index invalid
 */
NMO_API nmo_status_t nmo_interface_body_remove_comment(
    nmo_interface_body_t *body,
    size_t index);

/**
 * @brief Replace comment text at index.
 *
 * New text duplicated into arena. Old text pointer abandoned.
 *
 * @param body   Target body (returns NMO_ERR_INVALID_STATE if has_body is false)
 * @param arena  Arena for string allocation
 * @param index  Comment index (must be < comment_count)
 * @param text   New text (NULL allowed)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_interface_body_set_comment_text(
    nmo_interface_body_t *body,
    nmo_arena_t *arena,
    size_t index,
    const char *text);

/* ================================================================
 * Link routing point mutations
 * ================================================================ */

/**
 * @brief Append a routing point to a link.
 *
 * Grows the points array (old array abandoned in arena).
 *
 * @param link   Target link
 * @param arena  Arena for array allocation
 * @param h,v    Routing point coordinates
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_interface_link_add_point(
    nmo_interface_link_t *link,
    nmo_arena_t *arena,
    float h, float v);

/**
 * @brief Remove all routing points from a link.
 * Sets point_count=0 and points=NULL.
 */
NMO_API void nmo_interface_link_clear_points(
    nmo_interface_link_t *link);

#ifdef __cplusplus
}
#endif

#endif /* NMO_INTERFACE_EDIT_H */
