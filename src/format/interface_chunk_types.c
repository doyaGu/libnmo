/**
 * @file interface_chunk_types.c
 * @brief Type registrations for interface chunk struct types
 *
 * Registers reflection descriptors for all 14 interface chunk structs
 * so they participate in the GUID-based type system.
 */

#include "format/nmo_interface_chunk.h"
#include "type/nmo_type_system.h"
#include "type/nmo_reflection.h"
#include "core/nmo_error.h"
#include <stdio.h>
#include <string.h>
#include <stdalign.h>

/* ============================================================================
 * to_string helpers — leaf types
 * ============================================================================ */

static nmo_status_t iface_endpoint_to_string(
    const void *value, const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer, size_t buffer_size, int depth)
{
    (void)type; (void)registry; (void)depth;
    if (!value || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "endpoint to_string: bad args");
    }
    const nmo_interface_endpoint_t *e = (const nmo_interface_endpoint_t *)value;
    int n = snprintf(buffer, buffer_size, "%u:%d:%u", e->id, e->index, e->type);
    if (n < 0 || (size_t)n >= buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                         "endpoint to_string: buffer too small");
    }
    NMO_RETURN_OK();
}

static nmo_status_t iface_operation_to_string(
    const void *value, const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer, size_t buffer_size, int depth)
{
    (void)type; (void)registry; (void)depth;
    if (!value || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "operation to_string: bad args");
    }
    const nmo_interface_operation_t *op = (const nmo_interface_operation_t *)value;
    int n = snprintf(buffer, buffer_size, "op#%u (%.1f,%.1f)",
                     op->id, (double)op->h_pos, (double)op->v_pos);
    if (n < 0 || (size_t)n >= buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                         "operation to_string: buffer too small");
    }
    NMO_RETURN_OK();
}

static nmo_status_t iface_comment_to_string(
    const void *value, const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer, size_t buffer_size, int depth)
{
    (void)type; (void)registry; (void)depth;
    if (!value || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "comment to_string: bad args");
    }
    const nmo_interface_comment_t *c = (const nmo_interface_comment_t *)value;
    int n = snprintf(buffer, buffer_size, "rect text=%.32s%s",
                     c->text ? c->text : "(null)",
                     (c->text && strlen(c->text) > 32) ? "..." : "");
    if (n < 0 || (size_t)n >= buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                         "comment to_string: buffer too small");
    }
    NMO_RETURN_OK();
}

static nmo_status_t iface_param_to_string(
    const void *value, const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer, size_t buffer_size, int depth)
{
    (void)type; (void)registry; (void)depth;
    if (!value || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "param to_string: bad args");
    }
    const nmo_interface_param_t *p = (const nmo_interface_param_t *)value;
    int n = snprintf(buffer, buffer_size, "(%d,%d) style=%u",
                     p->h_pos, p->v_pos, p->style);
    if (n < 0 || (size_t)n >= buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                         "param to_string: buffer too small");
    }
    NMO_RETURN_OK();
}

static nmo_status_t iface_param_set_to_string(
    const void *value, const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer, size_t buffer_size, int depth)
{
    (void)type; (void)registry; (void)depth;
    if (!value || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "param_set to_string: bad args");
    }
    const nmo_interface_param_set_t *ps = (const nmo_interface_param_set_t *)value;
    int n = snprintf(buffer, buffer_size, "local=%zu shared=%zu",
                     ps->local_count, ps->shared_count);
    if (n < 0 || (size_t)n >= buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                         "param_set to_string: buffer too small");
    }
    NMO_RETURN_OK();
}

static nmo_status_t iface_graph_io_to_string(
    const void *value, const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer, size_t buffer_size, int depth)
{
    (void)type; (void)registry; (void)depth;
    if (!value || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "graph_io to_string: bad args");
    }
    const nmo_interface_graph_io_t *g = (const nmo_interface_graph_io_t *)value;
    int n = snprintf(buffer, buffer_size, "in:%zu/%zu out:%zu/%zu",
                     g->inward_input_count, g->outward_input_count,
                     g->inward_output_count, g->outward_output_count);
    if (n < 0 || (size_t)n >= buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                         "graph_io to_string: buffer too small");
    }
    NMO_RETURN_OK();
}

static nmo_status_t iface_extra_sub_to_string(
    const void *value, const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer, size_t buffer_size, int depth)
{
    (void)type; (void)registry; (void)depth;
    if (!value || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "extra_sub to_string: bad args");
    }
    const nmo_interface_extra_sub_t *s = (const nmo_interface_extra_sub_t *)value;
    int n = snprintf(buffer, buffer_size, "v1=%d v2=%d id1=%u",
                     s->value1, s->value2, s->id1);
    if (n < 0 || (size_t)n >= buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                         "extra_sub to_string: buffer too small");
    }
    NMO_RETURN_OK();
}

static nmo_status_t iface_extra_entry_to_string(
    const void *value, const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer, size_t buffer_size, int depth)
{
    (void)type; (void)registry; (void)depth;
    if (!value || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "extra_entry to_string: bad args");
    }
    const nmo_interface_extra_entry_t *e = (const nmo_interface_extra_entry_t *)value;
    int n = snprintf(buffer, buffer_size, "type=%u id1=%u subs=%zu",
                     e->type, e->id1, e->sub_count);
    if (n < 0 || (size_t)n >= buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                         "extra_entry to_string: buffer too small");
    }
    NMO_RETURN_OK();
}

static nmo_status_t iface_extra_to_string(
    const void *value, const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer, size_t buffer_size, int depth)
{
    (void)type; (void)registry; (void)depth;
    if (!value || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "extra to_string: bad args");
    }
    const nmo_interface_extra_t *x = (const nmo_interface_extra_t *)value;
    int n;
    if (x->present) {
        n = snprintf(buffer, buffer_size, "v=%u entries=%zu",
                     x->version, x->entry_count);
    } else {
        n = snprintf(buffer, buffer_size, "(none)");
    }
    if (n < 0 || (size_t)n >= buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                         "extra to_string: buffer too small");
    }
    NMO_RETURN_OK();
}

/* ============================================================================
 * to_string helpers — composite types
 * ============================================================================ */

static nmo_status_t iface_link_to_string(
    const void *value, const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer, size_t buffer_size, int depth)
{
    (void)type; (void)registry; (void)depth;
    if (!value || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "link to_string: bad args");
    }
    const nmo_interface_link_t *lk = (const nmo_interface_link_t *)value;
    int n = snprintf(buffer, buffer_size,
                     "#%u type=%u hl=%d %u:%d:%u->%u:%d:%u pts=%zu",
                     lk->link_id, lk->type, (int)lk->highlight,
                     lk->start.id, lk->start.index, lk->start.type,
                     lk->end.id, lk->end.index, lk->end.type,
                     lk->point_count);
    if (n < 0 || (size_t)n >= buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                         "link to_string: buffer too small");
    }
    NMO_RETURN_OK();
}

static nmo_status_t iface_body_to_string(
    const void *value, const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer, size_t buffer_size, int depth)
{
    (void)type; (void)registry; (void)depth;
    if (!value || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "body to_string: bad args");
    }
    const nmo_interface_body_t *b = (const nmo_interface_body_t *)value;
    int n = snprintf(buffer, buffer_size, "links=%zu ops=%zu comments=%zu",
                     b->link_count, b->operation_count, b->comment_count);
    if (n < 0 || (size_t)n >= buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                         "body to_string: buffer too small");
    }
    NMO_RETURN_OK();
}

static nmo_status_t iface_script_hdr_to_string(
    const void *value, const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer, size_t buffer_size, int depth)
{
    (void)type; (void)registry; (void)depth;
    if (!value || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "script_hdr to_string: bad args");
    }
    const nmo_interface_script_header_t *s =
        (const nmo_interface_script_header_t *)value;
    int n = snprintf(buffer, buffer_size,
                     "id=%u flags=0x%X links=%zu ops=%zu",
                     s->behavior_id, s->flags,
                     s->body.link_count, s->body.operation_count);
    if (n < 0 || (size_t)n >= buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                         "script_hdr to_string: buffer too small");
    }
    NMO_RETURN_OK();
}

static nmo_status_t iface_behavior_to_string(
    const void *value, const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer, size_t buffer_size, int depth)
{
    (void)type; (void)registry; (void)depth;
    if (!value || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "behavior to_string: bad args");
    }
    const nmo_interface_behavior_t *bh =
        (const nmo_interface_behavior_t *)value;
    int n = snprintf(buffer, buffer_size,
                     "id=%u depth=%u links=%zu",
                     bh->behavior_id, bh->depth, bh->body.link_count);
    if (n < 0 || (size_t)n >= buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                         "behavior to_string: buffer too small");
    }
    NMO_RETURN_OK();
}

static nmo_status_t iface_data_to_string(
    const void *value, const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer, size_t buffer_size, int depth)
{
    (void)type; (void)registry; (void)depth;
    if (!value || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "data to_string: bad args");
    }
    const nmo_interface_data_t *d = (const nmo_interface_data_t *)value;
    int n = snprintf(buffer, buffer_size, "v0x%02X subs=%zu links=%zu",
                     d->version, d->sub_count,
                     d->script.body.link_count);
    if (n < 0 || (size_t)n >= buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                         "data to_string: buffer too small");
    }
    NMO_RETURN_OK();
}

/* ============================================================================
 * VTables
 * ============================================================================ */

static const nmo_type_vtable_t vtable_endpoint   = { .to_string = iface_endpoint_to_string };
static const nmo_type_vtable_t vtable_link        = { .to_string = iface_link_to_string };
static const nmo_type_vtable_t vtable_operation   = { .to_string = iface_operation_to_string };
static const nmo_type_vtable_t vtable_comment     = { .to_string = iface_comment_to_string };
static const nmo_type_vtable_t vtable_param       = { .to_string = iface_param_to_string };
static const nmo_type_vtable_t vtable_param_set   = { .to_string = iface_param_set_to_string };
static const nmo_type_vtable_t vtable_graph_io    = { .to_string = iface_graph_io_to_string };
static const nmo_type_vtable_t vtable_body        = { .to_string = iface_body_to_string };
static const nmo_type_vtable_t vtable_script_hdr  = { .to_string = iface_script_hdr_to_string };
static const nmo_type_vtable_t vtable_behavior    = { .to_string = iface_behavior_to_string };
static const nmo_type_vtable_t vtable_extra_sub   = { .to_string = iface_extra_sub_to_string };
static const nmo_type_vtable_t vtable_extra_entry = { .to_string = iface_extra_entry_to_string };
static const nmo_type_vtable_t vtable_extra       = { .to_string = iface_extra_to_string };
static const nmo_type_vtable_t vtable_data        = { .to_string = iface_data_to_string };

/* ============================================================================
 * Field descriptors
 * ============================================================================ */

static const nmo_type_field_t fields_endpoint[] = {
    NMO_FIELD(nmo_interface_endpoint_t, id,    CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_endpoint_t, index, CKPGUID_INT),
    NMO_FIELD(nmo_interface_endpoint_t, type,  CKPGUID_UINT32),
};

static const nmo_type_field_t fields_link[] = {
    NMO_FIELD(nmo_interface_link_t, type,        CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_link_t, highlight,   CKPGUID_BOOL),
    NMO_FIELD(nmo_interface_link_t, link_id,     CKPGUID_UINT32),
    NMO_FIELD_NAMED("start",
        offsetof(nmo_interface_link_t, start),
        sizeof(nmo_interface_endpoint_t),
        NMO_GUID_IFACE_ENDPOINT, 0, NMO_SEMANTIC_NONE),
    NMO_FIELD(nmo_interface_link_t, point_count, CKPGUID_UINT32),
    NMO_FIELD_NAMED("end",
        offsetof(nmo_interface_link_t, end),
        sizeof(nmo_interface_endpoint_t),
        NMO_GUID_IFACE_ENDPOINT, 0, NMO_SEMANTIC_NONE),
};

static const nmo_type_field_t fields_operation[] = {
    NMO_FIELD(nmo_interface_operation_t, id,    CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_operation_t, h_pos, CKPGUID_FLOAT),
    NMO_FIELD(nmo_interface_operation_t, v_pos, CKPGUID_FLOAT),
};

static const nmo_type_field_t fields_comment[] = {
    NMO_FIELD(nmo_interface_comment_t, left,        CKPGUID_FLOAT),
    NMO_FIELD(nmo_interface_comment_t, top,         CKPGUID_FLOAT),
    NMO_FIELD(nmo_interface_comment_t, right,       CKPGUID_FLOAT),
    NMO_FIELD(nmo_interface_comment_t, bottom,      CKPGUID_FLOAT),
    NMO_FIELD(nmo_interface_comment_t, text,        CKPGUID_STRING),
    NMO_FIELD(nmo_interface_comment_t, style_flags, CKPGUID_UINT32),
};

static const nmo_type_field_t fields_param[] = {
    NMO_FIELD(nmo_interface_param_t, h_pos,     CKPGUID_INT),
    NMO_FIELD(nmo_interface_param_t, v_pos,     CKPGUID_INT),
    NMO_FIELD(nmo_interface_param_t, style,     CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_param_t, source_id, CKPGUID_UINT32),
};

static const nmo_type_field_t fields_param_set[] = {
    NMO_FIELD(nmo_interface_param_set_t, local_count,  CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_param_set_t, shared_count, CKPGUID_UINT32),
};

static const nmo_type_field_t fields_graph_io[] = {
    NMO_FIELD(nmo_interface_graph_io_t, inward_input_count,   CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_graph_io_t, outward_input_count,  CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_graph_io_t, inward_output_count,  CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_graph_io_t, outward_output_count, CKPGUID_UINT32),
};

static const nmo_type_field_t fields_extra_sub[] = {
    NMO_FIELD(nmo_interface_extra_sub_t, value1,    CKPGUID_INT),
    NMO_FIELD(nmo_interface_extra_sub_t, value2,    CKPGUID_INT),
    NMO_FIELD(nmo_interface_extra_sub_t, id1,       CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_extra_sub_t, id2,       CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_extra_sub_t, data_size, CKPGUID_UINT32),
};

static const nmo_type_field_t fields_extra_entry[] = {
    NMO_FIELD(nmo_interface_extra_entry_t, type,      CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_extra_entry_t, id1,       CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_extra_entry_t, id2,       CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_extra_entry_t, value,     CKPGUID_INT),
    NMO_FIELD(nmo_interface_extra_entry_t, sub_count, CKPGUID_UINT32),
};

static const nmo_type_field_t fields_extra[] = {
    NMO_FIELD(nmo_interface_extra_t, present,     CKPGUID_BOOL),
    NMO_FIELD(nmo_interface_extra_t, version,     CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_extra_t, entry_count, CKPGUID_UINT32),
};

static const nmo_type_field_t fields_body[] = {
    NMO_FIELD(nmo_interface_body_t, has_body,         CKPGUID_BOOL),
    NMO_FIELD(nmo_interface_body_t, link_count,       CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_body_t, operation_count,  CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_body_t, comment_count,    CKPGUID_UINT32),
    NMO_FIELD_PTR_ARRAY(nmo_interface_body_t, links,      link_count,      NMO_GUID_IFACE_LINK),
    NMO_FIELD_PTR_ARRAY(nmo_interface_body_t, operations, operation_count, NMO_GUID_IFACE_OPERATION),
    NMO_FIELD_PTR_ARRAY(nmo_interface_body_t, comments,   comment_count,   NMO_GUID_IFACE_COMMENT),
    NMO_FIELD(nmo_interface_body_t, has_params,       CKPGUID_BOOL),
    NMO_FIELD_NAMED("params",
        offsetof(nmo_interface_body_t, params),
        sizeof(nmo_interface_param_set_t),
        NMO_GUID_IFACE_PARAM_SET, 0, NMO_SEMANTIC_NONE),
    NMO_FIELD(nmo_interface_body_t, has_graph_io,     CKPGUID_BOOL),
    NMO_FIELD_PTR(nmo_interface_body_t, graph_io, NMO_GUID_IFACE_GRAPH_IO),
};

static const nmo_type_field_t fields_script_hdr[] = {
    NMO_FIELD(nmo_interface_script_header_t, behavior_id,  CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_script_header_t, flags,        CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_script_header_t, script_index, CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_script_header_t, h_pos,        CKPGUID_FLOAT),
    NMO_FIELD(nmo_interface_script_header_t, v_pos,        CKPGUID_FLOAT),
    NMO_FIELD(nmo_interface_script_header_t, h_start_pos,  CKPGUID_FLOAT),
    NMO_FIELD(nmo_interface_script_header_t, v_start_pos,  CKPGUID_FLOAT),
    NMO_FIELD(nmo_interface_script_header_t, v_size,       CKPGUID_FLOAT),
    NMO_FIELD(nmo_interface_script_header_t, has_snapshot,  CKPGUID_BOOL),
    NMO_FIELD(nmo_interface_script_header_t, color,        CKPGUID_UINT32),
    NMO_FIELD_NAMED("body",
        offsetof(nmo_interface_script_header_t, body),
        sizeof(nmo_interface_body_t),
        NMO_GUID_IFACE_BODY, 0, NMO_SEMANTIC_NONE),
};

static const nmo_type_field_t fields_behavior[] = {
    NMO_FIELD(nmo_interface_behavior_t, behavior_id,    CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_behavior_t, flags,          CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_behavior_t, depth,          CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_behavior_t, h_pos,          CKPGUID_FLOAT),
    NMO_FIELD(nmo_interface_behavior_t, v_pos,          CKPGUID_FLOAT),
    NMO_FIELD(nmo_interface_behavior_t, h_size,         CKPGUID_FLOAT),
    NMO_FIELD(nmo_interface_behavior_t, v_size,         CKPGUID_FLOAT),
    NMO_FIELD(nmo_interface_behavior_t, h_expand_size,  CKPGUID_FLOAT),
    NMO_FIELD(nmo_interface_behavior_t, v_expand_size,  CKPGUID_FLOAT),
    NMO_FIELD_NAMED("body",
        offsetof(nmo_interface_behavior_t, body),
        sizeof(nmo_interface_body_t),
        NMO_GUID_IFACE_BODY, 0, NMO_SEMANTIC_NONE),
};

static const nmo_type_field_t fields_data[] = {
    NMO_FIELD(nmo_interface_data_t, version,          CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_data_t, format_flags,     CKPGUID_UINT32),
    NMO_FIELD(nmo_interface_data_t, sub_count,        CKPGUID_UINT32),
    NMO_FIELD_NAMED("script",
        offsetof(nmo_interface_data_t, script),
        sizeof(nmo_interface_script_header_t),
        NMO_GUID_IFACE_SCRIPT_HDR, 0, NMO_SEMANTIC_NONE),
    NMO_FIELD_PTR_ARRAY(nmo_interface_data_t, subs, sub_count, NMO_GUID_IFACE_BEHAVIOR),
    NMO_FIELD_NAMED("extra",
        offsetof(nmo_interface_data_t, extra),
        sizeof(nmo_interface_extra_t),
        NMO_GUID_IFACE_EXTRA, 0, NMO_SEMANTIC_NONE),
};

/* ============================================================================
 * Registration
 * ============================================================================ */

nmo_status_t nmo_register_interface_types(nmo_type_registry_t *registry)
{
    if (!registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "nmo_register_interface_types: NULL registry");
    }

    nmo_status_t st;

    /* Leaf types first (no dependencies on other interface types) */

    {
        nmo_type_descriptor_t desc = {
            .guid = NMO_GUID_IFACE_ENDPOINT,
            .name = "iface_endpoint",
            .size = sizeof(nmo_interface_endpoint_t),
            .alignment = alignof(nmo_interface_endpoint_t),
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .flags = NMO_TYPE_FLAG_POD,
            .id = NMO_TYPE_ID_INVALID,
            .fields = fields_endpoint,
            .field_count = sizeof(fields_endpoint) / sizeof(fields_endpoint[0]),
            .vtable = &vtable_endpoint,
        };
        st = nmo_type_registry_register(registry, &desc);
        if (st != NMO_OK) return st;
    }

    {
        nmo_type_descriptor_t desc = {
            .guid = NMO_GUID_IFACE_OPERATION,
            .name = "iface_operation",
            .size = sizeof(nmo_interface_operation_t),
            .alignment = alignof(nmo_interface_operation_t),
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .flags = NMO_TYPE_FLAG_POD,
            .id = NMO_TYPE_ID_INVALID,
            .fields = fields_operation,
            .field_count = sizeof(fields_operation) / sizeof(fields_operation[0]),
            .vtable = &vtable_operation,
        };
        st = nmo_type_registry_register(registry, &desc);
        if (st != NMO_OK) return st;
    }

    {
        nmo_type_descriptor_t desc = {
            .guid = NMO_GUID_IFACE_COMMENT,
            .name = "iface_comment",
            .size = sizeof(nmo_interface_comment_t),
            .alignment = alignof(nmo_interface_comment_t),
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .flags = NMO_TYPE_FLAG_POD,
            .id = NMO_TYPE_ID_INVALID,
            .fields = fields_comment,
            .field_count = sizeof(fields_comment) / sizeof(fields_comment[0]),
            .vtable = &vtable_comment,
        };
        st = nmo_type_registry_register(registry, &desc);
        if (st != NMO_OK) return st;
    }

    {
        nmo_type_descriptor_t desc = {
            .guid = NMO_GUID_IFACE_PARAM,
            .name = "iface_param",
            .size = sizeof(nmo_interface_param_t),
            .alignment = alignof(nmo_interface_param_t),
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .flags = NMO_TYPE_FLAG_POD,
            .id = NMO_TYPE_ID_INVALID,
            .fields = fields_param,
            .field_count = sizeof(fields_param) / sizeof(fields_param[0]),
            .vtable = &vtable_param,
        };
        st = nmo_type_registry_register(registry, &desc);
        if (st != NMO_OK) return st;
    }

    {
        nmo_type_descriptor_t desc = {
            .guid = NMO_GUID_IFACE_PARAM_SET,
            .name = "iface_param_set",
            .size = sizeof(nmo_interface_param_set_t),
            .alignment = alignof(nmo_interface_param_set_t),
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .flags = NMO_TYPE_FLAG_POD,
            .id = NMO_TYPE_ID_INVALID,
            .fields = fields_param_set,
            .field_count = sizeof(fields_param_set) / sizeof(fields_param_set[0]),
            .vtable = &vtable_param_set,
        };
        st = nmo_type_registry_register(registry, &desc);
        if (st != NMO_OK) return st;
    }

    {
        nmo_type_descriptor_t desc = {
            .guid = NMO_GUID_IFACE_GRAPH_IO,
            .name = "iface_graph_io",
            .size = sizeof(nmo_interface_graph_io_t),
            .alignment = alignof(nmo_interface_graph_io_t),
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .flags = NMO_TYPE_FLAG_POD,
            .id = NMO_TYPE_ID_INVALID,
            .fields = fields_graph_io,
            .field_count = sizeof(fields_graph_io) / sizeof(fields_graph_io[0]),
            .vtable = &vtable_graph_io,
        };
        st = nmo_type_registry_register(registry, &desc);
        if (st != NMO_OK) return st;
    }

    {
        nmo_type_descriptor_t desc = {
            .guid = NMO_GUID_IFACE_EXTRA_SUB,
            .name = "iface_extra_sub",
            .size = sizeof(nmo_interface_extra_sub_t),
            .alignment = alignof(nmo_interface_extra_sub_t),
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .flags = NMO_TYPE_FLAG_POD,
            .id = NMO_TYPE_ID_INVALID,
            .fields = fields_extra_sub,
            .field_count = sizeof(fields_extra_sub) / sizeof(fields_extra_sub[0]),
            .vtable = &vtable_extra_sub,
        };
        st = nmo_type_registry_register(registry, &desc);
        if (st != NMO_OK) return st;
    }

    {
        nmo_type_descriptor_t desc = {
            .guid = NMO_GUID_IFACE_EXTRA_ENTRY,
            .name = "iface_extra_entry",
            .size = sizeof(nmo_interface_extra_entry_t),
            .alignment = alignof(nmo_interface_extra_entry_t),
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .flags = NMO_TYPE_FLAG_POD,
            .id = NMO_TYPE_ID_INVALID,
            .fields = fields_extra_entry,
            .field_count = sizeof(fields_extra_entry) / sizeof(fields_extra_entry[0]),
            .vtable = &vtable_extra_entry,
        };
        st = nmo_type_registry_register(registry, &desc);
        if (st != NMO_OK) return st;
    }

    {
        nmo_type_descriptor_t desc = {
            .guid = NMO_GUID_IFACE_EXTRA,
            .name = "iface_extra",
            .size = sizeof(nmo_interface_extra_t),
            .alignment = alignof(nmo_interface_extra_t),
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .flags = NMO_TYPE_FLAG_POD,
            .id = NMO_TYPE_ID_INVALID,
            .fields = fields_extra,
            .field_count = sizeof(fields_extra) / sizeof(fields_extra[0]),
            .vtable = &vtable_extra,
        };
        st = nmo_type_registry_register(registry, &desc);
        if (st != NMO_OK) return st;
    }

    /* Composite types (depend on leaf types above) */

    {
        nmo_type_descriptor_t desc = {
            .guid = NMO_GUID_IFACE_LINK,
            .name = "iface_link",
            .size = sizeof(nmo_interface_link_t),
            .alignment = alignof(nmo_interface_link_t),
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .flags = NMO_TYPE_FLAG_POD,
            .id = NMO_TYPE_ID_INVALID,
            .fields = fields_link,
            .field_count = sizeof(fields_link) / sizeof(fields_link[0]),
            .vtable = &vtable_link,
        };
        st = nmo_type_registry_register(registry, &desc);
        if (st != NMO_OK) return st;
    }

    {
        nmo_type_descriptor_t desc = {
            .guid = NMO_GUID_IFACE_BODY,
            .name = "iface_body",
            .size = sizeof(nmo_interface_body_t),
            .alignment = alignof(nmo_interface_body_t),
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .flags = NMO_TYPE_FLAG_POD,
            .id = NMO_TYPE_ID_INVALID,
            .fields = fields_body,
            .field_count = sizeof(fields_body) / sizeof(fields_body[0]),
            .vtable = &vtable_body,
        };
        st = nmo_type_registry_register(registry, &desc);
        if (st != NMO_OK) return st;
    }

    {
        nmo_type_descriptor_t desc = {
            .guid = NMO_GUID_IFACE_SCRIPT_HDR,
            .name = "iface_script_header",
            .size = sizeof(nmo_interface_script_header_t),
            .alignment = alignof(nmo_interface_script_header_t),
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .flags = NMO_TYPE_FLAG_POD,
            .id = NMO_TYPE_ID_INVALID,
            .fields = fields_script_hdr,
            .field_count = sizeof(fields_script_hdr) / sizeof(fields_script_hdr[0]),
            .vtable = &vtable_script_hdr,
        };
        st = nmo_type_registry_register(registry, &desc);
        if (st != NMO_OK) return st;
    }

    {
        nmo_type_descriptor_t desc = {
            .guid = NMO_GUID_IFACE_BEHAVIOR,
            .name = "iface_behavior",
            .size = sizeof(nmo_interface_behavior_t),
            .alignment = alignof(nmo_interface_behavior_t),
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .flags = NMO_TYPE_FLAG_POD,
            .id = NMO_TYPE_ID_INVALID,
            .fields = fields_behavior,
            .field_count = sizeof(fields_behavior) / sizeof(fields_behavior[0]),
            .vtable = &vtable_behavior,
        };
        st = nmo_type_registry_register(registry, &desc);
        if (st != NMO_OK) return st;
    }

    {
        nmo_type_descriptor_t desc = {
            .guid = NMO_GUID_IFACE_DATA,
            .name = "iface_data",
            .size = sizeof(nmo_interface_data_t),
            .alignment = alignof(nmo_interface_data_t),
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .flags = NMO_TYPE_FLAG_POD,
            .id = NMO_TYPE_ID_INVALID,
            .fields = fields_data,
            .field_count = sizeof(fields_data) / sizeof(fields_data[0]),
            .vtable = &vtable_data,
        };
        st = nmo_type_registry_register(registry, &desc);
        if (st != NMO_OK) return st;
    }

    NMO_RETURN_OK();
}
