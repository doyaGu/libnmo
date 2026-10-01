/**
 * @file interface_copy.c
 * @brief Deep copy of the parsed interface data of a behavior
 */

#include "format/nmo_interface_chunk.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"

#include <stdalign.h>
#include <string.h>

static nmo_status_t nmo_interface_copy_array(
    nmo_arena_t *arena,
    void **dst,
    const void *src,
    size_t elem_size,
    size_t count,
    const char *label)
{
    if (!dst || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid interface copy arguments");
    }
    *dst = NULL;
    if (count == 0) {
        NMO_RETURN_OK();
    }
    if (!src) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing %s data for interface copy", label);
    }
    if (elem_size != 0 && count > ((size_t)-1) / elem_size) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Interface copy size overflow for %s", label);
    }
    void *copy = nmo_arena_alloc(arena, elem_size * count, NMO_MAX_ALIGN);
    if (!copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Cannot allocate %s for interface copy", label);
    }
    memcpy(copy, src, elem_size * count);
    *dst = copy;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_interface_copy_bytes(
    nmo_arena_t *arena,
    void **dst,
    const void *src,
    size_t size,
    const char *label)
{
    return nmo_interface_copy_array(arena, dst, src, sizeof(uint8_t), size, label);
}

static nmo_status_t nmo_interface_copy_string(
    nmo_arena_t *arena,
    const char **dst,
    const char *src)
{
    if (!dst) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid interface string copy output");
    }
    *dst = NULL;
    if (!src) {
        NMO_RETURN_OK();
    }
    void *copy = NULL;
    NMO_RETURN_IF_ERROR(nmo_interface_copy_bytes(
        arena, &copy, src, strlen(src) + 1u, "comment text"));
    *dst = (const char *)copy;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_interface_copy_graph_io(
    nmo_arena_t *arena,
    nmo_interface_graph_io_t **dst,
    const nmo_interface_graph_io_t *src)
{
    if (!dst) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid graph IO copy output");
    }
    *dst = NULL;
    if (!src) {
        NMO_RETURN_OK();
    }

    nmo_interface_graph_io_t *copy =
        (nmo_interface_graph_io_t *)nmo_arena_alloc(
            arena, sizeof(*copy), alignof(nmo_interface_graph_io_t));
    if (!copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Cannot allocate graph IO copy");
    }
    *copy = *src;
    copy->inward_inputs = NULL;
    copy->outward_inputs = NULL;
    copy->inward_outputs = NULL;
    copy->outward_outputs = NULL;
    copy->inward_input_tags = NULL;
    copy->outward_input_tags = NULL;
    copy->inward_output_tags = NULL;
    copy->outward_output_tags = NULL;

    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->inward_inputs, src->inward_inputs,
        sizeof(int32_t), src->inward_input_count, "graph inward inputs"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->outward_inputs, src->outward_inputs,
        sizeof(int32_t), src->outward_input_count, "graph outward inputs"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->inward_outputs, src->inward_outputs,
        sizeof(int32_t), src->inward_output_count, "graph inward outputs"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->outward_outputs, src->outward_outputs,
        sizeof(int32_t), src->outward_output_count, "graph outward outputs"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->inward_input_tags, src->inward_input_tags,
        sizeof(int32_t), src->inward_input_count, "graph inward input tags"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->outward_input_tags, src->outward_input_tags,
        sizeof(int32_t), src->outward_input_count, "graph outward input tags"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->inward_output_tags, src->inward_output_tags,
        sizeof(int32_t), src->inward_output_count, "graph inward output tags"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->outward_output_tags, src->outward_output_tags,
        sizeof(int32_t), src->outward_output_count, "graph outward output tags"));

    *dst = copy;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_interface_copy_body(
    nmo_arena_t *arena,
    nmo_interface_body_t *dst,
    const nmo_interface_body_t *src)
{
    if (!dst || !src) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid interface body copy arguments");
    }
    *dst = *src;
    dst->links = NULL;
    dst->operations = NULL;
    dst->comments = NULL;
    dst->params.locals = NULL;
    dst->params.shared = NULL;
    dst->graph_io = NULL;

    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&dst->links, src->links, sizeof(nmo_interface_link_t),
        src->link_count, "interface links"));
    for (size_t i = 0; i < dst->link_count; ++i) {
        if (src->links[i].point_count > SIZE_MAX / 2u) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Interface link point count overflow");
        }
        dst->links[i].points = NULL;
        NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
            arena, (void **)&dst->links[i].points, src->links[i].points,
            sizeof(float), src->links[i].point_count * 2u,
            "interface link points"));
    }

    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&dst->operations, src->operations,
        sizeof(nmo_interface_operation_t), src->operation_count,
        "interface operations"));

    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&dst->comments, src->comments,
        sizeof(nmo_interface_comment_t), src->comment_count,
        "interface comments"));
    for (size_t i = 0; i < dst->comment_count; ++i) {
        dst->comments[i].text = NULL;
        NMO_RETURN_IF_ERROR(nmo_interface_copy_string(
            arena, &dst->comments[i].text, src->comments[i].text));
    }

    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&dst->params.locals, src->params.locals,
        sizeof(nmo_interface_param_t), src->params.local_count,
        "interface local params"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&dst->params.shared, src->params.shared,
        sizeof(nmo_interface_param_t), src->params.shared_count,
        "interface shared params"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_graph_io(
        arena, &dst->graph_io, src->graph_io));

    NMO_RETURN_OK();
}

nmo_status_t nmo_interface_data_copy(
    nmo_arena_t *arena,
    nmo_interface_data_t **dst,
    const nmo_interface_data_t *src)
{
    if (!dst) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid interface data copy output");
    }
    *dst = NULL;
    if (!src) {
        NMO_RETURN_OK();
    }

    nmo_interface_data_t *copy =
        (nmo_interface_data_t *)nmo_arena_alloc(
            arena, sizeof(*copy), alignof(nmo_interface_data_t));
    if (!copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Cannot allocate interface data copy");
    }
    *copy = *src;
    copy->script.snapshot_data = NULL;
    copy->script.body = (nmo_interface_body_t){0};
    copy->subs = NULL;
    copy->extra.entries = NULL;

    NMO_RETURN_IF_ERROR(nmo_interface_copy_bytes(
        arena, &copy->script.snapshot_data, src->script.snapshot_data,
        src->script.snapshot_size, "script snapshot"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_body(
        arena, &copy->script.body, &src->script.body));

    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->subs, src->subs,
        sizeof(nmo_interface_behavior_t), src->sub_count,
        "interface sub behaviors"));
    for (size_t i = 0; i < copy->sub_count; ++i) {
        copy->subs[i].body = (nmo_interface_body_t){0};
        NMO_RETURN_IF_ERROR(nmo_interface_copy_body(
            arena, &copy->subs[i].body, &src->subs[i].body));
    }

    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->extra.entries, src->extra.entries,
        sizeof(nmo_interface_extra_entry_t), src->extra.entry_count,
        "interface extra entries"));
    for (size_t i = 0; i < copy->extra.entry_count; ++i) {
        nmo_interface_extra_entry_t *dst_entry = &copy->extra.entries[i];
        const nmo_interface_extra_entry_t *src_entry = &src->extra.entries[i];
        dst_entry->sub_entries = NULL;
        NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
            arena, (void **)&dst_entry->sub_entries, src_entry->sub_entries,
            sizeof(nmo_interface_extra_sub_t), src_entry->sub_count,
            "interface extra sub entries"));
        for (size_t j = 0; j < dst_entry->sub_count; ++j) {
            dst_entry->sub_entries[j].data = NULL;
            NMO_RETURN_IF_ERROR(nmo_interface_copy_bytes(
                arena, &dst_entry->sub_entries[j].data,
                src_entry->sub_entries[j].data,
                src_entry->sub_entries[j].data_size,
                "interface extra sub data"));
        }
    }

    *dst = copy;
    NMO_RETURN_OK();
}
