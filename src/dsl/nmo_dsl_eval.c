#include "nmo_dsl_eval.h"
#include "dsl/nmo_dsl.h"
#include "dsl/nmo_dsl_ast.h"
#include "type/nmo_reflection.h"
#include "type/nmo_builtin_type_guids.h"
#include "type/nmo_type_string.h"
#include "type/nmo_operation_system.h"
#include "core/nmo_guid.h"

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
    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_guid(registry, field_guid);
    if (!t && nmo_guid_is_field_type(field_guid)) {
        nmo_guid_t mapped = nmo_guid_field_to_type(field_guid);
        t = nmo_type_registry_find_by_guid(registry, mapped);
    }
    return t;
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
            if (!nmo_guid_is_field_type(v->as.byref.guid)) return false;
            uint32_t field_class = (uint32_t)(v->as.byref.guid.d1 & 0xFFu);
            uint32_t size_bits = (uint32_t)(v->as.byref.guid.d2 >> 16);
            size_t size_bytes = v->as.byref.size;
            if (size_bits != 0) size_bytes = (size_t)((size_bits + 7u) / 8u);

            if (field_class == NMO_GUID_FIELD_CLASS_BOOL) {
                if (size_bytes != 1) return false;
                *out = (*(const uint8_t *)v->as.byref.ptr) ? 1.0 : 0.0;
                return true;
            }
            if (field_class == NMO_GUID_FIELD_CLASS_INT) {
                int64_t x = 0;
                if (size_bytes == 1) x = *(const int8_t *)v->as.byref.ptr;
                else if (size_bytes == 2) x = *(const int16_t *)v->as.byref.ptr;
                else if (size_bytes == 4) x = *(const int32_t *)v->as.byref.ptr;
                else if (size_bytes == 8) x = *(const int64_t *)v->as.byref.ptr;
                else return false;
                *out = (double)x;
                return true;
            }
            if (field_class == NMO_GUID_FIELD_CLASS_UINT || field_class == NMO_GUID_FIELD_CLASS_OBJECT_ID) {
                uint64_t x = 0;
                if (size_bytes == 1) x = *(const uint8_t *)v->as.byref.ptr;
                else if (size_bytes == 2) x = *(const uint16_t *)v->as.byref.ptr;
                else if (size_bytes == 4) x = *(const uint32_t *)v->as.byref.ptr;
                else if (size_bytes == 8) x = *(const uint64_t *)v->as.byref.ptr;
                else return false;
                *out = (double)x;
                return true;
            }
            if (field_class == NMO_GUID_FIELD_CLASS_FLOAT) {
                if (size_bytes == 4) *out = (double)*(const float *)v->as.byref.ptr;
                else if (size_bytes == 8) *out = *(const double *)v->as.byref.ptr;
                else return false;
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
    const void *array_ptr = *(const void *const *)ptr;
    uint64_t count = ctx->guess_array_count
        ? ctx->guess_array_count(owner_type, owner_instance, f, ctx->guess_array_count_user) : 0;
    size_t elem_size = 0;
    const nmo_type_descriptor_t *elem_type = lookup_field_type(ctx->registry, f->type_guid);
    if (elem_type && elem_type->size > 0) {
        elem_size = (size_t)elem_type->size;
    } else {
        uint32_t size_bits = (uint32_t)(f->type_guid.d2 & 0xFFFFu);
        elem_size = (size_bits > 0) ? (size_t)((size_bits + 7u) / 8u) : sizeof(uint32_t);
    }

    nmo_dsl_seq_t *seq = nmo_dsl_seq_array_create(
        owner_type, owner_instance, array_ptr, count, elem_size, f->type_guid, elem_type);
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

        /* For small types, parse into stack buffer and return as primitive */
        if (type->size <= 8 && type->size > 0) {
            uint8_t tmp[8] = {0};
            nmo_status_t st = nmo_type_value_from_string(tmp, type,
                ev->ctx->registry, args[1].as.s);
            if (st != NMO_OK) { set_err(ev, "from_string: parse failed"); goto fail; }

            nmo_guid_t g = type->guid;
            if (nmo_guid_equals(g, NMO_TYPE_GUID_INT)) {
                out->kind = NMO_DSL_VALUE_INT;
                out->as.i = *(int32_t *)tmp;
            } else if (nmo_guid_equals(g, NMO_TYPE_GUID_FLOAT)) {
                out->kind = NMO_DSL_VALUE_REAL;
                out->as.r = *(float *)tmp;
            } else if (nmo_guid_equals(g, NMO_TYPE_GUID_DOUBLE)) {
                out->kind = NMO_DSL_VALUE_REAL;
                out->as.r = *(double *)tmp;
            } else if (nmo_guid_equals(g, NMO_TYPE_GUID_BOOL)) {
                out->kind = NMO_DSL_VALUE_BOOL;
                out->as.b = *(bool *)tmp;
            } else {
                /* Return as BYREF for other types */
                out->kind = NMO_DSL_VALUE_INT;
                out->as.i = 0;
                memcpy(&out->as.i, tmp, type->size < 8 ? type->size : 8);
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
        const nmo_operation_registry_t *ops = ev->ctx->ops;
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
        uint8_t p1_buf[16] = {0}, p2_buf[16] = {0}, result_buf[16] = {0};
        const nmo_type_descriptor_t *p1_type = NULL;
        const nmo_type_descriptor_t *p2_type = NULL;
        void *p1_ptr = NULL, *p2_ptr = NULL;

        /* P1 */
        if (args[1].kind == NMO_DSL_VALUE_INT) {
            int32_t iv = (int32_t)args[1].as.i;
            memcpy(p1_buf, &iv, sizeof(iv));
            p1_type = nmo_type_registry_find_by_guid(ev->ctx->registry, NMO_TYPE_GUID_INT);
            p1_ptr = p1_buf;
        } else if (args[1].kind == NMO_DSL_VALUE_REAL) {
            float fv = (float)args[1].as.r;
            memcpy(p1_buf, &fv, sizeof(fv));
            p1_type = nmo_type_registry_find_by_guid(ev->ctx->registry, NMO_TYPE_GUID_FLOAT);
            p1_ptr = p1_buf;
        } else if (args[1].kind == NMO_DSL_VALUE_BOOL) {
            int32_t bv = args[1].as.b ? 1 : 0;
            memcpy(p1_buf, &bv, sizeof(bv));
            p1_type = nmo_type_registry_find_by_guid(ev->ctx->registry, NMO_TYPE_GUID_BOOL);
            p1_ptr = p1_buf;
        } else if (args[1].kind == NMO_DSL_VALUE_BYREF &&
                   args[1].as.byref.type && args[1].as.byref.ptr) {
            p1_type = args[1].as.byref.type;
            p1_ptr = (void *)args[1].as.byref.ptr;
        } else {
            set_err(ev, "op: unsupported p1 type"); goto fail;
        }

        /* P2 (optional) */
        if (call->arg_count == 3) {
            if (args[2].kind == NMO_DSL_VALUE_INT) {
                int32_t iv = (int32_t)args[2].as.i;
                memcpy(p2_buf, &iv, sizeof(iv));
                p2_type = nmo_type_registry_find_by_guid(ev->ctx->registry, NMO_TYPE_GUID_INT);
                p2_ptr = p2_buf;
            } else if (args[2].kind == NMO_DSL_VALUE_REAL) {
                float fv = (float)args[2].as.r;
                memcpy(p2_buf, &fv, sizeof(fv));
                p2_type = nmo_type_registry_find_by_guid(ev->ctx->registry, NMO_TYPE_GUID_FLOAT);
                p2_ptr = p2_buf;
            } else if (args[2].kind == NMO_DSL_VALUE_BOOL) {
                int32_t bv = args[2].as.b ? 1 : 0;
                memcpy(p2_buf, &bv, sizeof(bv));
                p2_type = nmo_type_registry_find_by_guid(ev->ctx->registry, NMO_TYPE_GUID_BOOL);
                p2_ptr = p2_buf;
            } else if (args[2].kind == NMO_DSL_VALUE_BYREF &&
                       args[2].as.byref.type && args[2].as.byref.ptr) {
                p2_type = args[2].as.byref.type;
                p2_ptr = (void *)args[2].as.byref.ptr;
            } else {
                set_err(ev, "op: unsupported p2 type"); goto fail;
            }
        }

        nmo_status_t st = nmo_operation_registry_execute(
            (nmo_operation_registry_t *)ops,
            &fam->operation_guid,
            p1_ptr, p1_type,
            p2_ptr, p2_type,
            result_buf, NULL,
            ev->ctx->registry);
        if (st != NMO_OK) { set_err(ev, "op: execution failed"); goto fail; }

        /* Convert result to DSL value. Best effort: check result type. */
        /* The operation result type is the same as p1 for most arith ops */
        if (p1_type && nmo_guid_equals(p1_type->guid, NMO_TYPE_GUID_INT)) {
            out->kind = NMO_DSL_VALUE_INT;
            int32_t rv;
            memcpy(&rv, result_buf, sizeof(rv));
            out->as.i = rv;
        } else if (p1_type && nmo_guid_equals(p1_type->guid, NMO_TYPE_GUID_FLOAT)) {
            out->kind = NMO_DSL_VALUE_REAL;
            float rv;
            memcpy(&rv, result_buf, sizeof(rv));
            out->as.r = rv;
        } else if (p1_type && nmo_guid_equals(p1_type->guid, NMO_TYPE_GUID_BOOL)) {
            out->kind = NMO_DSL_VALUE_BOOL;
            int32_t rv;
            memcpy(&rv, result_buf, sizeof(rv));
            out->as.b = (rv != 0);
        } else {
            /* Fallback: return as int */
            out->kind = NMO_DSL_VALUE_INT;
            int32_t rv;
            memcpy(&rv, result_buf, sizeof(rv));
            out->as.i = rv;
        }
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
    size_t *out_elem_size,
    nmo_guid_t *out_elem_guid,
    const nmo_type_descriptor_t **out_owner_type,
    const void **out_owner_instance,
    const nmo_type_field_t **out_field)
{
    const nmo_dsl_eval_context_t *ctx = ev->ctx;
    if (!ctx || !base_expr || !out_array_ptr || !out_elem_size || !out_elem_guid ||
        !out_owner_type || !out_owner_instance || !out_field) {
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

    void *array_ptr = *(void **)field_ptr;
    if (!array_ptr) {
        set_err(ev, "null array pointer");
        return false;
    }

    const nmo_type_descriptor_t *elem_type = lookup_field_type(ctx->registry, f->type_guid);
    size_t elem_size = 0;
    if (elem_type && elem_type->size > 0) {
        elem_size = (size_t)elem_type->size;
    } else {
        uint32_t size_bits = (uint32_t)(f->type_guid.d2 & 0xFFFFu);
        elem_size = (size_bits > 0) ? (size_t)((size_bits + 7u) / 8u) : sizeof(uint32_t);
    }

    *out_array_ptr = array_ptr;
    *out_elem_size = elem_size;
    *out_elem_guid = f->type_guid;
    *out_owner_type = owner_type;
    *out_owner_instance = owner_instance;
    *out_field = f;
    return true;
}

static bool resolve_mutable_target(nmo_dsl_eval_state_t *ev,
                                    const nmo_dsl_expr_t *target,
                                    void **out_ptr, size_t *out_size,
                                    nmo_guid_t *out_guid) {
    const nmo_dsl_eval_context_t *ctx = ev->ctx;

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
        size_t elem_size = 0;
        nmo_guid_t elem_guid = NMO_GUID_NULL;
        const nmo_type_descriptor_t *owner_type = NULL;
        const void *owner_instance = NULL;
        const nmo_type_field_t *field = NULL;

        if (!resolve_mutable_repeated_field(ev, target->as.index.base,
                                            &array_ptr, &elem_size, &elem_guid,
                                            &owner_type, &owner_instance, &field)) {
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

        uint64_t count = ctx->guess_array_count
            ? ctx->guess_array_count(owner_type, owner_instance, field, ctx->guess_array_count_user) : 0;
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

    /* Convert RHS to a numeric value for type-aware writing */
    uint8_t target_class = (uint8_t)(guid.d1 & 0xFFu);

    /* First, get a numeric representation of the RHS */
    double d_val = 0.0;
    int64_t i_val = 0;
    bool have_number = false;

    if (rhs.kind == NMO_DSL_VALUE_INT) {
        i_val = rhs.as.i;
        d_val = (double)rhs.as.i;
        have_number = true;
    } else if (rhs.kind == NMO_DSL_VALUE_UINT) {
        i_val = (int64_t)rhs.as.u;
        d_val = (double)rhs.as.u;
        have_number = true;
    } else if (rhs.kind == NMO_DSL_VALUE_REAL) {
        i_val = (int64_t)rhs.as.r;
        d_val = rhs.as.r;
        have_number = true;
    } else if (rhs.kind == NMO_DSL_VALUE_BOOL) {
        i_val = rhs.as.b ? 1 : 0;
        d_val = rhs.as.b ? 1.0 : 0.0;
        have_number = true;
    } else if (rhs.kind == NMO_DSL_VALUE_BYREF && rhs.as.byref.ptr) {
        bool direct_copy_ok = false;
        if (nmo_guid_equals(rhs.as.byref.guid, guid) && rhs.as.byref.size == size) {
            direct_copy_ok = true;
        } else {
            const nmo_type_descriptor_t *target_type =
                lookup_field_type(ev->ctx->registry, guid);
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
        if (value_to_number(&rhs, &d_val)) {
            i_val = (int64_t)d_val;
            have_number = true;
        }
    }

    if (!have_number) {
        nmo_dsl_value_destroy(&rhs);
        set_err(ev, "cannot assign this value type");
        return false;
    }

    /* Write based on target type class */
    if (target_class == NMO_GUID_FIELD_CLASS_INT) {
        /* Target is signed integer */
        if (size == 4)      { int32_t v = (int32_t)i_val; memcpy(ptr, &v, 4); }
        else if (size == 8) { memcpy(ptr, &i_val, 8); }
        else if (size == 2) { int16_t v = (int16_t)i_val; memcpy(ptr, &v, 2); }
        else if (size == 1) { int8_t v = (int8_t)i_val; memcpy(ptr, &v, 1); }
        else { nmo_dsl_value_destroy(&rhs); set_err(ev, "unsupported int field size"); return false; }
    } else if (target_class == NMO_GUID_FIELD_CLASS_UINT ||
               target_class == NMO_GUID_FIELD_CLASS_OBJECT_ID) {
        /* Target is unsigned integer */
        uint64_t u_val = (uint64_t)i_val;
        if (rhs.kind == NMO_DSL_VALUE_UINT) u_val = rhs.as.u;
        else if (rhs.kind == NMO_DSL_VALUE_REAL) u_val = (uint64_t)d_val;
        if (size == 4)      { uint32_t v = (uint32_t)u_val; memcpy(ptr, &v, 4); }
        else if (size == 8) { memcpy(ptr, &u_val, 8); }
        else if (size == 2) { uint16_t v = (uint16_t)u_val; memcpy(ptr, &v, 2); }
        else if (size == 1) { uint8_t v = (uint8_t)u_val; memcpy(ptr, &v, 1); }
        else { nmo_dsl_value_destroy(&rhs); set_err(ev, "unsupported uint field size"); return false; }
    } else if (target_class == NMO_GUID_FIELD_CLASS_FLOAT) {
        /* Target is floating point */
        if (size == 4)      { float v = (float)d_val; memcpy(ptr, &v, 4); }
        else if (size == 8) { memcpy(ptr, &d_val, 8); }
        else { nmo_dsl_value_destroy(&rhs); set_err(ev, "unsupported float field size"); return false; }
    } else if (target_class == NMO_GUID_FIELD_CLASS_BOOL) {
        uint8_t v = (i_val != 0) ? 1 : 0;
        memcpy(ptr, &v, 1);
    } else {
        /* Unknown type class — try raw size-based write */
        if (size <= 8) {
            memcpy(ptr, &i_val, size < 8 ? size : 8);
        } else {
            nmo_dsl_value_destroy(&rhs);
            set_err(ev, "cannot assign to this field type");
            return false;
        }
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
