#include "nmo_dsl_eval.h"
#include "dsl/nmo_dsl.h"
#include "dsl/nmo_dsl_ast.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_string.h"
#include "type/nmo_operation_system.h"
#include "core/nmo_guid.h"
#include "core/nmo_array.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Shared helpers (also used by nmo_dsl_seq.c via nmo_dsl_eval.h)
 * ============================================================================ */

static char *eval_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

static const nmo_type_descriptor_t *lookup_field_type(
    const nmo_type_registry_t *registry, nmo_guid_t field_guid)
{
    if (!registry) return NULL;
    return nmo_type_registry_find_by_guid(registry, field_guid);
}

static void set_err(nmo_dsl_eval_state_t *ev, const char *msg) {
    if (ev && msg) {
        (void)snprintf(ev->err, sizeof(ev->err), "%s", msg);
    }
}

/* ============================================================================
 * Value coercion (exact match with nmo_query.c)
 * ============================================================================ */

static bool value_to_number(const nmo_dsl_value_t *v, double *out) {
    if (!v || !out) return false;
    switch (v->kind) {
        case NMO_DSL_VALUE_INT: *out = (double)v->as.i; return true;
        case NMO_DSL_VALUE_UINT: *out = (double)v->as.u; return true;
        case NMO_DSL_VALUE_REAL: *out = v->as.r; return true;
        case NMO_DSL_VALUE_BOOL: *out = v->as.b ? 1.0 : 0.0; return true;
        case NMO_DSL_VALUE_BYREF: {
            if (!v->as.byref.ptr) return false;
            nmo_guid_t g = v->as.byref.type ? v->as.byref.type->guid : v->as.byref.guid;
            size_t size_bytes = v->as.byref.size;
            if (size_bytes == 0 && v->as.byref.type) {
                size_bytes = v->as.byref.type->size;
            }

            if (nmo_guid_equals(g, CKPGUID_BOOL)) {
                if (size_bytes < sizeof(uint8_t)) return false;
                *out = (*(const uint8_t *)v->as.byref.ptr != 0) ? 1.0 : 0.0;
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_INT8)) {
                if (size_bytes < sizeof(int8_t)) return false;
                *out = (double)*(const int8_t *)v->as.byref.ptr;
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_UINT8)) {
                if (size_bytes < sizeof(uint8_t)) return false;
                *out = (double)*(const uint8_t *)v->as.byref.ptr;
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_INT16)) {
                if (size_bytes < sizeof(int16_t)) return false;
                *out = (double)*(const int16_t *)v->as.byref.ptr;
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_UINT16)) {
                if (size_bytes < sizeof(uint16_t)) return false;
                *out = (double)*(const uint16_t *)v->as.byref.ptr;
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_INT)) {
                if (size_bytes < sizeof(int32_t)) return false;
                *out = (double)*(const int32_t *)v->as.byref.ptr;
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_UINT32)) {
                if (size_bytes < sizeof(uint32_t)) return false;
                *out = (double)*(const uint32_t *)v->as.byref.ptr;
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_INT64)) {
                if (size_bytes < sizeof(int64_t)) return false;
                *out = (double)*(const int64_t *)v->as.byref.ptr;
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_UINT64)) {
                if (size_bytes < sizeof(uint64_t)) return false;
                *out = (double)*(const uint64_t *)v->as.byref.ptr;
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_FLOAT)) {
                if (size_bytes < sizeof(float)) return false;
                *out = (double)*(const float *)v->as.byref.ptr;
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_DOUBLE)) {
                if (size_bytes < sizeof(double)) return false;
                *out = *(const double *)v->as.byref.ptr;
                return true;
            }

            return false;
        }
        default: return false;
    }
}

static bool value_to_bool(const nmo_dsl_value_t *v, bool *out) {
    if (!v || !out) return false;
    switch (v->kind) {
        case NMO_DSL_VALUE_BOOL: *out = v->as.b; return true;
        case NMO_DSL_VALUE_INT: *out = (v->as.i != 0); return true;
        case NMO_DSL_VALUE_UINT: *out = (v->as.u != 0); return true;
        case NMO_DSL_VALUE_REAL: *out = (v->as.r != 0.0); return true;
        case NMO_DSL_VALUE_BYREF: {
            double n = 0.0;
            if (!value_to_number(v, &n)) return false;
            *out = (n != 0.0);
            return true;
        }
        case NMO_DSL_VALUE_NULL: *out = false; return true;
        default: return false;
    }
}

/* ============================================================================
 * Sequence factory declarations (from nmo_dsl_seq.c)
 * ============================================================================ */

extern nmo_dsl_seq_t *nmo_dsl_seq_array_create(
    const nmo_type_descriptor_t *owner_type, const void *owner_instance,
    const void *array_ptr, uint64_t count, size_t elem_size,
    nmo_guid_t elem_guid, const nmo_type_descriptor_t *elem_type);

extern nmo_dsl_seq_t *nmo_dsl_seq_map_field_create(
    nmo_dsl_seq_t *src, const char *field, const nmo_type_registry_t *registry);

extern nmo_dsl_seq_t *nmo_dsl_seq_filter_create(
    nmo_dsl_seq_t *src, const nmo_dsl_expr_t *pred, nmo_dsl_eval_state_t *ev);

extern nmo_dsl_seq_t *nmo_dsl_seq_slice_create(
    nmo_dsl_seq_t *src, uint64_t start, uint64_t end);

/* ============================================================================
 * Repeated field helpers
 * ============================================================================ */

static bool resolve_repeated_field_view(
    const nmo_dsl_eval_context_t *ctx,
    const nmo_type_descriptor_t *owner_type,
    const void *owner_instance,
    const nmo_type_field_t *field,
    const void *field_ptr,
    const void **out_array_ptr,
    uint64_t *out_count,
    size_t *out_elem_size,
    nmo_guid_t *out_elem_guid,
    const nmo_type_descriptor_t **out_elem_type,
    nmo_dsl_eval_state_t *ev)
{
    if (!ctx || !field || !field_ptr || !out_array_ptr || !out_count ||
        !out_elem_size || !out_elem_guid || !out_elem_type) {
        return false;
    }

    const nmo_struct_descriptor_t *struct_field = NULL;
    if (ctx->registry && owner_type && field->name) {
        struct_field = nmo_type_get_struct_field_by_name(ctx->registry, owner_type, field->name);
    }

    nmo_guid_t elem_guid = field->type_guid;
    if (struct_field &&
        nmo_guid_equals(struct_field->type_guid, NMO_TYPE_GUID_POINTER) &&
        !nmo_guid_is_null(struct_field->pointee_guid)) {
        elem_guid = struct_field->pointee_guid;
    }

    const nmo_type_descriptor_t *elem_type = lookup_field_type(ctx->registry, elem_guid);
    size_t elem_size = 0;
    if (elem_type && elem_type->size > 0) {
        elem_size = (size_t)elem_type->size;
    } else if (nmo_guid_equals(elem_guid, CKPGUID_BOOL) ||
               nmo_guid_equals(elem_guid, CKPGUID_INT8) ||
               nmo_guid_equals(elem_guid, CKPGUID_UINT8)) {
        elem_size = 1;
    } else if (nmo_guid_equals(elem_guid, CKPGUID_INT16) ||
               nmo_guid_equals(elem_guid, CKPGUID_UINT16)) {
        elem_size = 2;
    } else if (nmo_guid_equals(elem_guid, CKPGUID_INT) ||
               nmo_guid_equals(elem_guid, CKPGUID_UINT32) ||
               nmo_guid_equals(elem_guid, CKPGUID_FLOAT) ||
               nmo_guid_equals(elem_guid, CKPGUID_ID)) {
        elem_size = 4;
    } else if (nmo_guid_equals(elem_guid, CKPGUID_INT64) ||
               nmo_guid_equals(elem_guid, CKPGUID_UINT64) ||
               nmo_guid_equals(elem_guid, CKPGUID_DOUBLE) ||
               nmo_guid_equals(elem_guid, CKPGUID_GUID)) {
        elem_size = 8;
    } else if (nmo_guid_equals(elem_guid, CKPGUID_2DVECTOR)) {
        elem_size = sizeof(float) * 2u;
    } else if (nmo_guid_equals(elem_guid, CKPGUID_VECTOR)) {
        elem_size = sizeof(float) * 3u;
    } else if (nmo_guid_equals(elem_guid, CKPGUID_VECTOR4) ||
               nmo_guid_equals(elem_guid, CKPGUID_QUATERNION) ||
               nmo_guid_equals(elem_guid, CKPGUID_COLOR) ||
               nmo_guid_equals(elem_guid, CKPGUID_RECT)) {
        elem_size = sizeof(float) * 4u;
    }
    if (elem_size == 0) {
        set_err(ev, "array element size unknown");
        return false;
    }

    if (field->size == sizeof(nmo_array_t)) {
        const nmo_array_t *array = (const nmo_array_t *)field_ptr;
        size_t array_elem_size = array->element_size ? array->element_size : elem_size;

        *out_array_ptr = array->data;
        *out_count = (uint64_t)array->count;
        *out_elem_size = array_elem_size;
        *out_elem_guid = elem_guid;
        *out_elem_type = elem_type;
        return true;
    }

    uint64_t callback_count = 0;
    if (ctx->guess_array_count) {
        callback_count = ctx->guess_array_count(
            owner_type, owner_instance, field, ctx->guess_array_count_user);
    }

    bool pointer_storage = true;
    uint64_t fixed_count = 0;
    if (struct_field && struct_field->array_count > 0) {
        pointer_storage = false;
        fixed_count = (uint64_t)struct_field->array_count;
    } else if (field->size != sizeof(void *)) {
        pointer_storage = false;
        if (field->size >= elem_size) {
            fixed_count = (uint64_t)(field->size / elem_size);
        }
    }

    const void *array_ptr = NULL;
    uint64_t count = 0;
    if (pointer_storage) {
        array_ptr = *(const void *const *)field_ptr;
        count = callback_count;
    } else {
        array_ptr = field_ptr;
        count = (callback_count > 0) ? callback_count : fixed_count;
    }

    if (!pointer_storage && !array_ptr) {
        set_err(ev, "inline array pointer is null");
        return false;
    }

    *out_array_ptr = array_ptr;
    *out_count = count;
    *out_elem_size = elem_size;
    *out_elem_guid = elem_guid;
    *out_elem_type = elem_type;
    return true;
}

/* ============================================================================
 * Create array sequence from field
 * ============================================================================ */

static bool create_array_seq(
    const nmo_dsl_eval_context_t *ctx,
    const nmo_type_descriptor_t *owner_type,
    const void *owner_instance,
    const nmo_type_field_t *f,
    const void *ptr,
    nmo_dsl_value_t *out,
    nmo_dsl_eval_state_t *ev)
{
    const void *array_ptr = NULL;
    uint64_t count = 0;
    size_t elem_size = 0;
    nmo_guid_t elem_guid = NMO_GUID_NULL;
    const nmo_type_descriptor_t *elem_type = NULL;
    if (!resolve_repeated_field_view(
            ctx, owner_type, owner_instance, f, ptr,
            &array_ptr, &count, &elem_size, &elem_guid, &elem_type, ev)) {
        return false;
    }

    nmo_dsl_seq_t *seq = nmo_dsl_seq_array_create(
        owner_type, owner_instance, array_ptr, count, elem_size, elem_guid, elem_type);
    if (!seq) { set_err(ev, "oom"); return false; }
    out->kind = NMO_DSL_VALUE_SEQ;
    out->as.seq = seq;
    return true;
}

/* ============================================================================
 * Identifier resolution (exact match with nmo_query.c)
 * ============================================================================ */

static bool resolve_ident(nmo_dsl_eval_state_t *ev, const char *name, nmo_dsl_value_t *out) {
    const nmo_dsl_eval_context_t *ctx = ev->ctx;
    if (!ctx || !name || !out) return false;

    /* @ */
    if (strcmp(name, "@") == 0) {
        if (ctx->current_type && ctx->current_instance) {
            out->kind = NMO_DSL_VALUE_OBJECT;
            out->as.object.type = ctx->current_type;
            out->as.object.instance = ctx->current_instance;
            return true;
        }
        set_err(ev, "@ only valid in filter context");
        return false;
    }

    /* current element fields */
    if (ctx->current_type && ctx->current_instance && nmo_type_has_reflection(ctx->current_type)) {
        const nmo_type_field_t *f = nmo_type_get_field_by_name(ctx->current_type, name);
        if (f) {
            const void *ptr = nmo_field_get_ptr_const(ctx->current_instance, f);
            if (!ptr) { out->kind = NMO_DSL_VALUE_NULL; return true; }
            if (f->flags & NMO_FIELD_REPEATED) {
                return create_array_seq(ctx, ctx->current_type, ctx->current_instance, f, ptr, out, ev);
            }
            out->kind = NMO_DSL_VALUE_BYREF;
            out->as.byref.guid = f->type_guid;
            out->as.byref.type = lookup_field_type(ctx->registry, f->type_guid);
            out->as.byref.ptr = ptr;
            out->as.byref.size = f->size;
            return true;
        }
    }

    /* root fields */
    if (!ctx->root_type || !ctx->root_instance || !nmo_type_has_reflection(ctx->root_type)) {
        set_err(ev, "no reflection");
        return false;
    }

    const nmo_type_field_t *f = nmo_type_get_field_by_name(ctx->root_type, name);
    if (!f) { set_err(ev, "field not found"); return false; }

    const void *ptr = nmo_field_get_ptr_const(ctx->root_instance, f);
    if (!ptr) { out->kind = NMO_DSL_VALUE_NULL; return true; }

    if (f->flags & NMO_FIELD_REPEATED) {
        return create_array_seq(ctx, ctx->root_type, ctx->root_instance, f, ptr, out, ev);
    }

    out->kind = NMO_DSL_VALUE_BYREF;
    out->as.byref.guid = f->type_guid;
    out->as.byref.type = lookup_field_type(ctx->registry, f->type_guid);
    out->as.byref.ptr = ptr;
    out->as.byref.size = f->size;
    return true;
}

/* ============================================================================
 * Member access
 * ============================================================================ */

static bool eval_member(nmo_dsl_eval_state_t *ev, const nmo_dsl_value_t *base,
                         const char *field, nmo_dsl_value_t *out) {
    const nmo_dsl_eval_context_t *ctx = ev->ctx;

    if (base->kind == NMO_DSL_VALUE_SEQ) {
        nmo_dsl_seq_t *map = nmo_dsl_seq_map_field_create(base->as.seq, field, ctx->registry);
        if (!map) { set_err(ev, "oom"); return false; }
        out->kind = NMO_DSL_VALUE_SEQ;
        out->as.seq = map;
        return true;
    }

    const nmo_type_descriptor_t *type = NULL;
    const void *instance = NULL;

    if (base->kind == NMO_DSL_VALUE_OBJECT) {
        type = base->as.object.type;
        instance = base->as.object.instance;
    } else if (base->kind == NMO_DSL_VALUE_BYREF && base->as.byref.type && nmo_type_has_reflection(base->as.byref.type)) {
        type = base->as.byref.type;
        instance = base->as.byref.ptr;
    }

    if (!type || !instance || !nmo_type_has_reflection(type)) {
        set_err(ev, "cannot traverse");
        return false;
    }

    const nmo_type_field_t *f = nmo_type_get_field_by_name(type, field);
    if (!f) { set_err(ev, "field not found"); return false; }

    const void *ptr = nmo_field_get_ptr_const(instance, f);
    if (!ptr) { out->kind = NMO_DSL_VALUE_NULL; return true; }

    if (f->flags & NMO_FIELD_REPEATED) {
        return create_array_seq(ctx, type, instance, f, ptr, out, ev);
    }

    out->kind = NMO_DSL_VALUE_BYREF;
    out->as.byref.guid = f->type_guid;
    out->as.byref.type = lookup_field_type(ctx->registry, f->type_guid);
    out->as.byref.ptr = ptr;
    out->as.byref.size = f->size;
    return true;
}

/* ============================================================================
 * Binary operations
 * ============================================================================ */

static bool eval_binary_numeric(nmo_dsl_tok_kind_t op, const nmo_dsl_value_t *a,
                                 const nmo_dsl_value_t *b, nmo_dsl_value_t *out) {
    double x = 0.0, y = 0.0;
    if (!value_to_number(a, &x) || !value_to_number(b, &y)) return false;
    out->kind = NMO_DSL_VALUE_REAL;
    switch (op) {
        case NMO_DSL_TOK_PLUS: out->as.r = x + y; return true;
        case NMO_DSL_TOK_MINUS: out->as.r = x - y; return true;
        case NMO_DSL_TOK_STAR: out->as.r = x * y; return true;
        case NMO_DSL_TOK_SLASH: out->as.r = (y == 0.0) ? NAN : (x / y); return true;
        case NMO_DSL_TOK_PERCENT: {
            int64_t xi = (int64_t)x;
            int64_t yi = (int64_t)y;
            out->kind = NMO_DSL_VALUE_INT;
            out->as.i = (yi == 0) ? 0 : (xi % yi);
            return true;
        }
        default: return false;
    }
}

static bool eval_compare(nmo_dsl_tok_kind_t op, const nmo_dsl_value_t *a,
                          const nmo_dsl_value_t *b, nmo_dsl_value_t *out) {
    double x = 0.0, y = 0.0;
    if (!value_to_number(a, &x) || !value_to_number(b, &y)) return false;
    bool res = false;
    switch (op) {
        case NMO_DSL_TOK_LT: res = x < y; break;
        case NMO_DSL_TOK_LE: res = x <= y; break;
        case NMO_DSL_TOK_GT: res = x > y; break;
        case NMO_DSL_TOK_GE: res = x >= y; break;
        case NMO_DSL_TOK_EQEQ: res = x == y; break;
        case NMO_DSL_TOK_NEQ: res = x != y; break;
        default: return false;
    }
    out->kind = NMO_DSL_VALUE_BOOL;
    out->as.b = res;
    return true;
}

static bool eval_logic(nmo_dsl_tok_kind_t op, const nmo_dsl_value_t *a,
                        const nmo_dsl_value_t *b, nmo_dsl_value_t *out) {
    bool x = false, y = false;
    if (!value_to_bool(a, &x) || !value_to_bool(b, &y)) return false;
    out->kind = NMO_DSL_VALUE_BOOL;
    if (op == NMO_DSL_TOK_ANDAND) out->as.b = x && y;
    else if (op == NMO_DSL_TOK_OROR) out->as.b = x || y;
    else return false;
    return true;
}

/* ============================================================================
 * Operation invocation helpers
 * ============================================================================ */

static bool op_arg_to_native(
    nmo_dsl_eval_state_t *ev,
    const nmo_dsl_value_t *arg,
    uint8_t *buf,
    size_t buf_size,
    const void **out_ptr,
    const nmo_type_descriptor_t **out_type)
{
    if (!ev || !arg || !buf || !out_ptr || !out_type) return false;
    if (!ev->ctx || !ev->ctx->registry) {
        set_err(ev, "op: no type registry");
        return false;
    }

    *out_ptr = NULL;
    *out_type = NULL;

    if (arg->kind == NMO_DSL_VALUE_INT) {
        if (buf_size < sizeof(int32_t)) return false;
        int32_t iv = (int32_t)arg->as.i;
        memcpy(buf, &iv, sizeof(iv));
        *out_type = nmo_type_registry_find_by_guid(ev->ctx->registry, NMO_TYPE_GUID_INT);
        *out_ptr = buf;
    } else if (arg->kind == NMO_DSL_VALUE_REAL) {
        if (buf_size < sizeof(float)) return false;
        float fv = (float)arg->as.r;
        memcpy(buf, &fv, sizeof(fv));
        *out_type = nmo_type_registry_find_by_guid(ev->ctx->registry, NMO_TYPE_GUID_FLOAT);
        *out_ptr = buf;
    } else if (arg->kind == NMO_DSL_VALUE_BOOL) {
        if (buf_size < sizeof(bool)) return false;
        bool bv = arg->as.b;
        memcpy(buf, &bv, sizeof(bv));
        *out_type = nmo_type_registry_find_by_guid(ev->ctx->registry, NMO_TYPE_GUID_BOOL);
        *out_ptr = buf;
    } else if (arg->kind == NMO_DSL_VALUE_BYREF && arg->as.byref.type && arg->as.byref.ptr) {
        *out_type = arg->as.byref.type;
        *out_ptr = arg->as.byref.ptr;
    } else {
        return false;
    }

    if (!*out_type || !*out_ptr) {
        set_err(ev, "op: failed to resolve argument type");
        return false;
    }

    return true;
}

static bool op_result_to_dsl_value(
    nmo_dsl_eval_state_t *ev,
    const nmo_type_descriptor_t *result_type,
    const void *result_data,
    nmo_dsl_value_t *out)
{
    if (!ev || !result_type || !result_data || !out) return false;
    nmo_guid_t guid = result_type->guid;

    if (nmo_guid_equals(guid, NMO_TYPE_GUID_BOOL)) {
        out->kind = NMO_DSL_VALUE_BOOL;
        out->as.b = *(const bool *)result_data;
        return true;
    }

    if (nmo_guid_equals(guid, NMO_TYPE_GUID_INT8)) {
        out->kind = NMO_DSL_VALUE_INT;
        out->as.i = *(const int8_t *)result_data;
        return true;
    }
    if (nmo_guid_equals(guid, NMO_TYPE_GUID_INT16)) {
        out->kind = NMO_DSL_VALUE_INT;
        out->as.i = *(const int16_t *)result_data;
        return true;
    }
    if (nmo_guid_equals(guid, NMO_TYPE_GUID_INT)) {
        out->kind = NMO_DSL_VALUE_INT;
        out->as.i = *(const int32_t *)result_data;
        return true;
    }
    if (nmo_guid_equals(guid, NMO_TYPE_GUID_INT64)) {
        out->kind = NMO_DSL_VALUE_INT;
        out->as.i = *(const int64_t *)result_data;
        return true;
    }

    if (nmo_guid_equals(guid, NMO_TYPE_GUID_UINT8)) {
        out->kind = NMO_DSL_VALUE_UINT;
        out->as.u = *(const uint8_t *)result_data;
        return true;
    }
    if (nmo_guid_equals(guid, NMO_TYPE_GUID_UINT16)) {
        out->kind = NMO_DSL_VALUE_UINT;
        out->as.u = *(const uint16_t *)result_data;
        return true;
    }
    if (nmo_guid_equals(guid, NMO_TYPE_GUID_UINT32) ||
        nmo_guid_equals(guid, NMO_TYPE_GUID_OBJECT_ID)) {
        out->kind = NMO_DSL_VALUE_UINT;
        out->as.u = *(const uint32_t *)result_data;
        return true;
    }
    if (nmo_guid_equals(guid, NMO_TYPE_GUID_UINT64)) {
        out->kind = NMO_DSL_VALUE_UINT;
        out->as.u = *(const uint64_t *)result_data;
        return true;
    }

    if (nmo_guid_equals(guid, NMO_TYPE_GUID_FLOAT)) {
        out->kind = NMO_DSL_VALUE_REAL;
        out->as.r = (double)*(const float *)result_data;
        return true;
    }
    if (nmo_guid_equals(guid, NMO_TYPE_GUID_DOUBLE)) {
        out->kind = NMO_DSL_VALUE_REAL;
        out->as.r = *(const double *)result_data;
        return true;
    }

    if (nmo_guid_equals(guid, NMO_TYPE_GUID_STRING)) {
        const char *src = *(const char *const *)result_data;
        out->kind = NMO_DSL_VALUE_STRING;
        out->as.s = eval_strdup(src ? src : "");
        if (!out->as.s) {
            set_err(ev, "oom");
            return false;
        }
        return true;
    }

    set_err(ev, "op: unsupported result type");
    return false;
}

/* ============================================================================
 * Builtins
 * ============================================================================ */

static bool eval_call(nmo_dsl_eval_state_t *ev, const nmo_dsl_call_t *call, nmo_dsl_value_t *out) {
    if (!ev || !call || !call->name || !out) return false;

    nmo_dsl_value_t args[4];
    if (call->arg_count > 4) { set_err(ev, "too many args"); return false; }
    memset(args, 0, sizeof(args));

    for (size_t i = 0; i < call->arg_count; ++i) {
        if (!nmo_dsl_eval_expr_impl(ev, call->args[i], &args[i])) {
            for (size_t j = 0; j < i; ++j) nmo_dsl_value_destroy(&args[j]);
            return false;
        }
    }

    const char *name = call->name;

    if (strcmp(name, "len") == 0 || strcmp(name, "size") == 0 || strcmp(name, "count") == 0) {
        if (call->arg_count != 1) { set_err(ev, "len(x)"); goto fail; }
        if (args[0].kind == NMO_DSL_VALUE_SEQ) {
            out->kind = NMO_DSL_VALUE_UINT;
            out->as.u = nmo_dsl_seq_count(args[0].as.seq);
            nmo_dsl_value_destroy(&args[0]);
            return true;
        }
        if (args[0].kind == NMO_DSL_VALUE_STRING && args[0].as.s) {
            out->kind = NMO_DSL_VALUE_UINT;
            out->as.u = (uint64_t)strlen(args[0].as.s);
            nmo_dsl_value_destroy(&args[0]);
            return true;
        }
        set_err(ev, "len expects seq or string");
        goto fail;
    }

    if (strcmp(name, "empty") == 0) {
        if (call->arg_count != 1) { set_err(ev, "empty(x)"); goto fail; }
        out->kind = NMO_DSL_VALUE_BOOL;
        if (args[0].kind == NMO_DSL_VALUE_SEQ) {
            out->as.b = (nmo_dsl_seq_count(args[0].as.seq) == 0);
        } else if (args[0].kind == NMO_DSL_VALUE_STRING && args[0].as.s) {
            out->as.b = (args[0].as.s[0] == '\0');
        } else if (args[0].kind == NMO_DSL_VALUE_NULL) {
            out->as.b = true;
        } else {
            set_err(ev, "empty expects seq or string");
            goto fail;
        }
        nmo_dsl_value_destroy(&args[0]);
        return true;
    }

    if (strcmp(name, "first") == 0 || strcmp(name, "last") == 0) {
        if (call->arg_count != 1 || args[0].kind != NMO_DSL_VALUE_SEQ) {
            set_err(ev, "first/last expects sequence");
            goto fail;
        }
        uint64_t n = nmo_dsl_seq_count(args[0].as.seq);
        if (n == 0) {
            out->kind = NMO_DSL_VALUE_NULL;
            nmo_dsl_value_destroy(&args[0]);
            return true;
        }
        uint64_t idx = (strcmp(name, "first") == 0) ? 0 : (n - 1);
        if (!nmo_dsl_seq_get(args[0].as.seq, idx, out)) {
            set_err(ev, "first/last: index error");
            goto fail;
        }
        nmo_dsl_value_destroy(&args[0]);
        return true;
    }

    if (strcmp(name, "name") == 0) {
        if (call->arg_count != 1) { set_err(ev, "name(id)"); goto fail; }
        double idd = 0.0;
        if (!value_to_number(&args[0], &idd)) { set_err(ev, "name expects numeric id"); goto fail; }
        uint32_t id = (uint32_t)idd;
        const char *n = ev->ctx->resolve_object_name
            ? ev->ctx->resolve_object_name(id, ev->ctx->resolve_object_name_user) : NULL;
        out->kind = NMO_DSL_VALUE_STRING;
        out->as.s = n ? eval_strdup(n) : eval_strdup("");
        if (!out->as.s) { set_err(ev, "oom"); goto fail; }
        nmo_dsl_value_destroy(&args[0]);
        return true;
    }

    if (strcmp(name, "sum") == 0 || strcmp(name, "min") == 0 || strcmp(name, "max") == 0 ||
        strcmp(name, "avg") == 0 || strcmp(name, "any") == 0 || strcmp(name, "all") == 0) {
        if (call->arg_count != 1 || args[0].kind != NMO_DSL_VALUE_SEQ) {
            set_err(ev, "expected one sequence argument");
            goto fail;
        }

        nmo_dsl_seq_t *seq = args[0].as.seq;
        uint64_t n = nmo_dsl_seq_count(seq);

        if (strcmp(name, "any") == 0 || strcmp(name, "all") == 0) {
            bool acc = (strcmp(name, "all") == 0);
            for (uint64_t i = 0; i < n; ++i) {
                nmo_dsl_value_t v = {0};
                if (!nmo_dsl_seq_get(seq, i, &v)) continue;
                bool b = false;
                if (!value_to_bool(&v, &b)) {
                    nmo_dsl_value_destroy(&v);
                    set_err(ev, "any/all expects booleans");
                    goto fail;
                }
                nmo_dsl_value_destroy(&v);
                if (strcmp(name, "any") == 0) {
                    if (b) { acc = true; break; }
                } else {
                    if (!b) { acc = false; break; }
                }
            }
            out->kind = NMO_DSL_VALUE_BOOL;
            out->as.b = acc;
            nmo_dsl_value_destroy(&args[0]);
            return true;
        }

        if (n == 0) {
            out->kind = NMO_DSL_VALUE_NULL;
            nmo_dsl_value_destroy(&args[0]);
            return true;
        }

        double acc = 0.0;
        uint64_t count = 0;
        bool acc_init = false;
        for (uint64_t i = 0; i < n; ++i) {
            nmo_dsl_value_t v = {0};
            if (!nmo_dsl_seq_get(seq, i, &v)) continue;
            double x = 0.0;
            if (!value_to_number(&v, &x)) {
                nmo_dsl_value_destroy(&v);
                set_err(ev, "sum/min/max/avg expects numbers");
                goto fail;
            }
            nmo_dsl_value_destroy(&v);
            if (!acc_init) { acc = x; acc_init = true; }
            else if (strcmp(name, "sum") == 0 || strcmp(name, "avg") == 0) acc += x;
            else if (strcmp(name, "min") == 0) { if (x < acc) acc = x; }
            else if (strcmp(name, "max") == 0) { if (x > acc) acc = x; }
            count++;
        }

        if (strcmp(name, "avg") == 0) {
            if (count == 0) {
                out->kind = NMO_DSL_VALUE_NULL;
                nmo_dsl_value_destroy(&args[0]);
                return true;
            }
            acc = acc / (double)count;
        }

        out->kind = NMO_DSL_VALUE_REAL;
        out->as.r = acc;
        nmo_dsl_value_destroy(&args[0]);
        return true;
    }

    /* ---- Phase D: type builtins ---- */

    if (strcmp(name, "type") == 0) {
        if (call->arg_count != 1) { set_err(ev, "type(x)"); goto fail; }
        out->kind = NMO_DSL_VALUE_TYPE;
        memset(&out->as.type_handle, 0, sizeof(out->as.type_handle));
        switch (args[0].kind) {
            case NMO_DSL_VALUE_BYREF:
                out->as.type_handle.guid = args[0].as.byref.guid;
                out->as.type_handle.type = args[0].as.byref.type;
                break;
            case NMO_DSL_VALUE_OBJECT:
                if (args[0].as.object.type) {
                    out->as.type_handle.guid = args[0].as.object.type->guid;
                    out->as.type_handle.type = args[0].as.object.type;
                }
                break;
            case NMO_DSL_VALUE_INT:
                out->as.type_handle.guid = NMO_TYPE_GUID_INT64;
                out->as.type_handle.type = nmo_type_registry_find_by_guid(
                    ev->ctx->registry, NMO_TYPE_GUID_INT64);
                break;
            case NMO_DSL_VALUE_UINT:
                out->as.type_handle.guid = NMO_TYPE_GUID_UINT64;
                out->as.type_handle.type = nmo_type_registry_find_by_guid(
                    ev->ctx->registry, NMO_TYPE_GUID_UINT64);
                break;
            case NMO_DSL_VALUE_REAL:
                out->as.type_handle.guid = NMO_TYPE_GUID_DOUBLE;
                out->as.type_handle.type = nmo_type_registry_find_by_guid(
                    ev->ctx->registry, NMO_TYPE_GUID_DOUBLE);
                break;
            case NMO_DSL_VALUE_BOOL:
                out->as.type_handle.guid = NMO_TYPE_GUID_BOOL;
                out->as.type_handle.type = nmo_type_registry_find_by_guid(
                    ev->ctx->registry, NMO_TYPE_GUID_BOOL);
                break;
            case NMO_DSL_VALUE_STRING:
                out->as.type_handle.guid = NMO_TYPE_GUID_STRING;
                out->as.type_handle.type = nmo_type_registry_find_by_guid(
                    ev->ctx->registry, NMO_TYPE_GUID_STRING);
                break;
            default:
                out->kind = NMO_DSL_VALUE_NULL;
                break;
        }
        for (size_t j = 0; j < call->arg_count; ++j)
            nmo_dsl_value_destroy(&args[j]);
        return true;
    }

    if (strcmp(name, "type_name") == 0) {
        if (call->arg_count != 1) { set_err(ev, "type_name(x)"); goto fail; }
        const char *tname = NULL;
        if (args[0].kind == NMO_DSL_VALUE_BYREF && args[0].as.byref.type) {
            tname = args[0].as.byref.type->name;
        } else if (args[0].kind == NMO_DSL_VALUE_OBJECT && args[0].as.object.type) {
            tname = args[0].as.object.type->name;
        } else if (args[0].kind == NMO_DSL_VALUE_TYPE && args[0].as.type_handle.type) {
            tname = args[0].as.type_handle.type->name;
        } else if (ev->ctx->registry) {
            /* Try to look up by GUID for primitive literals */
            nmo_guid_t g = {0, 0};
            if (args[0].kind == NMO_DSL_VALUE_INT) g = NMO_TYPE_GUID_INT64;
            else if (args[0].kind == NMO_DSL_VALUE_UINT) g = NMO_TYPE_GUID_UINT64;
            else if (args[0].kind == NMO_DSL_VALUE_REAL) g = NMO_TYPE_GUID_DOUBLE;
            else if (args[0].kind == NMO_DSL_VALUE_BOOL) g = NMO_TYPE_GUID_BOOL;
            else if (args[0].kind == NMO_DSL_VALUE_STRING) g = NMO_TYPE_GUID_STRING;
            tname = nmo_type_registry_guid_to_name(ev->ctx->registry, g);
        }
        out->kind = NMO_DSL_VALUE_STRING;
        out->as.s = eval_strdup(tname ? tname : "");
        if (!out->as.s) { set_err(ev, "oom"); goto fail; }
        for (size_t j = 0; j < call->arg_count; ++j)
            nmo_dsl_value_destroy(&args[j]);
        return true;
    }

    if (strcmp(name, "to_string") == 0) {
        if (call->arg_count != 1) { set_err(ev, "to_string(x)"); goto fail; }
        char buf[256];
        buf[0] = '\0';
        if (args[0].kind == NMO_DSL_VALUE_INT) {
            (void)snprintf(buf, sizeof(buf), "%lld", (long long)args[0].as.i);
        } else if (args[0].kind == NMO_DSL_VALUE_UINT) {
            (void)snprintf(buf, sizeof(buf), "%llu", (unsigned long long)args[0].as.u);
        } else if (args[0].kind == NMO_DSL_VALUE_REAL) {
            (void)snprintf(buf, sizeof(buf), "%g", args[0].as.r);
        } else if (args[0].kind == NMO_DSL_VALUE_BOOL) {
            (void)snprintf(buf, sizeof(buf), "%s", args[0].as.b ? "true" : "false");
        } else if (args[0].kind == NMO_DSL_VALUE_STRING) {
            out->kind = NMO_DSL_VALUE_STRING;
            out->as.s = eval_strdup(args[0].as.s ? args[0].as.s : "");
            if (!out->as.s) { set_err(ev, "oom"); goto fail; }
            for (size_t j = 0; j < call->arg_count; ++j)
                nmo_dsl_value_destroy(&args[j]);
            return true;
        } else if (args[0].kind == NMO_DSL_VALUE_BYREF &&
                   args[0].as.byref.ptr && args[0].as.byref.type &&
                   ev->ctx->registry) {
            nmo_type_value_to_string(args[0].as.byref.ptr,
                args[0].as.byref.type, ev->ctx->registry, buf, sizeof(buf));
        } else if (args[0].kind == NMO_DSL_VALUE_NULL) {
            (void)snprintf(buf, sizeof(buf), "null");
        } else {
            set_err(ev, "to_string: unsupported value"); goto fail;
        }
        out->kind = NMO_DSL_VALUE_STRING;
        out->as.s = eval_strdup(buf);
        if (!out->as.s) { set_err(ev, "oom"); goto fail; }
        for (size_t j = 0; j < call->arg_count; ++j)
            nmo_dsl_value_destroy(&args[j]);
        return true;
    }

    if (strcmp(name, "from_string") == 0) {
        if (call->arg_count != 2) { set_err(ev, "from_string(type_name, s)"); goto fail; }
        if (args[0].kind != NMO_DSL_VALUE_STRING || !args[0].as.s) {
            set_err(ev, "from_string: first arg must be string"); goto fail;
        }
        if (args[1].kind != NMO_DSL_VALUE_STRING || !args[1].as.s) {
            set_err(ev, "from_string: second arg must be string"); goto fail;
        }
        if (!ev->ctx->registry) { set_err(ev, "from_string: no registry"); goto fail; }

        const nmo_type_descriptor_t *type =
            nmo_type_registry_find_by_name(ev->ctx->registry, args[0].as.s);
        if (!type) { set_err(ev, "from_string: unknown type"); goto fail; }

        /* Parse scalar-like values through the type conversion layer. */
        if (type->size <= sizeof(uint64_t) && type->size > 0) {
            uint64_t tmp = 0;
            nmo_status_t st = nmo_type_value_from_string(&tmp, type,
                ev->ctx->registry, args[1].as.s);
            if (st != NMO_OK) { set_err(ev, "from_string: parse failed"); goto fail; }

            if (!op_result_to_dsl_value(ev, type, &tmp, out)) {
                set_err(ev, "from_string: unsupported type");
                goto fail;
            }
        } else {
            set_err(ev, "from_string: complex types not supported"); goto fail;
        }
        for (size_t j = 0; j < call->arg_count; ++j)
            nmo_dsl_value_destroy(&args[j]);
        return true;
    }

    if (strcmp(name, "cast") == 0) {
        if (call->arg_count != 2) { set_err(ev, "cast(x, type_name)"); goto fail; }
        if (args[1].kind != NMO_DSL_VALUE_STRING || !args[1].as.s) {
            set_err(ev, "cast: second arg must be type name string"); goto fail;
        }
        const char *target = args[1].as.s;
        double d = 0.0;
        if (strcmp(target, "float") == 0 || strcmp(target, "double") == 0 ||
            strcmp(target, "real") == 0) {
            if (!value_to_number(&args[0], &d)) {
                set_err(ev, "cast: cannot convert to number"); goto fail;
            }
            out->kind = NMO_DSL_VALUE_REAL;
            out->as.r = d;
        } else if (strcmp(target, "int") == 0 || strcmp(target, "int32") == 0 ||
                   strcmp(target, "int64") == 0) {
            if (!value_to_number(&args[0], &d)) {
                set_err(ev, "cast: cannot convert to number"); goto fail;
            }
            out->kind = NMO_DSL_VALUE_INT;
            out->as.i = (int64_t)d;
        } else if (strcmp(target, "uint32") == 0 || strcmp(target, "uint64") == 0) {
            if (!value_to_number(&args[0], &d)) {
                set_err(ev, "cast: cannot convert to number"); goto fail;
            }
            out->kind = NMO_DSL_VALUE_UINT;
            out->as.u = (uint64_t)d;
        } else if (strcmp(target, "bool") == 0) {
            bool b = false;
            if (!value_to_bool(&args[0], &b)) {
                set_err(ev, "cast: cannot convert to bool"); goto fail;
            }
            out->kind = NMO_DSL_VALUE_BOOL;
            out->as.b = b;
        } else if (strcmp(target, "string") == 0) {
            char buf[256] = {0};
            if (args[0].kind == NMO_DSL_VALUE_INT) {
                (void)snprintf(buf, sizeof(buf), "%lld", (long long)args[0].as.i);
            } else if (args[0].kind == NMO_DSL_VALUE_REAL) {
                (void)snprintf(buf, sizeof(buf), "%g", args[0].as.r);
            } else if (args[0].kind == NMO_DSL_VALUE_BOOL) {
                (void)snprintf(buf, sizeof(buf), "%s", args[0].as.b ? "true" : "false");
            } else {
                set_err(ev, "cast: cannot convert to string"); goto fail;
            }
            out->kind = NMO_DSL_VALUE_STRING;
            out->as.s = eval_strdup(buf);
        } else {
            set_err(ev, "cast: unknown target type"); goto fail;
        }
        for (size_t j = 0; j < call->arg_count; ++j)
            nmo_dsl_value_destroy(&args[j]);
        return true;
    }

    /* ---- Phase E: operation invocation ---- */

    if (strcmp(name, "op") == 0) {
        if (call->arg_count < 2 || call->arg_count > 3) {
            set_err(ev, "op(name, arg1 [, arg2])"); goto fail;
        }
        if (!ev->ctx->ops) { set_err(ev, "op: no operation registry"); goto fail; }
        if (args[0].kind != NMO_DSL_VALUE_STRING || !args[0].as.s) {
            set_err(ev, "op: first arg must be operation name string"); goto fail;
        }

        /* Find operation family by name */
        nmo_operation_registry_t *ops = ev->ctx->ops;
        const nmo_operation_family_t *fam = NULL;
        for (uint32_t fi = 0; fi < ops->family_count; ++fi) {
            if (ops->families[fi] && ops->families[fi]->name &&
                strcmp(ops->families[fi]->name, args[0].as.s) == 0) {
                fam = ops->families[fi];
                break;
            }
        }
        if (!fam) { set_err(ev, "op: unknown operation"); goto fail; }

        /* Convert DSL values to typed buffers for the operation system */
        uint8_t p1_buf[16] = {0}, p2_buf[16] = {0};
        const nmo_type_descriptor_t *p1_type = NULL;
        const nmo_type_descriptor_t *p2_type = NULL;
        const void *p1_ptr = NULL, *p2_ptr = NULL;

        if (!op_arg_to_native(ev, &args[1], p1_buf, sizeof(p1_buf), &p1_ptr, &p1_type)) {
            set_err(ev, "op: unsupported p1 type");
            goto fail;
        }

        /* P2 (optional) */
        if (call->arg_count == 3) {
            if (!op_arg_to_native(ev, &args[2], p2_buf, sizeof(p2_buf), &p2_ptr, &p2_type)) {
                set_err(ev, "op: unsupported p2 type");
                goto fail;
            }
        }

        const nmo_operation_tree_cell_t *cell = NULL;
        nmo_status_t st = nmo_operation_registry_find(
            ops,
            &fam->operation_guid,
            p1_type,
            p2_type,
            ev->ctx->registry,
            &cell);
        if (st != NMO_OK || !cell || !cell->desc.function || !cell->result_type) {
            set_err(ev, "op: operation not found");
            goto fail;
        }

        size_t result_size = (cell->result_type->size > 0) ? (size_t)cell->result_type->size : sizeof(uint64_t);
        uint8_t result_stack[32] = {0};
        void *result_ptr = result_stack;
        if (result_size > sizeof(result_stack)) {
            result_ptr = malloc(result_size);
            if (!result_ptr) {
                set_err(ev, "oom");
                goto fail;
            }
            memset(result_ptr, 0, result_size);
        }

        st = cell->desc.function(
            p1_ptr, p1_type,
            p2_ptr, p2_type,
            result_ptr, cell->result_type,
            cell->desc.user_data);
        if (st != NMO_OK) {
            if (result_ptr != result_stack) free(result_ptr);
            set_err(ev, "op: execution failed");
            goto fail;
        }

        if (!op_result_to_dsl_value(ev, cell->result_type, result_ptr, out)) {
            if (result_ptr != result_stack) free(result_ptr);
            goto fail;
        }
        if (result_ptr != result_stack) free(result_ptr);

        for (size_t j = 0; j < call->arg_count; ++j)
            nmo_dsl_value_destroy(&args[j]);
        return true;
    }

    set_err(ev, "unknown function");

fail:
    for (size_t i = 0; i < call->arg_count; ++i) nmo_dsl_value_destroy(&args[i]);
    return false;
}

/* ============================================================================
 * Index
 * ============================================================================ */

static bool eval_index(nmo_dsl_eval_state_t *ev, const nmo_dsl_value_t *base,
                        const nmo_dsl_value_t *idx, nmo_dsl_value_t *out) {
    if (base->kind != NMO_DSL_VALUE_SEQ) { set_err(ev, "indexing requires sequence"); return false; }
    double d = 0.0;
    if (!value_to_number(idx, &d)) { set_err(ev, "index must be number"); return false; }
    if (d < 0) { set_err(ev, "index out of bounds"); return false; }
    uint64_t i = (uint64_t)d;
    if (!nmo_dsl_seq_get(base->as.seq, i, out)) { set_err(ev, "index out of bounds"); return false; }
    return true;
}

/* ============================================================================
 * Recursive expression evaluator
 * ============================================================================ */

bool nmo_dsl_eval_expr_impl(nmo_dsl_eval_state_t *ev, const nmo_dsl_expr_t *e, nmo_dsl_value_t *out) {
    if (!ev || !e || !out) return false;

    switch (e->kind) {
        case NMO_DSL_EXPR_LITERAL: {
            memset(out, 0, sizeof(*out));
            switch (e->as.lit.kind) {
                case NMO_DSL_LIT_NULL: out->kind = NMO_DSL_VALUE_NULL; break;
                case NMO_DSL_LIT_BOOL: out->kind = NMO_DSL_VALUE_BOOL; out->as.b = e->as.lit.as.b; break;
                case NMO_DSL_LIT_INT: out->kind = NMO_DSL_VALUE_INT; out->as.i = e->as.lit.as.i; break;
                case NMO_DSL_LIT_UINT: out->kind = NMO_DSL_VALUE_UINT; out->as.u = e->as.lit.as.u; break;
                case NMO_DSL_LIT_REAL: out->kind = NMO_DSL_VALUE_REAL; out->as.r = e->as.lit.as.r; break;
                case NMO_DSL_LIT_STRING:
                    out->kind = NMO_DSL_VALUE_STRING;
                    out->as.s = eval_strdup(e->as.lit.as.s);
                    if (!out->as.s && e->as.lit.as.s) { set_err(ev, "oom"); return false; }
                    break;
            }
            return true;
        }

        case NMO_DSL_EXPR_IDENT:
            return resolve_ident(ev, e->as.ident, out);

        case NMO_DSL_EXPR_UNARY: {
            nmo_dsl_value_t rhs = {0};
            if (!nmo_dsl_eval_expr_impl(ev, e->as.unary.rhs, &rhs)) return false;

            if (e->as.unary.op == NMO_DSL_TOK_BANG) {
                bool b = false;
                if (!value_to_bool(&rhs, &b)) {
                    nmo_dsl_value_destroy(&rhs);
                    set_err(ev, "! expects boolean");
                    return false;
                }
                out->kind = NMO_DSL_VALUE_BOOL;
                out->as.b = !b;
                nmo_dsl_value_destroy(&rhs);
                return true;
            }

            if (e->as.unary.op == NMO_DSL_TOK_MINUS || e->as.unary.op == NMO_DSL_TOK_PLUS) {
                double x = 0.0;
                if (!value_to_number(&rhs, &x)) {
                    nmo_dsl_value_destroy(&rhs);
                    set_err(ev, "unary expects number");
                    return false;
                }
                out->kind = NMO_DSL_VALUE_REAL;
                out->as.r = (e->as.unary.op == NMO_DSL_TOK_MINUS) ? -x : x;
                nmo_dsl_value_destroy(&rhs);
                return true;
            }

            nmo_dsl_value_destroy(&rhs);
            set_err(ev, "bad unary");
            return false;
        }

        case NMO_DSL_EXPR_BINARY: {
            nmo_dsl_value_t lhs = {0};
            if (!nmo_dsl_eval_expr_impl(ev, e->as.binary.lhs, &lhs)) return false;

            bool ok = false;
            nmo_dsl_tok_kind_t op = e->as.binary.op;
            if (op == NMO_DSL_TOK_ANDAND || op == NMO_DSL_TOK_OROR) {
                bool lb = false;
                if (!value_to_bool(&lhs, &lb)) {
                    nmo_dsl_value_destroy(&lhs);
                    set_err(ev, "bad binary");
                    return false;
                }
                if (op == NMO_DSL_TOK_ANDAND && !lb) {
                    out->kind = NMO_DSL_VALUE_BOOL;
                    out->as.b = false;
                    nmo_dsl_value_destroy(&lhs);
                    return true;
                }
                if (op == NMO_DSL_TOK_OROR && lb) {
                    out->kind = NMO_DSL_VALUE_BOOL;
                    out->as.b = true;
                    nmo_dsl_value_destroy(&lhs);
                    return true;
                }
            }

            nmo_dsl_value_t rhs = {0};
            if (!nmo_dsl_eval_expr_impl(ev, e->as.binary.rhs, &rhs)) {
                nmo_dsl_value_destroy(&lhs);
                return false;
            }

            if (op == NMO_DSL_TOK_PLUS || op == NMO_DSL_TOK_MINUS || op == NMO_DSL_TOK_STAR ||
                op == NMO_DSL_TOK_SLASH || op == NMO_DSL_TOK_PERCENT) {
                ok = eval_binary_numeric(op, &lhs, &rhs, out);
            } else if (op == NMO_DSL_TOK_LT || op == NMO_DSL_TOK_LE || op == NMO_DSL_TOK_GT ||
                       op == NMO_DSL_TOK_GE || op == NMO_DSL_TOK_EQEQ || op == NMO_DSL_TOK_NEQ) {
                ok = eval_compare(op, &lhs, &rhs, out);
            } else if (op == NMO_DSL_TOK_ANDAND || op == NMO_DSL_TOK_OROR) {
                ok = eval_logic(op, &lhs, &rhs, out);
            }

            nmo_dsl_value_destroy(&lhs);
            nmo_dsl_value_destroy(&rhs);
            if (!ok) { set_err(ev, "bad binary"); return false; }
            return true;
        }

        case NMO_DSL_EXPR_CALL:
            return eval_call(ev, &e->as.call, out);

        case NMO_DSL_EXPR_MEMBER: {
            nmo_dsl_value_t b = {0};
            if (!nmo_dsl_eval_expr_impl(ev, e->as.member.base, &b)) return false;
            bool ok = eval_member(ev, &b, e->as.member.field, out);
            /* If member created a mapped sequence, it consumed b.as.seq */
            if (ok && b.kind == NMO_DSL_VALUE_SEQ && out->kind == NMO_DSL_VALUE_SEQ) {
                b.as.seq = NULL;
            }
            nmo_dsl_value_destroy(&b);
            return ok;
        }

        case NMO_DSL_EXPR_WILDCARD: {
            nmo_dsl_value_t b = {0};
            if (!nmo_dsl_eval_expr_impl(ev, e->as.wild_base, &b)) return false;
            if (b.kind != NMO_DSL_VALUE_SEQ) {
                nmo_dsl_value_destroy(&b);
                set_err(ev, "[*] expects array");
                return false;
            }
            *out = b;
            return true;
        }

        case NMO_DSL_EXPR_INDEX: {
            nmo_dsl_value_t b = {0}, i = {0};
            if (!nmo_dsl_eval_expr_impl(ev, e->as.index.base, &b)) return false;
            if (!nmo_dsl_eval_expr_impl(ev, e->as.index.index, &i)) {
                nmo_dsl_value_destroy(&b);
                return false;
            }
            bool ok = eval_index(ev, &b, &i, out);
            nmo_dsl_value_destroy(&b);
            nmo_dsl_value_destroy(&i);
            return ok;
        }

        case NMO_DSL_EXPR_SLICE: {
            nmo_dsl_value_t b = {0};
            if (!nmo_dsl_eval_expr_impl(ev, e->as.slice.base, &b)) return false;
            if (b.kind != NMO_DSL_VALUE_SEQ) {
                nmo_dsl_value_destroy(&b);
                set_err(ev, "slice expects array");
                return false;
            }

            uint64_t n = nmo_dsl_seq_count(b.as.seq);
            uint64_t start = 0, end = n;

            if (e->as.slice.start) {
                nmo_dsl_value_t sv = {0};
                if (!nmo_dsl_eval_expr_impl(ev, e->as.slice.start, &sv)) {
                    nmo_dsl_value_destroy(&b);
                    return false;
                }
                double d = 0.0;
                if (!value_to_number(&sv, &d)) {
                    nmo_dsl_value_destroy(&sv);
                    nmo_dsl_value_destroy(&b);
                    set_err(ev, "slice start must be number");
                    return false;
                }
                nmo_dsl_value_destroy(&sv);
                start = (d < 0) ? 0 : (uint64_t)d;
            }

            if (e->as.slice.end) {
                nmo_dsl_value_t sv = {0};
                if (!nmo_dsl_eval_expr_impl(ev, e->as.slice.end, &sv)) {
                    nmo_dsl_value_destroy(&b);
                    return false;
                }
                double d = 0.0;
                if (!value_to_number(&sv, &d)) {
                    nmo_dsl_value_destroy(&sv);
                    nmo_dsl_value_destroy(&b);
                    set_err(ev, "slice end must be number");
                    return false;
                }
                nmo_dsl_value_destroy(&sv);
                end = (d < 0) ? 0 : (uint64_t)d;
            }

            nmo_dsl_seq_t *sl = nmo_dsl_seq_slice_create(b.as.seq, start, end);
            if (!sl) {
                nmo_dsl_value_destroy(&b);
                set_err(ev, "oom");
                return false;
            }
            b.as.seq = NULL; /* ownership transferred */
            out->kind = NMO_DSL_VALUE_SEQ;
            out->as.seq = sl;
            nmo_dsl_value_destroy(&b);
            return true;
        }

        case NMO_DSL_EXPR_FILTER: {
            nmo_dsl_value_t b = {0};
            if (!nmo_dsl_eval_expr_impl(ev, e->as.filter.base, &b)) return false;
            if (b.kind != NMO_DSL_VALUE_SEQ) {
                nmo_dsl_value_destroy(&b);
                set_err(ev, "filter expects array");
                return false;
            }

            nmo_dsl_seq_t *filt = nmo_dsl_seq_filter_create(b.as.seq, e->as.filter.pred, ev);
            if (!filt) {
                nmo_dsl_value_destroy(&b);
                if (!ev->err[0]) set_err(ev, "failed to build filter");
                return false;
            }
            b.as.seq = NULL;
            out->kind = NMO_DSL_VALUE_SEQ;
            out->as.seq = filt;
            nmo_dsl_value_destroy(&b);
            return true;
        }

        default:
            set_err(ev, "unsupported expr");
            return false;
    }
}

/* ============================================================================
 * Phase B: Mutable field resolution and assignment
 * ============================================================================ */

static bool resolve_mutable_ident(nmo_dsl_eval_state_t *ev,
                                   const char *name, void **out_ptr,
                                   const nmo_type_field_t **out_field) {
    const nmo_dsl_eval_context_t *ctx = ev->ctx;
    if (!ctx || !name || !out_ptr || !out_field) return false;

    /* Try root type fields */
    if (!ctx->root_type || !ctx->root_instance ||
        !nmo_type_has_reflection(ctx->root_type)) {
        set_err(ev, "no reflection for assignment");
        return false;
    }

    const nmo_type_field_t *f =
        nmo_type_get_field_by_name(ctx->root_type, name);
    if (!f) { set_err(ev, "field not found for assignment"); return false; }

    void *ptr = nmo_field_get_ptr(ctx->root_instance, f);
    if (!ptr) { set_err(ev, "null field pointer"); return false; }

    *out_ptr = ptr;
    *out_field = f;
    return true;
}

static bool resolve_mutable_repeated_field(
    nmo_dsl_eval_state_t *ev,
    const nmo_dsl_expr_t *base_expr,
    void **out_array_ptr,
    uint64_t *out_count,
    size_t *out_elem_size,
    nmo_guid_t *out_elem_guid,
    const nmo_type_descriptor_t **out_owner_type,
    const void **out_owner_instance,
    const nmo_type_field_t **out_field)
{
    const nmo_dsl_eval_context_t *ctx = ev->ctx;
    if (!ctx || !base_expr || !out_array_ptr || !out_count || !out_elem_size || !out_elem_guid) {
        return false;
    }

    void *field_ptr = NULL;
    const nmo_type_field_t *f = NULL;
    const nmo_type_descriptor_t *owner_type = NULL;
    const void *owner_instance = NULL;

    if (base_expr->kind == NMO_DSL_EXPR_IDENT) {
        if (!resolve_mutable_ident(ev, base_expr->as.ident, &field_ptr, &f)) {
            return false;
        }
        owner_type = ctx->root_type;
        owner_instance = ctx->root_instance;
    } else if (base_expr->kind == NMO_DSL_EXPR_MEMBER) {
        nmo_dsl_value_t base_val = {0};
        if (!nmo_dsl_eval_expr_impl(ev, base_expr->as.member.base, &base_val)) {
            return false;
        }

        if (base_val.kind == NMO_DSL_VALUE_OBJECT) {
            owner_type = base_val.as.object.type;
            owner_instance = base_val.as.object.instance;
        } else if (base_val.kind == NMO_DSL_VALUE_BYREF &&
                   base_val.as.byref.type &&
                   nmo_type_has_reflection(base_val.as.byref.type)) {
            owner_type = base_val.as.byref.type;
            owner_instance = base_val.as.byref.ptr;
        }

        if (!owner_type || !owner_instance || !nmo_type_has_reflection(owner_type)) {
            nmo_dsl_value_destroy(&base_val);
            set_err(ev, "cannot resolve member array for assignment");
            return false;
        }

        f = nmo_type_get_field_by_name(owner_type, base_expr->as.member.field);
        if (!f) {
            nmo_dsl_value_destroy(&base_val);
            set_err(ev, "field not found for assignment");
            return false;
        }
        field_ptr = nmo_field_get_ptr((void *)owner_instance, f);
        nmo_dsl_value_destroy(&base_val);
        if (!field_ptr) {
            set_err(ev, "null field pointer");
            return false;
        }
    } else {
        set_err(ev, "complex index assignment not supported");
        return false;
    }

    if (!(f->flags & NMO_FIELD_REPEATED)) {
        set_err(ev, "index assignment: not an array");
        return false;
    }

    const void *array_ptr = NULL;
    uint64_t count = 0;
    size_t elem_size = 0;
    nmo_guid_t elem_guid = NMO_GUID_NULL;
    const nmo_type_descriptor_t *elem_type = NULL;
    if (!resolve_repeated_field_view(
            ctx, owner_type, owner_instance, f, field_ptr,
            &array_ptr, &count, &elem_size, &elem_guid, &elem_type, ev)) {
        return false;
    }

    if (!array_ptr) {
        set_err(ev, "null array pointer");
        return false;
    }

    (void)elem_type;

    *out_array_ptr = (void *)array_ptr;
    *out_count = count;
    *out_elem_size = elem_size;
    *out_elem_guid = elem_guid;
    if (out_owner_type) *out_owner_type = owner_type;
    if (out_owner_instance) *out_owner_instance = owner_instance;
    if (out_field) *out_field = f;
    return true;
}

static bool resolve_mutable_target(nmo_dsl_eval_state_t *ev,
                                    const nmo_dsl_expr_t *target,
                                    void **out_ptr, size_t *out_size,
                                    nmo_guid_t *out_guid) {
    if (target->kind == NMO_DSL_EXPR_IDENT) {
        void *ptr = NULL;
        const nmo_type_field_t *f = NULL;
        if (!resolve_mutable_ident(ev, target->as.ident, &ptr, &f))
            return false;
        *out_ptr = ptr;
        *out_size = f->size;
        *out_guid = f->type_guid;
        return true;
    }

    if (target->kind == NMO_DSL_EXPR_MEMBER) {
        /* Resolve base to a typed location */
        nmo_dsl_value_t base_val = {0};
        if (!nmo_dsl_eval_expr_impl(ev, target->as.member.base, &base_val))
            return false;

        const nmo_type_descriptor_t *type = NULL;
        void *instance = NULL;

        if (base_val.kind == NMO_DSL_VALUE_OBJECT) {
            type = base_val.as.object.type;
            instance = (void *)base_val.as.object.instance;
        } else if (base_val.kind == NMO_DSL_VALUE_BYREF &&
                   base_val.as.byref.type &&
                   nmo_type_has_reflection(base_val.as.byref.type)) {
            type = base_val.as.byref.type;
            instance = (void *)base_val.as.byref.ptr;
        }

        nmo_dsl_value_destroy(&base_val);

        if (!type || !instance || !nmo_type_has_reflection(type)) {
            set_err(ev, "cannot resolve member for assignment");
            return false;
        }

        const nmo_type_field_t *f =
            nmo_type_get_field_by_name(type, target->as.member.field);
        if (!f) { set_err(ev, "field not found for assignment"); return false; }

        void *ptr = nmo_field_get_ptr(instance, f);
        if (!ptr) { set_err(ev, "null field pointer"); return false; }

        *out_ptr = ptr;
        *out_size = f->size;
        *out_guid = f->type_guid;
        return true;
    }

    if (target->kind == NMO_DSL_EXPR_INDEX) {
        void *array_ptr = NULL;
        uint64_t count = 0;
        size_t elem_size = 0;
        nmo_guid_t elem_guid = NMO_GUID_NULL;

        if (!resolve_mutable_repeated_field(ev, target->as.index.base,
                                            &array_ptr, &count, &elem_size, &elem_guid,
                                            NULL, NULL, NULL)) {
            return false;
        }

        nmo_dsl_value_t idx_val = {0};
        if (!nmo_dsl_eval_expr_impl(ev, target->as.index.index, &idx_val)) {
            return false;
        }

        double d = 0.0;
        if (!value_to_number(&idx_val, &d)) {
            nmo_dsl_value_destroy(&idx_val);
            set_err(ev, "index must be number");
            return false;
        }
        nmo_dsl_value_destroy(&idx_val);

        if (d < 0) {
            set_err(ev, "negative index");
            return false;
        }
        uint64_t idx = (uint64_t)d;

        if (idx >= count) {
            set_err(ev, "index out of bounds");
            return false;
        }

        *out_ptr = (uint8_t *)array_ptr + (size_t)idx * elem_size;
        *out_size = elem_size;
        *out_guid = elem_guid;
        return true;
    }

    set_err(ev, "invalid assignment target");
    return false;
}

static bool eval_assign(nmo_dsl_eval_state_t *ev,
                         const nmo_dsl_expr_t *target,
                         const nmo_dsl_expr_t *value_expr) {
    /* Evaluate RHS first */
    nmo_dsl_value_t rhs = {0};
    if (!nmo_dsl_eval_expr_impl(ev, value_expr, &rhs)) return false;

    /* Resolve LHS to a writable pointer */
    void *ptr = NULL;
    size_t size = 0;
    nmo_guid_t guid = {0, 0};
    if (!resolve_mutable_target(ev, target, &ptr, &size, &guid)) {
        nmo_dsl_value_destroy(&rhs);
        return false;
    }

    const nmo_type_descriptor_t *target_type = lookup_field_type(ev->ctx->registry, guid);
    nmo_guid_t target_guid = target_type ? target_type->guid : guid;
    enum {
        NMO_DSL_NUM_NONE = 0,
        NMO_DSL_NUM_INT,
        NMO_DSL_NUM_UINT,
        NMO_DSL_NUM_FLOAT,
        NMO_DSL_NUM_BOOL
    } target_kind = NMO_DSL_NUM_NONE;

    if (nmo_guid_equals(target_guid, CKPGUID_BOOL)) {
        target_kind = NMO_DSL_NUM_BOOL;
    } else if (nmo_guid_equals(target_guid, CKPGUID_INT8) ||
               nmo_guid_equals(target_guid, CKPGUID_INT16) ||
               nmo_guid_equals(target_guid, CKPGUID_INT) ||
               nmo_guid_equals(target_guid, CKPGUID_INT64)) {
        target_kind = NMO_DSL_NUM_INT;
    } else if (nmo_guid_equals(target_guid, CKPGUID_UINT8) ||
               nmo_guid_equals(target_guid, CKPGUID_UINT16) ||
               nmo_guid_equals(target_guid, CKPGUID_UINT32) ||
               nmo_guid_equals(target_guid, CKPGUID_UINT64) ||
               nmo_guid_equals(target_guid, CKPGUID_ID)) {
        target_kind = NMO_DSL_NUM_UINT;
    } else if (nmo_guid_equals(target_guid, CKPGUID_FLOAT) ||
               nmo_guid_equals(target_guid, CKPGUID_DOUBLE)) {
        target_kind = NMO_DSL_NUM_FLOAT;
    }

    bool target_is_numeric = (target_kind != NMO_DSL_NUM_NONE);

    /* First, get a numeric representation of the RHS */
    double d_val = 0.0;
    int64_t i_val = 0;
    bool have_number = false;

    if (rhs.kind == NMO_DSL_VALUE_INT && target_is_numeric) {
        i_val = rhs.as.i;
        d_val = (double)rhs.as.i;
        have_number = true;
    } else if (rhs.kind == NMO_DSL_VALUE_UINT && target_is_numeric) {
        i_val = (int64_t)rhs.as.u;
        d_val = (double)rhs.as.u;
        have_number = true;
    } else if (rhs.kind == NMO_DSL_VALUE_REAL && target_is_numeric) {
        i_val = (int64_t)rhs.as.r;
        d_val = rhs.as.r;
        have_number = true;
    } else if (rhs.kind == NMO_DSL_VALUE_BOOL && target_is_numeric) {
        i_val = rhs.as.b ? 1 : 0;
        d_val = rhs.as.b ? 1.0 : 0.0;
        have_number = true;
    } else if (rhs.kind == NMO_DSL_VALUE_BYREF && rhs.as.byref.ptr) {
        bool direct_copy_ok = false;
        if (nmo_guid_equals(rhs.as.byref.guid, guid) && rhs.as.byref.size == size) {
            direct_copy_ok = true;
        } else {
            if (target_type && rhs.as.byref.type &&
                nmo_guid_equals(target_type->guid, rhs.as.byref.type->guid) &&
                rhs.as.byref.size == size) {
                direct_copy_ok = true;
            }
        }
        if (direct_copy_ok) {
            memcpy(ptr, rhs.as.byref.ptr, size);
            nmo_dsl_value_destroy(&rhs);
            return true;
        }
        if (target_is_numeric && value_to_number(&rhs, &d_val)) {
            i_val = (int64_t)d_val;
            have_number = true;
        }
    }

    if (!have_number) {
        nmo_dsl_value_destroy(&rhs);
        if (!target_is_numeric) {
            set_err(ev, "cannot assign non-numeric field from incompatible value");
        } else {
            set_err(ev, "cannot assign this value type");
        }
        return false;
    }

    /* Write based on target type class */
    if (target_kind == NMO_DSL_NUM_INT) {
        /* Target is signed integer */
        if (size == 4)      { int32_t v = (int32_t)i_val; memcpy(ptr, &v, 4); }
        else if (size == 8) { memcpy(ptr, &i_val, 8); }
        else if (size == 2) { int16_t v = (int16_t)i_val; memcpy(ptr, &v, 2); }
        else if (size == 1) { int8_t v = (int8_t)i_val; memcpy(ptr, &v, 1); }
        else { nmo_dsl_value_destroy(&rhs); set_err(ev, "unsupported int field size"); return false; }
    } else if (target_kind == NMO_DSL_NUM_UINT) {
        /* Target is unsigned integer */
        uint64_t u_val = (uint64_t)i_val;
        if (rhs.kind == NMO_DSL_VALUE_UINT) u_val = rhs.as.u;
        else if (rhs.kind == NMO_DSL_VALUE_REAL) u_val = (uint64_t)d_val;
        if (size == 4)      { uint32_t v = (uint32_t)u_val; memcpy(ptr, &v, 4); }
        else if (size == 8) { memcpy(ptr, &u_val, 8); }
        else if (size == 2) { uint16_t v = (uint16_t)u_val; memcpy(ptr, &v, 2); }
        else if (size == 1) { uint8_t v = (uint8_t)u_val; memcpy(ptr, &v, 1); }
        else { nmo_dsl_value_destroy(&rhs); set_err(ev, "unsupported uint field size"); return false; }
    } else if (target_kind == NMO_DSL_NUM_FLOAT) {
        /* Target is floating point */
        if (size == 4)      { float v = (float)d_val; memcpy(ptr, &v, 4); }
        else if (size == 8) { memcpy(ptr, &d_val, 8); }
        else { nmo_dsl_value_destroy(&rhs); set_err(ev, "unsupported float field size"); return false; }
    } else if (target_kind == NMO_DSL_NUM_BOOL) {
        uint8_t v = (i_val != 0) ? 1 : 0;
        memcpy(ptr, &v, 1);
    } else {
        nmo_dsl_value_destroy(&rhs);
        set_err(ev, "cannot assign to non-numeric field type");
        return false;
    }

    nmo_dsl_value_destroy(&rhs);
    return true;
}

bool nmo_dsl_eval_stmt_list(nmo_dsl_eval_state_t *ev,
                             const nmo_dsl_stmt_t *head,
                             nmo_dsl_value_t *out_last_value) {
    if (out_last_value) {
        memset(out_last_value, 0, sizeof(*out_last_value));
    }

    for (const nmo_dsl_stmt_t *s = head; s; s = s->next) {
        switch (s->kind) {
            case NMO_DSL_STMT_ASSIGN:
                if (!eval_assign(ev, s->as.assign.target, s->as.assign.value))
                    return false;
                break;

            case NMO_DSL_STMT_EXPR: {
                if (out_last_value) {
                    nmo_dsl_value_destroy(out_last_value);
                }
                nmo_dsl_value_t v = {0};
                if (!nmo_dsl_eval_expr_impl(ev, s->as.expr, &v))
                    return false;
                if (out_last_value) {
                    *out_last_value = v;
                } else {
                    nmo_dsl_value_destroy(&v);
                }
                break;
            }

            default:
                set_err(ev, "unsupported statement kind in script");
                return false;
        }
    }

    return true;
}
