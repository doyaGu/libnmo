#include "dsl/nmo_dsl.h"
#include "nmo_dsl_eval.h"
#include "dsl/nmo_dsl_ast.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_guids.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * seq_array
 * ============================================================================ */

typedef struct {
    nmo_dsl_seq_t base;
    const nmo_type_descriptor_t *owner_type;
    const void *owner_instance;
    const void *array_ptr;
    uint64_t count;
    size_t elem_size;
    nmo_guid_t elem_guid;
    const nmo_type_descriptor_t *elem_type;
} nmo_dsl_seq_array_t;

static uint64_t seq_array_count(const nmo_dsl_seq_t *seq) {
    const nmo_dsl_seq_array_t *s = (const nmo_dsl_seq_array_t *)seq;
    return s ? s->count : 0;
}

static bool seq_array_get(const nmo_dsl_seq_t *seq, uint64_t index, nmo_dsl_value_t *out) {
    const nmo_dsl_seq_array_t *s = (const nmo_dsl_seq_array_t *)seq;
    if (!s || !out || !s->array_ptr || index >= s->count) return false;
    const uint8_t *ptr = (const uint8_t *)s->array_ptr + (size_t)index * s->elem_size;
    out->kind = NMO_DSL_VALUE_BYREF;
    out->as.byref.guid = s->elem_guid;
    out->as.byref.type = s->elem_type;
    out->as.byref.ptr = ptr;
    out->as.byref.size = s->elem_size;
    return true;
}

static void seq_array_destroy(nmo_dsl_seq_t *seq) {
    nmo_dsl_seq_array_t *s = (nmo_dsl_seq_array_t *)seq;
    if (!s) return;
    free(s);
}

static const nmo_dsl_seq_vtable_t g_seq_array_vt = {
    .count = seq_array_count,
    .get = seq_array_get,
    .destroy = seq_array_destroy,
};

nmo_dsl_seq_t *nmo_dsl_seq_array_create(
    const nmo_type_descriptor_t *owner_type, const void *owner_instance,
    const void *array_ptr, uint64_t count, size_t elem_size,
    nmo_guid_t elem_guid, const nmo_type_descriptor_t *elem_type)
{
    nmo_dsl_seq_array_t *s = (nmo_dsl_seq_array_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->base.vt = &g_seq_array_vt;
    s->owner_type = owner_type;
    s->owner_instance = owner_instance;
    s->array_ptr = array_ptr;
    s->count = count;
    s->elem_size = elem_size;
    s->elem_guid = elem_guid;
    s->elem_type = elem_type;
    return &s->base;
}

/* ============================================================================
 * seq_map_field
 * ============================================================================ */

static const nmo_type_descriptor_t *dsl_lookup_field_type(
    const nmo_type_registry_t *registry, nmo_guid_t field_guid)
{
    if (!registry) return NULL;
    return nmo_type_registry_find_by_guid(registry, field_guid);
}

typedef struct {
    nmo_dsl_seq_t base;
    nmo_dsl_seq_t *src;
    char *field;
    const nmo_type_registry_t *registry;
} nmo_dsl_seq_map_field_t;

static uint64_t seq_map_field_count(const nmo_dsl_seq_t *seq) {
    const nmo_dsl_seq_map_field_t *m = (const nmo_dsl_seq_map_field_t *)seq;
    return m ? nmo_dsl_seq_count(m->src) : 0;
}

static bool seq_map_field_get(const nmo_dsl_seq_t *seq, uint64_t index, nmo_dsl_value_t *out) {
    const nmo_dsl_seq_map_field_t *m = (const nmo_dsl_seq_map_field_t *)seq;
    if (!m || !out) return false;

    nmo_dsl_value_t elem = {0};
    if (!nmo_dsl_seq_get(m->src, index, &elem)) return false;
    bool ok = false;

    if (elem.kind == NMO_DSL_VALUE_OBJECT) {
        const nmo_type_field_t *f = nmo_type_get_field_by_name(elem.as.object.type, m->field);
        if (!f) goto done;
        const void *ptr = nmo_field_get_ptr_const(elem.as.object.instance, f);
        if (!ptr) {
            out->kind = NMO_DSL_VALUE_NULL;
            ok = true;
            goto done;
        }
        if (f->flags & NMO_FIELD_REPEATED) goto done;
        out->kind = NMO_DSL_VALUE_BYREF;
        out->as.byref.guid = f->type_guid;
        out->as.byref.type = dsl_lookup_field_type(m->registry, f->type_guid);
        out->as.byref.ptr = ptr;
        out->as.byref.size = f->size;
        ok = true;
        goto done;
    }

    if (elem.kind == NMO_DSL_VALUE_BYREF && elem.as.byref.type && nmo_type_has_reflection(elem.as.byref.type)) {
        const nmo_type_field_t *f = nmo_type_get_field_by_name(elem.as.byref.type, m->field);
        if (!f) goto done;
        const void *ptr = nmo_field_get_ptr_const(elem.as.byref.ptr, f);
        if (!ptr) {
            out->kind = NMO_DSL_VALUE_NULL;
            ok = true;
            goto done;
        }
        if (f->flags & NMO_FIELD_REPEATED) goto done;
        out->kind = NMO_DSL_VALUE_BYREF;
        out->as.byref.guid = f->type_guid;
        out->as.byref.type = dsl_lookup_field_type(m->registry, f->type_guid);
        out->as.byref.ptr = ptr;
        out->as.byref.size = f->size;
        ok = true;
        goto done;
    }

done:
    nmo_dsl_value_destroy(&elem);
    return ok;
}

static void seq_map_field_destroy(nmo_dsl_seq_t *seq) {
    nmo_dsl_seq_map_field_t *m = (nmo_dsl_seq_map_field_t *)seq;
    if (!m) return;
    nmo_dsl_seq_destroy(m->src);
    free(m->field);
    free(m);
}

static const nmo_dsl_seq_vtable_t g_seq_map_field_vt = {
    .count = seq_map_field_count,
    .get = seq_map_field_get,
    .destroy = seq_map_field_destroy,
};

nmo_dsl_seq_t *nmo_dsl_seq_map_field_create(
    nmo_dsl_seq_t *src, const char *field, const nmo_type_registry_t *registry)
{
    nmo_dsl_seq_map_field_t *m = (nmo_dsl_seq_map_field_t *)calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->base.vt = &g_seq_map_field_vt;
    m->src = src;
    size_t len = strlen(field);
    m->field = (char *)malloc(len + 1);
    if (!m->field) { free(m); return NULL; }
    memcpy(m->field, field, len + 1);
    m->registry = registry;
    return &m->base;
}

/* ============================================================================
 * seq_filter
 * ============================================================================ */

typedef struct {
    nmo_dsl_seq_t base;
    nmo_dsl_seq_t *src;
    const nmo_dsl_expr_t *pred;
    nmo_dsl_eval_state_t ev;
    uint64_t *match_indices;
    uint64_t match_count;
    bool materialized;
    bool materialize_ok;
} nmo_dsl_seq_filter_t;

/* forward decl for value_to_bool */
static bool dsl_value_to_bool(const nmo_dsl_value_t *v, bool *out);

static bool dsl_value_to_number(const nmo_dsl_value_t *v, double *out);

static bool dsl_value_to_bool(const nmo_dsl_value_t *v, bool *out) {
    if (!v || !out) return false;
    switch (v->kind) {
        case NMO_DSL_VALUE_BOOL: *out = v->as.b; return true;
        case NMO_DSL_VALUE_INT: *out = (v->as.i != 0); return true;
        case NMO_DSL_VALUE_UINT: *out = (v->as.u != 0); return true;
        case NMO_DSL_VALUE_REAL: *out = (v->as.r != 0.0); return true;
        case NMO_DSL_VALUE_BYREF: {
            double n = 0.0;
            if (!dsl_value_to_number(v, &n)) return false;
            *out = (n != 0.0);
            return true;
        }
        case NMO_DSL_VALUE_NULL: *out = false; return true;
        default: return false;
    }
}

static bool dsl_value_to_number(const nmo_dsl_value_t *v, double *out) {
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

static bool seq_filter_elem_matches(nmo_dsl_seq_filter_t *f, const nmo_dsl_value_t *elem, bool *out_match) {
    if (!f || !elem || !out_match) return false;

    nmo_dsl_eval_context_t sub = *f->ev.ctx;

    if (elem->kind == NMO_DSL_VALUE_OBJECT) {
        sub.current_type = elem->as.object.type;
        sub.current_instance = elem->as.object.instance;
    } else if (elem->kind == NMO_DSL_VALUE_BYREF && elem->as.byref.type && nmo_type_has_reflection(elem->as.byref.type)) {
        sub.current_type = elem->as.byref.type;
        sub.current_instance = elem->as.byref.ptr;
    } else {
        *out_match = false;
        return true;
    }

    nmo_dsl_eval_state_t ev2 = { .ctx = &sub };
    nmo_dsl_value_t v = {0};
    if (!nmo_dsl_eval_expr_impl(&ev2, f->pred, &v)) {
        nmo_dsl_value_destroy(&v);
        if (ev2.err[0]) {
            (void)snprintf(f->ev.err, sizeof(f->ev.err), "%s", ev2.err);
        }
        return false;
    }

    bool b = false;
    bool ok = dsl_value_to_bool(&v, &b);
    nmo_dsl_value_destroy(&v);
    if (!ok) {
        (void)snprintf(f->ev.err, sizeof(f->ev.err), "filter predicate must be boolean");
        return false;
    }

    *out_match = b;
    return true;
}

static bool seq_filter_materialize(nmo_dsl_seq_filter_t *f) {
    if (!f) return false;
    if (f->materialized) return f->materialize_ok;

    f->materialized = true;
    f->materialize_ok = false;
    uint64_t n = nmo_dsl_seq_count(f->src);
    if (n == 0) {
        f->match_count = 0;
        f->materialize_ok = true;
        return true;
    }

    uint64_t *indices = (uint64_t *)malloc((size_t)n * sizeof(*indices));
    if (!indices) {
        (void)snprintf(f->ev.err, sizeof(f->ev.err), "oom");
        return false;
    }

    uint64_t cnt = 0;
    for (uint64_t i = 0; i < n; ++i) {
        nmo_dsl_value_t elem = {0};
        if (!nmo_dsl_seq_get(f->src, i, &elem)) continue;
        bool match = false;
        if (!seq_filter_elem_matches(f, &elem, &match)) {
            nmo_dsl_value_destroy(&elem);
            free(indices);
            return false;
        }
        nmo_dsl_value_destroy(&elem);
        if (match) {
            indices[cnt++] = i;
        }
    }
    f->match_indices = indices;
    f->match_count = cnt;
    f->materialize_ok = true;
    return true;
}

static uint64_t seq_filter_count(const nmo_dsl_seq_t *seq) {
    nmo_dsl_seq_filter_t *f = (nmo_dsl_seq_filter_t *)seq;
    if (!f) return 0;
    if (!seq_filter_materialize(f)) return 0;
    return f->match_count;
}

static bool seq_filter_get(const nmo_dsl_seq_t *seq, uint64_t index, nmo_dsl_value_t *out) {
    nmo_dsl_seq_filter_t *f = (nmo_dsl_seq_filter_t *)seq;
    if (!f || !out) return false;
    if (!seq_filter_materialize(f)) return false;
    if (index >= f->match_count) return false;
    return nmo_dsl_seq_get(f->src, f->match_indices[index], out);
}

static void seq_filter_destroy(nmo_dsl_seq_t *seq) {
    nmo_dsl_seq_filter_t *f = (nmo_dsl_seq_filter_t *)seq;
    if (!f) return;
    nmo_dsl_seq_destroy(f->src);
    free(f->match_indices);
    free(f);
}

static const nmo_dsl_seq_vtable_t g_seq_filter_vt = {
    .count = seq_filter_count,
    .get = seq_filter_get,
    .destroy = seq_filter_destroy,
};

nmo_dsl_seq_t *nmo_dsl_seq_filter_create(
    nmo_dsl_seq_t *src, const nmo_dsl_expr_t *pred, nmo_dsl_eval_state_t *ev)
{
    if (!src || !pred || !ev) return NULL;
    nmo_dsl_seq_filter_t *f = (nmo_dsl_seq_filter_t *)calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->base.vt = &g_seq_filter_vt;
    f->src = src;
    f->pred = pred;
    f->ev = *ev;
    if (!seq_filter_materialize(f)) {
        if (f->ev.err[0]) {
            (void)snprintf(ev->err, sizeof(ev->err), "%s", f->ev.err);
        }
        free(f);
        return NULL;
    }
    return &f->base;
}

/* ============================================================================
 * seq_slice
 * ============================================================================ */

typedef struct {
    nmo_dsl_seq_t base;
    nmo_dsl_seq_t *src;
    uint64_t start;
    uint64_t end;
} nmo_dsl_seq_slice_t;

static uint64_t seq_slice_count(const nmo_dsl_seq_t *seq) {
    const nmo_dsl_seq_slice_t *s = (const nmo_dsl_seq_slice_t *)seq;
    if (!s) return 0;
    uint64_t n = nmo_dsl_seq_count(s->src);
    uint64_t st = (s->start > n) ? n : s->start;
    uint64_t en = (s->end > n) ? n : s->end;
    if (en < st) return 0;
    return en - st;
}

static bool seq_slice_get(const nmo_dsl_seq_t *seq, uint64_t index, nmo_dsl_value_t *out) {
    const nmo_dsl_seq_slice_t *s = (const nmo_dsl_seq_slice_t *)seq;
    if (!s || !out) return false;
    uint64_t n = nmo_dsl_seq_count(s->src);
    uint64_t st = (s->start > n) ? n : s->start;
    uint64_t en = (s->end > n) ? n : s->end;
    if (en < st) return false;
    if (index >= en - st) return false;
    return nmo_dsl_seq_get(s->src, st + index, out);
}

static void seq_slice_destroy(nmo_dsl_seq_t *seq) {
    nmo_dsl_seq_slice_t *s = (nmo_dsl_seq_slice_t *)seq;
    if (!s) return;
    nmo_dsl_seq_destroy(s->src);
    free(s);
}

static const nmo_dsl_seq_vtable_t g_seq_slice_vt = {
    .count = seq_slice_count,
    .get = seq_slice_get,
    .destroy = seq_slice_destroy,
};

nmo_dsl_seq_t *nmo_dsl_seq_slice_create(nmo_dsl_seq_t *src, uint64_t start, uint64_t end)
{
    nmo_dsl_seq_slice_t *s = (nmo_dsl_seq_slice_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->base.vt = &g_seq_slice_vt;
    s->src = src;
    s->start = start;
    s->end = end;
    return &s->base;
}
