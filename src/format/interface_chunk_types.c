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

/* Registration order matters: a type must follow the types its fields refer to. */
#define IFACE_TYPE(GUID, NAME, CTYPE, FIELDS, VTABLE) \
    { \
        .guid = NMO_GUID_##GUID##_INIT, \
        .id = NMO_TYPE_ID_INVALID, \
        .category = NMO_TYPE_CATEGORY_STRUCT, \
        .flags = NMO_TYPE_FLAG_POD, \
        .name = (NAME), \
        .size = sizeof(CTYPE), \
        .alignment = alignof(CTYPE), \
        .fields = (FIELDS), \
        .field_count = sizeof(FIELDS) / sizeof((FIELDS)[0]), \
        .vtable = &(VTABLE), \
    }

static const nmo_type_descriptor_t interface_types[] = {
    /* Leaf types first (no dependencies on other interface types) */
    IFACE_TYPE(IFACE_ENDPOINT, "iface_endpoint", nmo_interface_endpoint_t, fields_endpoint, vtable_endpoint),
    IFACE_TYPE(IFACE_OPERATION, "iface_operation", nmo_interface_operation_t, fields_operation, vtable_operation),
    IFACE_TYPE(IFACE_COMMENT, "iface_comment", nmo_interface_comment_t, fields_comment, vtable_comment),
    IFACE_TYPE(IFACE_PARAM, "iface_param", nmo_interface_param_t, fields_param, vtable_param),
    IFACE_TYPE(IFACE_PARAM_SET, "iface_param_set", nmo_interface_param_set_t, fields_param_set, vtable_param_set),
    IFACE_TYPE(IFACE_GRAPH_IO, "iface_graph_io", nmo_interface_graph_io_t, fields_graph_io, vtable_graph_io),
    IFACE_TYPE(IFACE_EXTRA_SUB, "iface_extra_sub", nmo_interface_extra_sub_t, fields_extra_sub, vtable_extra_sub),
    IFACE_TYPE(IFACE_EXTRA_ENTRY, "iface_extra_entry", nmo_interface_extra_entry_t, fields_extra_entry, vtable_extra_entry),
    IFACE_TYPE(IFACE_EXTRA, "iface_extra", nmo_interface_extra_t, fields_extra, vtable_extra),

    /* Composite types (depend on leaf types above) */
    IFACE_TYPE(IFACE_LINK, "iface_link", nmo_interface_link_t, fields_link, vtable_link),
    IFACE_TYPE(IFACE_BODY, "iface_body", nmo_interface_body_t, fields_body, vtable_body),
    IFACE_TYPE(IFACE_SCRIPT_HDR, "iface_script_header", nmo_interface_script_header_t, fields_script_hdr, vtable_script_hdr),
    IFACE_TYPE(IFACE_BEHAVIOR, "iface_behavior", nmo_interface_behavior_t, fields_behavior, vtable_behavior),
    IFACE_TYPE(IFACE_DATA, "iface_data", nmo_interface_data_t, fields_data, vtable_data),
};

nmo_status_t nmo_register_interface_types(nmo_type_registry_t *registry)
{
    if (!registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "nmo_register_interface_types: NULL registry");
    }

    for (size_t i = 0; i < sizeof(interface_types) / sizeof(interface_types[0]); ++i) {
        NMO_RETURN_IF_ERROR(nmo_type_registry_register(registry, &interface_types[i]));
    }

    NMO_RETURN_OK();
}
