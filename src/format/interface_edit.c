/**
 * @file interface_edit.c
 * @brief Lookup and mutation helpers for nmo_interface_data_t
 */

#include "format/nmo_interface_edit.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"

#include <string.h>

/* ================================================================
 * Lookups
 * ================================================================ */

nmo_interface_behavior_t *nmo_interface_find_sub(
    nmo_interface_data_t *data,
    nmo_object_id_t behavior_id)
{
    if (!data || behavior_id == 0) return NULL;
    for (size_t i = 0; i < data->sub_count; i++) {
        if (data->subs[i].behavior_id == behavior_id)
            return &data->subs[i];
    }
    return NULL;
}

nmo_interface_body_t *nmo_interface_find_body(
    nmo_interface_data_t *data,
    nmo_object_id_t behavior_id)
{
    if (!data || behavior_id == 0) return NULL;
    if (data->script.behavior_id == behavior_id)
        return data->script.body.has_body ? &data->script.body : NULL;
    for (size_t i = 0; i < data->sub_count; i++) {
        if (data->subs[i].behavior_id == behavior_id)
            return data->subs[i].body.has_body ? &data->subs[i].body : NULL;
    }
    return NULL;
}

static nmo_interface_link_t *find_link_in_body(
    nmo_interface_body_t *body,
    nmo_object_id_t link_id)
{
    for (size_t i = 0; i < body->link_count; i++) {
        if (body->links[i].link_id == link_id)
            return &body->links[i];
    }
    return NULL;
}

nmo_interface_link_t *nmo_interface_find_link(
    nmo_interface_data_t *data,
    nmo_object_id_t link_id)
{
    if (!data || link_id == 0) return NULL;
    nmo_interface_link_t *found;
    if (data->script.body.has_body) {
        found = find_link_in_body(&data->script.body, link_id);
        if (found) return found;
    }
    for (size_t i = 0; i < data->sub_count; i++) {
        if (!data->subs[i].body.has_body) continue;
        found = find_link_in_body(&data->subs[i].body, link_id);
        if (found) return found;
    }
    return NULL;
}

nmo_interface_link_t *nmo_interface_body_find_link(
    nmo_interface_body_t *body,
    nmo_object_id_t link_id)
{
    if (!body || link_id == 0) return NULL;
    return find_link_in_body(body, link_id);
}

static nmo_interface_operation_t *find_operation_in_body(
    nmo_interface_body_t *body,
    nmo_object_id_t op_id)
{
    for (size_t i = 0; i < body->operation_count; i++) {
        if (body->operations[i].id == op_id)
            return &body->operations[i];
    }
    return NULL;
}

nmo_interface_operation_t *nmo_interface_find_operation(
    nmo_interface_data_t *data,
    nmo_object_id_t op_id)
{
    if (!data || op_id == 0) return NULL;
    nmo_interface_operation_t *found;
    if (data->script.body.has_body) {
        found = find_operation_in_body(&data->script.body, op_id);
        if (found) return found;
    }
    for (size_t i = 0; i < data->sub_count; i++) {
        if (!data->subs[i].body.has_body) continue;
        found = find_operation_in_body(&data->subs[i].body, op_id);
        if (found) return found;
    }
    return NULL;
}

nmo_interface_operation_t *nmo_interface_body_find_operation(
    nmo_interface_body_t *body,
    nmo_object_id_t op_id)
{
    if (!body || op_id == 0) return NULL;
    return find_operation_in_body(body, op_id);
}

/* ================================================================
 * Comment mutations
 * ================================================================ */

nmo_status_t nmo_interface_body_add_comment(
    nmo_interface_body_t *body,
    nmo_arena_t *arena,
    const char *text,
    float left, float top, float right, float bottom,
    uint32_t style_flags,
    size_t *out_index)
{
    if (!body || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "interface edit: add_comment NULL argument");
    }
    if (!body->has_body) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "interface edit: add_comment on body with has_body=false");
    }

    size_t new_count = body->comment_count + 1;
    nmo_interface_comment_t *new_arr = (nmo_interface_comment_t *)nmo_arena_alloc(
        arena, new_count * sizeof(nmo_interface_comment_t),
        alignof(nmo_interface_comment_t));
    if (!new_arr) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "interface edit: cannot allocate comments array");
    }
    if (body->comment_count > 0 && body->comments) {
        memcpy(new_arr, body->comments,
               body->comment_count * sizeof(nmo_interface_comment_t));
    }

    nmo_interface_comment_t *c = &new_arr[body->comment_count];
    c->left = left;
    c->top = top;
    c->right = right;
    c->bottom = bottom;
    if (text) {
        c->text = nmo_arena_strdup(arena, text);
        if (!c->text) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "interface edit: cannot duplicate comment text");
        }
    } else {
        c->text = NULL;
    }
    c->style_flags = style_flags;

    body->comments = new_arr;
    if (out_index) *out_index = body->comment_count;
    body->comment_count = new_count;

    /* Maintain section flag for sectioned layout */
    body->has_comments_section = true;

    return NMO_OK;
}

nmo_status_t nmo_interface_body_remove_comment(
    nmo_interface_body_t *body,
    size_t index)
{
    if (!body) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "interface edit: remove_comment NULL body");
    }
    if (!body->has_body) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "interface edit: remove_comment on body with has_body=false");
    }
    if (index >= body->comment_count) {
        NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR,
                         "interface edit: comment index %zu >= count %zu",
                         index, body->comment_count);
    }

    if (index + 1 < body->comment_count) {
        memmove(&body->comments[index], &body->comments[index + 1],
                (body->comment_count - index - 1) * sizeof(nmo_interface_comment_t));
    }
    body->comment_count--;
    if (body->comment_count == 0) {
        body->comments = NULL;
    }
    return NMO_OK;
}

nmo_status_t nmo_interface_body_set_comment_text(
    nmo_interface_body_t *body,
    nmo_arena_t *arena,
    size_t index,
    const char *text)
{
    if (!body || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "interface edit: set_comment_text NULL argument");
    }
    if (!body->has_body) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "interface edit: set_comment_text on body with has_body=false");
    }
    if (index >= body->comment_count) {
        NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR,
                         "interface edit: comment index %zu >= count %zu",
                         index, body->comment_count);
    }

    if (text) {
        const char *dup = nmo_arena_strdup(arena, text);
        if (!dup) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "interface edit: cannot duplicate comment text");
        }
        body->comments[index].text = dup;
    } else {
        body->comments[index].text = NULL;
    }
    return NMO_OK;
}

/* ================================================================
 * Link routing point mutations
 * ================================================================ */

nmo_status_t nmo_interface_link_add_point(
    nmo_interface_link_t *link,
    nmo_arena_t *arena,
    float h, float v)
{
    if (!link || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "interface edit: add_point NULL argument");
    }

    size_t new_count = link->point_count + 1;
    float *new_pts = (float *)nmo_arena_alloc(
        arena, new_count * 2 * sizeof(float), alignof(float));
    if (!new_pts) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "interface edit: cannot allocate points array");
    }
    if (link->point_count > 0 && link->points) {
        memcpy(new_pts, link->points, link->point_count * 2 * sizeof(float));
    }
    new_pts[link->point_count * 2]     = h;
    new_pts[link->point_count * 2 + 1] = v;

    link->points = new_pts;
    link->point_count = new_count;
    return NMO_OK;
}

void nmo_interface_link_clear_points(
    nmo_interface_link_t *link)
{
    if (!link) return;
    link->point_count = 0;
    link->points = NULL;
}

nmo_status_t nmo_interface_link_remove_point(
    nmo_interface_link_t *link,
    size_t index)
{
    if (!link) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "interface edit: remove_point NULL link");
    }
    if (index >= link->point_count) {
        NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR,
                         "interface edit: remove_point index out of range");
    }

    size_t remaining = link->point_count - index - 1;
    if (remaining > 0) {
        memmove(&link->points[index * 2],
                &link->points[(index + 1) * 2],
                remaining * 2 * sizeof(float));
    }
    link->point_count--;
    if (link->point_count == 0) {
        link->points = NULL;
    }
    return NMO_OK;
}

nmo_status_t nmo_interface_graph_io_set_array(
    int32_t **array_ptr,
    size_t *count_ptr,
    nmo_arena_t *arena,
    const int32_t *values,
    size_t count)
{
    if (!array_ptr || !count_ptr || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "interface edit: graph_io_set_array NULL argument");
    }

    if (count == 0) {
        *array_ptr = NULL;
        *count_ptr = 0;
        return NMO_OK;
    }

    if (!values) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "interface edit: graph_io_set_array NULL values with count > 0");
    }

    int32_t *new_arr = (int32_t *)nmo_arena_alloc(
        arena, count * sizeof(int32_t), _Alignof(int32_t));
    if (!new_arr) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "interface edit: cannot allocate graph IO array");
    }
    memcpy(new_arr, values, count * sizeof(int32_t));

    *array_ptr = new_arr;
    *count_ptr = count;
    return NMO_OK;
}
