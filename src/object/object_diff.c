#include "object/nmo_object_diff.h"

#include "../runtime/runtime_internal.h"
#include "core/nmo_allocator.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include "core/nmo_guid.h"
#include "core/nmo_hash.h"
#include "document/nmo_document.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_string.h"
#include "type/nmo_type_view.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    nmo_context_t *ctx;
    nmo_type_registry_t *registry;
    nmo_object_repository_t *repo;
} diff_side_t;

typedef struct {
    nmo_allocator_t *allocator;
    nmo_object_id_t *ids1;
    nmo_object_id_t *ids2;
    size_t count;
} match_lookup_t;

typedef struct {
    uint32_t other;
    nmo_class_id_t other_class;
    nmo_ref_kind_t kind;
} topo_edge_t;

typedef struct {
    nmo_object_t *obj;
    nmo_object_id_t id;
    nmo_class_id_t cid;
    const char *name;
    uint64_t sig;
    size_t out_start;
    size_t out_count;
    size_t in_start;
    size_t in_count;
} diff_node_t;

typedef struct {
    diff_side_t side;
    nmo_allocator_t *allocator;
    diff_node_t *nodes;
    size_t count;
    topo_edge_t *out_edges;
    topo_edge_t *in_edges;
} graph_side_t;

typedef struct {
    nmo_allocator_t *allocator;
    int *to2_by1;
    int *to1_by2;
    float *sim_by1;
    uint8_t *anchor_by1;
    size_t n1;
    size_t n2;
} match_state_t;

typedef struct {
    nmo_class_id_t cid;
    uint32_t *idx1;
    size_t count1;
    size_t cap1;
    uint32_t *idx2;
    size_t count2;
    size_t cap2;
} class_bucket_t;

typedef struct {
    nmo_allocator_t *allocator;
    class_bucket_t *items;
    size_t count;
    size_t cap;
} class_bucket_array_t;

typedef struct {
    nmo_class_id_t cid;
    uint32_t *u1;
    size_t n1;
    uint32_t *u2;
    size_t n2;
    float *s0;
    float *s;
    float *tmp;
} class_matrix_t;

typedef struct {
    nmo_allocator_t *allocator;
    class_matrix_t *items;
    size_t count;
    size_t cap;
} class_matrix_array_t;

typedef struct {
    const graph_side_t *g1;
    const graph_side_t *g2;
    const match_state_t *match;
    const class_matrix_array_t *mats;
    int *slot1;
    int *slot2;
    int *pos1;
    int *pos2;
} flood_env_t;

typedef struct {
    void *raw;
    size_t size;
} diff_alloc_header_t;

static bool diff_is_pow2(size_t x)
{
    return x != 0 && (x & (x - 1u)) == 0;
}

static bool diff_mul_size(size_t a, size_t b, size_t *out)
{
    if (!out) return false;
    if (a != 0 && b > SIZE_MAX / a) return false;
    *out = a * b;
    return true;
}

static void *diff_alloc(nmo_allocator_t *allocator, size_t size, size_t alignment, bool zeroed)
{
    if (!allocator || !allocator->alloc || !allocator->free || size == 0) return NULL;

    if (alignment == 0) alignment = _Alignof(max_align_t);
    if (!diff_is_pow2(alignment)) return NULL;

    size_t hdr = sizeof(diff_alloc_header_t);
    size_t extra = alignment - 1u;
    if (size > SIZE_MAX - hdr - extra) return NULL;
    size_t total = size + hdr + extra;

    void *raw = nmo_alloc(allocator, total, _Alignof(max_align_t));
    if (!raw) return NULL;

    uintptr_t start = (uintptr_t)raw + hdr;
    uintptr_t aligned = (start + extra) & ~(uintptr_t)extra;
    diff_alloc_header_t *h = (diff_alloc_header_t *)(aligned - hdr);
    h->raw = raw;
    h->size = size;

    void *ptr = (void *)aligned;
    if (zeroed) memset(ptr, 0, size);
    return ptr;
}

static void *diff_alloc_array(nmo_allocator_t *allocator,
                              size_t count,
                              size_t elem_size,
                              size_t alignment,
                              bool zeroed)
{
    size_t bytes = 0;
    if (count == 0) return NULL;
    if (!diff_mul_size(count, elem_size, &bytes)) return NULL;
    return diff_alloc(allocator, bytes, alignment, zeroed);
}

static size_t diff_alloc_size(const void *ptr)
{
    if (!ptr) return 0;
    const diff_alloc_header_t *h =
        (const diff_alloc_header_t *)((const uint8_t *)ptr - sizeof(diff_alloc_header_t));
    return h->size;
}

static void diff_free(nmo_allocator_t *allocator, void *ptr)
{
    if (!allocator || !ptr) return;
    diff_alloc_header_t *h = (diff_alloc_header_t *)((uint8_t *)ptr - sizeof(diff_alloc_header_t));
    nmo_free(allocator, h->raw);
}

static void *diff_realloc(nmo_allocator_t *allocator,
                          void *ptr,
                          size_t new_size,
                          size_t alignment)
{
    if (!ptr) return diff_alloc(allocator, new_size, alignment, false);
    if (new_size == 0) {
        diff_free(allocator, ptr);
        return NULL;
    }

    size_t old_size = diff_alloc_size(ptr);
    void *nptr = diff_alloc(allocator, new_size, alignment, false);
    if (!nptr) return NULL;
    size_t cp = old_size < new_size ? old_size : new_size;
    if (cp) memcpy(nptr, ptr, cp);
    diff_free(allocator, ptr);
    return nptr;
}

static void *diff_realloc_array(nmo_allocator_t *allocator,
                                void *ptr,
                                size_t count,
                                size_t elem_size,
                                size_t alignment)
{
    size_t bytes = 0;
    if (count == 0) {
        diff_free(allocator, ptr);
        return NULL;
    }
    if (!diff_mul_size(count, elem_size, &bytes)) return NULL;
    return diff_realloc(allocator, ptr, bytes, alignment);
}

static const char *norm_name(const char *name) {
    return (name && name[0]) ? name : "";
}

static const nmo_type_descriptor_t *resolve_object_type(
    const nmo_type_registry_t *registry,
    const nmo_object_t *obj)
{
    if (!registry || !obj) return NULL;
    nmo_guid_t type_guid = nmo_object_get_type_guid(obj);
    if (!nmo_guid_is_null(type_guid)) {
        const nmo_type_descriptor_t *t = nmo_type_registry_find_by_guid(registry, type_guid);
        if (t) return t;
    }
    return nmo_type_registry_find_by_class_id_inherited(registry, nmo_object_get_class_id(obj));
}

static bool is_base_embedding(const nmo_type_descriptor_t *owner,
                              const nmo_type_field_t *field)
{
    if (!owner || !field) return false;
    if (field->flags & NMO_FIELD_REPEATED) return false;
    if (nmo_guid_is_null(owner->base_type)) return false;
    if (nmo_guid_equals(field->type_guid, owner->base_type)) return true;
    if (nmo_guid_equals(field->type_guid, CKPGUID_NONE)) {
        if (strcmp(field->name, "base") == 0 ||
            strcmp(field->name, "entity") == 0 ||
            strcmp(field->name, "beobject") == 0 ||
            strcmp(field->name, "object") == 0) {
            return true;
        }
    }
    return false;
}

static bool is_object_ref(const nmo_type_field_t *field)
{
    if (!field) return false;
    if (field->flags & NMO_FIELD_REFERENCE) return true;
    if (field->semantic == NMO_SEMANTIC_OBJECT_REF) return true;
    return nmo_guid_equals(field->type_guid, CKPGUID_ID);
}

static void match_lookup_reset(match_lookup_t *lookup)
{
    if (!lookup) return;
    diff_free(lookup->allocator, lookup->ids1);
    diff_free(lookup->allocator, lookup->ids2);
    memset(lookup, 0, sizeof(*lookup));
}

static nmo_object_id_t match_lookup_map_left(const match_lookup_t *lookup, nmo_object_id_t id1)
{
    if (!lookup || id1 == 0) return 0;
    for (size_t i = 0; i < lookup->count; i++) {
        if (lookup->ids1[i] == id1) return lookup->ids2[i];
    }
    return 0;
}

bool nmo_object_ref_equal(nmo_object_id_t id1, nmo_object_id_t id2,
                          const nmo_object_repository_t *repo1,
                          const nmo_object_repository_t *repo2)
{
    if (id1 == 0 && id2 == 0) return true;
    if (id1 == 0 || id2 == 0) return false;
    nmo_object_t *obj1 = nmo_object_repository_find_by_id(repo1, id1);
    nmo_object_t *obj2 = nmo_object_repository_find_by_id(repo2, id2);
    if (!obj1 || !obj2) return false;
    if (nmo_object_get_class_id(obj1) != nmo_object_get_class_id(obj2)) return false;
    return strcmp(norm_name(nmo_object_get_name(obj1)), norm_name(nmo_object_get_name(obj2))) == 0;
}

void nmo_object_format_path(char *buf, size_t buf_size,
                            nmo_context_t *ctx,
                            const nmo_object_t *obj)
{
    if (!buf || buf_size == 0) return;
    if (!obj) {
        snprintf(buf, buf_size, "(null)");
        return;
    }
    const char *class_name = "Unknown";
    if (ctx) {
        nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
        if (registry) {
            nmo_type_view_t type_view;
            if (nmo_type_view_from_object(registry, obj, &type_view) == NMO_OK &&
                type_view.name && type_view.name[0]) {
                class_name = type_view.name;
            }
        }
    }
    const char *name = norm_name(nmo_object_get_name(obj));
    if (!name[0]) name = "(unnamed)";
    snprintf(buf, buf_size, "%s/%s", class_name, name);
}

void nmo_object_format_ref(char *buf, size_t buf_size,
                           nmo_object_id_t id,
                           const nmo_object_repository_t *repo,
                           nmo_context_t *ctx)
{
    if (!buf || buf_size == 0) return;
    if (id == 0) {
        snprintf(buf, buf_size, "(null)");
        return;
    }
    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, id);
    if (!obj) {
        snprintf(buf, buf_size, "(unknown #%u)", id);
        return;
    }
    nmo_object_format_path(buf, buf_size, ctx, obj);
}

static bool ref_equal_lookup(nmo_object_id_t id1, nmo_object_id_t id2,
                             const nmo_object_repository_t *repo1,
                             const nmo_object_repository_t *repo2,
                             const match_lookup_t *lookup)
{
    if (id1 == 0 && id2 == 0) return true;
    if (id1 == 0 || id2 == 0) return false;
    if (lookup) {
        nmo_object_id_t m = match_lookup_map_left(lookup, id1);
        if (m != 0) return m == id2;
    }
    return nmo_object_ref_equal(id1, id2, repo1, repo2);
}

static bool scalar_equal(const nmo_type_field_t *f,
                         const void *p1, const void *p2,
                         const nmo_object_repository_t *repo1,
                         const nmo_object_repository_t *repo2,
                         const match_lookup_t *lookup,
                         const nmo_type_registry_t *registry)
{
    if (!f) return false;
    if (!p1 || !p2) return p1 == p2;

    /* Pointer fields: compare pointed-to data, not pointer addresses.
     * Resolve the pointee type via registry to get the real struct size. */
    if (f->flags & NMO_FIELD_POINTER) {
        const void *a_ptr = *(const void *const *)p1;
        const void *b_ptr = *(const void *const *)p2;
        if (a_ptr == NULL && b_ptr == NULL) return true;
        if (a_ptr == NULL || b_ptr == NULL) return false;
        const nmo_type_descriptor_t *pointee = registry
            ? nmo_type_registry_find_by_guid(registry, f->type_guid) : NULL;
        size_t cmp_size = (pointee && pointee->size > 0) ? pointee->size : f->size;
        return memcmp(a_ptr, b_ptr, cmp_size) == 0;
    }

    if (is_object_ref(f) && f->size >= sizeof(uint32_t) && !(f->flags & NMO_FIELD_REPEATED)) {
        nmo_object_id_t id1 = *(const nmo_object_id_t *)p1;
        nmo_object_id_t id2 = *(const nmo_object_id_t *)p2;
        return ref_equal_lookup(id1, id2, repo1, repo2, lookup);
    }
    if (nmo_guid_equals(f->type_guid, CKPGUID_STRING) && f->size == sizeof(char *)) {
        const char *s1 = *(const char *const *)p1;
        const char *s2 = *(const char *const *)p2;
        if (!s1 || !s2) return s1 == s2;
        return strcmp(s1, s2) == 0;
    }
    return memcmp(p1, p2, f->size) == 0;
}

static bool repeated_equal(const nmo_type_field_t *f1,
                           const nmo_type_field_t *f2,
                           const void *p1, const void *p2,
                           const nmo_object_repository_t *repo1,
                           const nmo_object_repository_t *repo2,
                           const match_lookup_t *lookup)
{
    if (!f1 || !f2) return false;
    if (f1->size != sizeof(nmo_array_t) || f2->size != sizeof(nmo_array_t) || !p1 || !p2) return true;

    const nmo_array_t *a1 = (const nmo_array_t *)p1;
    const nmo_array_t *a2 = (const nmo_array_t *)p2;
    if (a1->count != a2->count || a1->element_size != a2->element_size) return false;
    if (!a1->count || !a1->element_size) return true;
    if (!a1->data || !a2->data) return a1->data == a2->data;

    if (is_object_ref(f1) && a1->element_size == sizeof(nmo_object_id_t)) {
        const nmo_object_id_t *ids1 = (const nmo_object_id_t *)a1->data;
        const nmo_object_id_t *ids2 = (const nmo_object_id_t *)a2->data;
        for (size_t i = 0; i < a1->count; i++) {
            if (!ref_equal_lookup(ids1[i], ids2[i], repo1, repo2, lookup)) return false;
        }
        return true;
    }
    return memcmp(a1->data, a2->data, a1->count * a1->element_size) == 0;
}

static bool scalar_equal_noref(const nmo_type_field_t *f, const void *p1, const void *p2,
                               const nmo_type_registry_t *registry)
{
    if (!f) return false;
    if (!p1 || !p2) return p1 == p2;
    if (is_object_ref(f)) return true;
    /* Pointer fields: compare pointed-to data, not pointer addresses */
    if (f->flags & NMO_FIELD_POINTER) {
        const void *a_ptr = *(const void *const *)p1;
        const void *b_ptr = *(const void *const *)p2;
        if (a_ptr == NULL && b_ptr == NULL) return true;
        if (a_ptr == NULL || b_ptr == NULL) return false;
        const nmo_type_descriptor_t *pointee = registry
            ? nmo_type_registry_find_by_guid(registry, f->type_guid) : NULL;
        size_t cmp_size = (pointee && pointee->size > 0) ? pointee->size : f->size;
        return memcmp(a_ptr, b_ptr, cmp_size) == 0;
    }
    if (nmo_guid_equals(f->type_guid, CKPGUID_STRING) && f->size == sizeof(char *)) {
        const char *s1 = *(const char *const *)p1;
        const char *s2 = *(const char *const *)p2;
        if (!s1 || !s2) return s1 == s2;
        return strcmp(s1, s2) == 0;
    }
    return memcmp(p1, p2, f->size) == 0;
}

static bool repeated_equal_noref(const nmo_type_field_t *f1,
                                 const nmo_type_field_t *f2,
                                 const void *p1, const void *p2)
{
    if (!f1 || !f2) return false;
    if (is_object_ref(f1) || is_object_ref(f2)) return true;
    if (f1->size != sizeof(nmo_array_t) || f2->size != sizeof(nmo_array_t) || !p1 || !p2) return true;

    const nmo_array_t *a1 = (const nmo_array_t *)p1;
    const nmo_array_t *a2 = (const nmo_array_t *)p2;
    if (a1->count != a2->count || a1->element_size != a2->element_size) return false;
    if (!a1->count || !a1->element_size) return true;
    if (!a1->data || !a2->data) return a1->data == a2->data;
    return memcmp(a1->data, a2->data, a1->count * a1->element_size) == 0;
}

static float similarity_core(const nmo_object_t *obj1, const nmo_object_t *obj2,
                             const nmo_type_registry_t *reg1,
                             const nmo_type_registry_t *reg2,
                             const nmo_object_repository_t *repo1,
                             const nmo_object_repository_t *repo2,
                             const match_lookup_t *lookup,
                             bool use_refs)
{
    if (!obj1 || !obj2) return 0.0f;
    if (nmo_object_get_class_id(obj1) != nmo_object_get_class_id(obj2)) return 0.0f;

    const nmo_type_descriptor_t *t1 = resolve_object_type(reg1, obj1);
    const nmo_type_descriptor_t *t2 = resolve_object_type(reg2, obj2);
    const void *s1 = nmo_object_get_state(obj1);
    const void *s2 = nmo_object_get_state(obj2);

    if (t1 && t2 && s1 && s2 && nmo_type_has_reflection(t1) && nmo_type_has_reflection(t2)) {
        size_t total = 0, eq = 0;
        size_t field_count = nmo_type_get_field_count(t1);
        for (size_t i = 0; i < field_count; i++) {
            const nmo_type_field_t *f1 = nmo_type_get_field_by_index(t1, i);
            if (!f1 || !f1->name || is_base_embedding(t1, f1)) continue;
            const nmo_type_field_t *f2 = nmo_type_get_field_by_name(t2, f1->name);
            total++;
            if (!f2) continue;
            const void *p1 = nmo_field_get_ptr_const(s1, f1);
            const void *p2 = nmo_field_get_ptr_const(s2, f2);
            bool same;
            if ((f1->flags & NMO_FIELD_REPEATED) || (f2->flags & NMO_FIELD_REPEATED)) {
                same = use_refs ? repeated_equal(f1, f2, p1, p2, repo1, repo2, lookup)
                                : repeated_equal_noref(f1, f2, p1, p2);
            } else if (f1->size != f2->size) {
                same = false;
            } else {
                same = use_refs ? scalar_equal(f1, p1, p2, repo1, repo2, lookup, reg1)
                                : scalar_equal_noref(f1, p1, p2, reg1);
            }
            if (same) eq++;
        }
        return total ? (float)eq / (float)total : 1.0f;
    }

    nmo_chunk_t *c1 = nmo_object_get_chunk(obj1);
    nmo_chunk_t *c2 = nmo_object_get_chunk(obj2);
    if (c1 && c2) {
        size_t sz1 = 0, sz2 = 0;
        const void *d1 = nmo_chunk_get_data(c1, &sz1);
        const void *d2 = nmo_chunk_get_data(c2, &sz2);
        if (d1 && d2 && sz1 == sz2 && sz1 > 0) {
            if (memcmp(d1, d2, sz1) == 0) return 1.0f;
            size_t same = 0;
            const uint8_t *b1 = (const uint8_t *)d1;
            const uint8_t *b2 = (const uint8_t *)d2;
            for (size_t i = 0; i < sz1; i++) if (b1[i] == b2[i]) same++;
            return (float)same / (float)sz1;
        }
        if (sz1 != sz2) return 0.2f;
    }
    return 0.5f;
}

float nmo_object_similarity(const nmo_object_t *obj1, const nmo_object_t *obj2,
                            const nmo_type_registry_t *reg1,
                            const nmo_type_registry_t *reg2,
                            const nmo_object_repository_t *repo1,
                            const nmo_object_repository_t *repo2)
{
    return similarity_core(obj1, obj2, reg1, reg2, repo1, repo2, NULL, true);
}

static float initial_pair_similarity(const nmo_object_t *obj1,
                                     const nmo_object_t *obj2,
                                     const diff_side_t *s1,
                                     const diff_side_t *s2)
{
    float local = similarity_core(obj1, obj2, s1->registry, s2->registry, s1->repo, s2->repo, NULL, false);
    float name = strcmp(norm_name(nmo_object_get_name(obj1)), norm_name(nmo_object_get_name(obj2))) == 0 ? 1.0f : 0.0f;
    float out = 0.9f * local + 0.1f * name;
    if (out < 0.0f) out = 0.0f;
    if (out > 1.0f) out = 1.0f;
    return out;
}

static uint32_t hash_bytes32(const void *data, size_t size, uint32_t seed)
{
    if (!data || size == 0) return nmo_hash_int32(seed ^ 0x85ebca6bu);
    size_t hv = nmo_hash_fnv1a(data, size);
    uint32_t lo = (uint32_t)hv;
    uint32_t hi = (uint32_t)(hv >> 32);
    return nmo_hash_int32(seed ^ lo ^ (hi * 0x9e3779b9u));
}

static bool type_is_pointer_like(const nmo_type_descriptor_t *type)
{
    if (!type) return false;
    if (nmo_guid_equals(type->guid, CKPGUID_POINTER)) return true;
    return (type->category & NMO_TYPE_CATEGORY_POINTER) != 0;
}

static bool type_hash_safe_for_diff(const nmo_type_descriptor_t *type)
{
    if (!type || !type->vtable || !type->vtable->hash) return false;
    if (type_is_pointer_like(type)) return false;
    return true;
}

static uint32_t hash_instance_for_diff(const nmo_type_descriptor_t *type,
                                       const void *value,
                                       size_t size_fallback,
                                       const nmo_type_registry_t *reg,
                                       uint32_t seed,
                                       int depth);

static uint32_t hash_array_for_diff(const nmo_array_t *arr,
                                    const nmo_type_descriptor_t *elem_type,
                                    const nmo_type_registry_t *reg,
                                    uint32_t seed,
                                    int depth)
{
    uint32_t h = seed;
    if (!arr) return h;

    h = hash_bytes32(&arr->count, sizeof(arr->count), h);
    h = hash_bytes32(&arr->element_size, sizeof(arr->element_size), h);
    if (arr->count == 0 || arr->element_size == 0 || !arr->data) return h;

    if (elem_type && elem_type->size > 0 && (size_t)elem_type->size == arr->element_size) {
        const uint8_t *base = (const uint8_t *)arr->data;
        for (size_t i = 0; i < arr->count; i++) {
            const void *ep = base + i * arr->element_size;
            uint32_t eh = hash_instance_for_diff(
                elem_type, ep, arr->element_size, reg, 0x9e3779b9u, depth + 1);
            h = hash_bytes32(&eh, sizeof(eh), h);
        }
        return h;
    }

    size_t bytes = 0;
    if (diff_mul_size(arr->count, arr->element_size, &bytes) && bytes > 0) {
        h = hash_bytes32(arr->data, bytes, h);
    }
    return h;
}

static uint32_t hash_field_value_for_diff(const nmo_type_field_t *field,
                                          const void *value,
                                          const nmo_type_registry_t *reg,
                                          uint32_t seed,
                                          int depth)
{
    if (!field || !value) return seed;
    if (is_object_ref(field)) return seed;

    const nmo_type_descriptor_t *field_type =
        (reg && !nmo_guid_is_null(field->type_guid))
            ? nmo_type_registry_find_by_guid(reg, field->type_guid)
            : NULL;

    if (field->flags & NMO_FIELD_POINTER) {
        if (field->size < sizeof(void *)) {
            return hash_bytes32(value, field->size, seed);
        }

        const void *pointed = *(const void *const *)value;
        uint8_t present = pointed ? 1u : 0u;
        uint32_t h = hash_bytes32(&present, sizeof(present), seed);
        if (!pointed) return h;

        /* Raw pointer arrays need count metadata and element ownership rules.
         * The diff equality path also avoids dereferencing them blindly, so the
         * signature records presence without walking potentially invalid memory.
         */
        if (field->flags & NMO_FIELD_REPEATED) {
            return h;
        }

        if (field_type && field_type->size > 0) {
            uint32_t ph = hash_instance_for_diff(
                field_type, pointed, field_type->size, reg, 0x9747b28cu, depth + 1);
            return hash_bytes32(&ph, sizeof(ph), h);
        }
        return h;
    }

    if ((field->flags & NMO_FIELD_REPEATED) && field->size == sizeof(nmo_array_t)) {
        return hash_array_for_diff((const nmo_array_t *)value, field_type,
                                   reg, 0x7f4a7c15u, depth + 1);
    }

    if (field->flags & NMO_FIELD_REPEATED) {
        return hash_bytes32(value, field->size, seed);
    }

    return hash_instance_for_diff(field_type, value, field->size,
                                  reg, 0x9747b28cu, depth + 1);
}

static uint32_t hash_instance_for_diff(const nmo_type_descriptor_t *type,
                                       const void *value,
                                       size_t size_fallback,
                                       const nmo_type_registry_t *reg,
                                       uint32_t seed,
                                       int depth)
{
    if (!value) return seed;
    if (depth > 8) return hash_bytes32(value, size_fallback, seed);

    if (type && nmo_type_has_reflection(type)) {
        uint32_t h = seed;
        size_t fields = nmo_type_get_field_count(type);
        for (size_t i = 0; i < fields; i++) {
            const nmo_type_field_t *f = nmo_type_get_field_by_index(type, i);
            if (!f || !f->name || is_base_embedding(type, f)) continue;

            h = hash_bytes32(f->name, strlen(f->name), h);
            const void *p = nmo_field_get_ptr_const(value, f);
            if (!p) continue;
            if (is_object_ref(f)) continue;

            uint32_t fh = hash_field_value_for_diff(f, p, reg, 0x9747b28cu, depth);
            h = hash_bytes32(&fh, sizeof(fh), h);
        }
        return h;
    }

    if (type_hash_safe_for_diff(type)) {
        uint32_t hv = type->vtable->hash(value);
        return hash_bytes32(&hv, sizeof(hv), seed);
    }

    if (type_is_pointer_like(type) && size_fallback >= sizeof(void *)) {
        const void *p = *(const void *const *)value;
        uint8_t marker = p ? 1u : 0u;
        return hash_bytes32(&marker, sizeof(marker), seed);
    }

    return hash_bytes32(value, size_fallback, seed);
}

static uint64_t compute_signature(const nmo_object_t *obj, const nmo_type_registry_t *reg)
{
    if (!obj) return 0;
    uint32_t sig = nmo_hash_int32((uint32_t)nmo_object_get_class_id(obj));

    const nmo_type_descriptor_t *t = resolve_object_type(reg, obj);
    const void *state = nmo_object_get_state(obj);
    if (t && state && nmo_type_has_reflection(t)) {
        size_t fields = nmo_type_get_field_count(t);
        size_t touched = 0;
        for (size_t i = 0; i < fields; i++) {
            const nmo_type_field_t *f = nmo_type_get_field_by_index(t, i);
            if (!f || !f->name || is_base_embedding(t, f)) continue;
            touched++;
            sig = hash_bytes32(f->name, strlen(f->name), sig);
            const void *p = nmo_field_get_ptr_const(state, f);
            if (!p) continue;
            if (is_object_ref(f)) continue;

            uint32_t field_sig = hash_field_value_for_diff(f, p, reg, 0x9747b28cu, 0);
            sig = hash_bytes32(&field_sig, sizeof(field_sig), sig);
        }
        if (touched) return ((uint64_t)nmo_object_get_class_id(obj) << 32) | (uint64_t)sig;
    }

    if (t && state) {
        uint32_t hv = hash_instance_for_diff(t, state, t->size, reg, 0x3c6ef372u, 0);
        sig = hash_bytes32(&hv, sizeof(hv), sig);
        return ((uint64_t)nmo_object_get_class_id(obj) << 32) | (uint64_t)sig;
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    if (chunk) {
        size_t sz = 0;
        const void *d = nmo_chunk_get_data(chunk, &sz);
        sig = hash_bytes32(&sz, sizeof(sz), sig);
        if (d && sz) sig = hash_bytes32(d, sz, sig);
    }
    return ((uint64_t)nmo_object_get_class_id(obj) << 32) | (uint64_t)sig;
}

static bool build_graph_side(const diff_side_t *src, nmo_allocator_t *allocator, graph_side_t *dst)
{
    memset(dst, 0, sizeof(*dst));
    dst->side = *src;
    dst->allocator = allocator;

    size_t n = nmo_object_repository_get_count(src->repo);
    if (!n) return true;

    dst->nodes = (diff_node_t *)diff_alloc_array(
        allocator, n, sizeof(diff_node_t), _Alignof(diff_node_t), true);
    if (!dst->nodes) return false;
    dst->count = n;
    for (size_t i = 0; i < n; i++) {
        nmo_object_t *obj = nmo_object_repository_get_by_index(src->repo, i);
        if (obj == NULL) {
            continue;
        }
        dst->nodes[i].obj = obj;
        dst->nodes[i].id = nmo_object_get_id(obj);
        dst->nodes[i].cid = nmo_object_get_class_id(obj);
        dst->nodes[i].name = nmo_object_get_name(obj);
        dst->nodes[i].sig = compute_signature(obj, src->registry);
    }

    if (!src->registry) return true;
    nmo_arena_t *a = nmo_arena_create(allocator, 64 * 1024);
    if (!a) return true;
    nmo_ref_graph_t *g = nmo_ref_graph_create(src->repo, src->registry, a);
    if (!g) {
        nmo_arena_destroy(a);
        return true;
    }

    nmo_ref_edge_t *edges = NULL;
    size_t edge_count = 0;
    if (nmo_ref_graph_get_edges(g, &edges, &edge_count) != NMO_OK || !edges || !edge_count) {
        nmo_ref_graph_destroy(g);
        nmo_arena_destroy(a);
        return true;
    }

    size_t *out_count = (size_t *)diff_alloc_array(
        allocator, n, sizeof(size_t), _Alignof(size_t), true);
    size_t *in_count = (size_t *)diff_alloc_array(
        allocator, n, sizeof(size_t), _Alignof(size_t), true);
    int *fi = (int *)diff_alloc_array(
        allocator, edge_count, sizeof(int), _Alignof(int), false);
    int *ti = (int *)diff_alloc_array(
        allocator, edge_count, sizeof(int), _Alignof(int), false);
    if (!out_count || !in_count || !fi || !ti) {
        diff_free(allocator, out_count);
        diff_free(allocator, in_count);
        diff_free(allocator, fi);
        diff_free(allocator, ti);
        nmo_ref_graph_destroy(g);
        nmo_arena_destroy(a);
        return false;
    }

    for (size_t e = 0; e < edge_count; e++) {
        fi[e] = -1;
        ti[e] = -1;
        for (size_t i = 0; i < n; i++) {
            if (dst->nodes[i].id == edges[e].from) fi[e] = (int)i;
            if (dst->nodes[i].id == edges[e].to) ti[e] = (int)i;
        }
        if (fi[e] >= 0 && ti[e] >= 0) {
            out_count[(size_t)fi[e]]++;
            in_count[(size_t)ti[e]]++;
        }
    }

    size_t out_total = 0, in_total = 0;
    for (size_t i = 0; i < n; i++) {
        dst->nodes[i].out_start = out_total;
        dst->nodes[i].out_count = out_count[i];
        out_total += out_count[i];
        dst->nodes[i].in_start = in_total;
        dst->nodes[i].in_count = in_count[i];
        in_total += in_count[i];
    }

    if (out_total) {
        dst->out_edges = (topo_edge_t *)diff_alloc_array(
            allocator, out_total, sizeof(topo_edge_t), _Alignof(topo_edge_t), true);
    }
    if (in_total) {
        dst->in_edges = (topo_edge_t *)diff_alloc_array(
            allocator, in_total, sizeof(topo_edge_t), _Alignof(topo_edge_t), true);
    }
    if ((out_total && !dst->out_edges) || (in_total && !dst->in_edges)) {
        diff_free(allocator, out_count);
        diff_free(allocator, in_count);
        diff_free(allocator, fi);
        diff_free(allocator, ti);
        nmo_ref_graph_destroy(g);
        nmo_arena_destroy(a);
        return false;
    }

    size_t *oc = (size_t *)diff_alloc_array(
        allocator, n, sizeof(size_t), _Alignof(size_t), true);
    size_t *ic = (size_t *)diff_alloc_array(
        allocator, n, sizeof(size_t), _Alignof(size_t), true);
    if (!oc || !ic) {
        diff_free(allocator, oc);
        diff_free(allocator, ic);
        diff_free(allocator, out_count);
        diff_free(allocator, in_count);
        diff_free(allocator, fi);
        diff_free(allocator, ti);
        nmo_ref_graph_destroy(g);
        nmo_arena_destroy(a);
        return false;
    }

    for (size_t e = 0; e < edge_count; e++) {
        if (fi[e] < 0 || ti[e] < 0) continue;
        size_t from = (size_t)fi[e], to = (size_t)ti[e];
        size_t op = dst->nodes[from].out_start + oc[from]++;
        size_t ip = dst->nodes[to].in_start + ic[to]++;
        dst->out_edges[op].other = (uint32_t)to;
        dst->out_edges[op].other_class = dst->nodes[to].cid;
        dst->out_edges[op].kind = edges[e].kind;
        dst->in_edges[ip].other = (uint32_t)from;
        dst->in_edges[ip].other_class = dst->nodes[from].cid;
        dst->in_edges[ip].kind = edges[e].kind;
    }

    diff_free(allocator, oc);
    diff_free(allocator, ic);
    diff_free(allocator, out_count);
    diff_free(allocator, in_count);
    diff_free(allocator, fi);
    diff_free(allocator, ti);
    nmo_ref_graph_destroy(g);
    nmo_arena_destroy(a);
    return true;
}

static void graph_side_destroy(graph_side_t *g)
{
    if (!g) return;
    diff_free(g->allocator, g->nodes);
    diff_free(g->allocator, g->out_edges);
    diff_free(g->allocator, g->in_edges);
    memset(g, 0, sizeof(*g));
}

static class_bucket_t *bucket_find(class_bucket_array_t *arr, nmo_class_id_t cid)
{
    for (size_t i = 0; i < arr->count; i++) if (arr->items[i].cid == cid) return &arr->items[i];
    return NULL;
}

static bool bucket_push(nmo_allocator_t *allocator,
                        uint32_t **arr, size_t *count, size_t *cap, uint32_t idx)
{
    if (*count >= *cap) {
        size_t nc = *cap < 8 ? 8 : *cap * 2;
        uint32_t *ni = (uint32_t *)diff_realloc_array(
            allocator, *arr, nc, sizeof(uint32_t), _Alignof(uint32_t));
        if (!ni) return false;
        *arr = ni;
        *cap = nc;
    }
    (*arr)[(*count)++] = idx;
    return true;
}

static bool build_buckets(const graph_side_t *g1, const graph_side_t *g2, class_bucket_array_t *out)
{
    memset(out, 0, sizeof(*out));
    out->allocator = g1 ? g1->allocator : NULL;
    for (size_t i = 0; i < g1->count; i++) {
        class_bucket_t *b = bucket_find(out, g1->nodes[i].cid);
        if (!b) {
            if (out->count >= out->cap) {
                size_t nc = out->cap < 16 ? 16 : out->cap * 2;
                class_bucket_t *ni = (class_bucket_t *)diff_realloc_array(
                    out->allocator, out->items, nc, sizeof(class_bucket_t), _Alignof(class_bucket_t));
                if (!ni) return false;
                out->items = ni;
                out->cap = nc;
            }
            b = &out->items[out->count++];
            memset(b, 0, sizeof(*b));
            b->cid = g1->nodes[i].cid;
        }
        if (!bucket_push(out->allocator, &b->idx1, &b->count1, &b->cap1, (uint32_t)i)) return false;
    }
    for (size_t i = 0; i < g2->count; i++) {
        class_bucket_t *b = bucket_find(out, g2->nodes[i].cid);
        if (!b) {
            if (out->count >= out->cap) {
                size_t nc = out->cap < 16 ? 16 : out->cap * 2;
                class_bucket_t *ni = (class_bucket_t *)diff_realloc_array(
                    out->allocator, out->items, nc, sizeof(class_bucket_t), _Alignof(class_bucket_t));
                if (!ni) return false;
                out->items = ni;
                out->cap = nc;
            }
            b = &out->items[out->count++];
            memset(b, 0, sizeof(*b));
            b->cid = g2->nodes[i].cid;
        }
        if (!bucket_push(out->allocator, &b->idx2, &b->count2, &b->cap2, (uint32_t)i)) return false;
    }
    return true;
}

static void buckets_destroy(class_bucket_array_t *b)
{
    if (!b) return;
    for (size_t i = 0; i < b->count; i++) {
        diff_free(b->allocator, b->items[i].idx1);
        diff_free(b->allocator, b->items[i].idx2);
    }
    diff_free(b->allocator, b->items);
    memset(b, 0, sizeof(*b));
}

static bool match_init(match_state_t *m, size_t n1, size_t n2, nmo_allocator_t *allocator)
{
    memset(m, 0, sizeof(*m));
    m->allocator = allocator;
    m->n1 = n1; m->n2 = n2;
    m->to2_by1 = (int *)diff_alloc_array(allocator, n1, sizeof(int), _Alignof(int), false);
    m->to1_by2 = (int *)diff_alloc_array(allocator, n2, sizeof(int), _Alignof(int), false);
    m->sim_by1 = (float *)diff_alloc_array(allocator, n1, sizeof(float), _Alignof(float), true);
    m->anchor_by1 = (uint8_t *)diff_alloc_array(allocator, n1, sizeof(uint8_t), _Alignof(uint8_t), true);

    if ((n1 && (!m->to2_by1 || !m->sim_by1 || !m->anchor_by1)) || (n2 && !m->to1_by2)) {
        diff_free(allocator, m->to2_by1);
        diff_free(allocator, m->to1_by2);
        diff_free(allocator, m->sim_by1);
        diff_free(allocator, m->anchor_by1);
        memset(m, 0, sizeof(*m));
        return false;
    }
    for (size_t i = 0; i < n1; i++) m->to2_by1[i] = -1;
    for (size_t j = 0; j < n2; j++) m->to1_by2[j] = -1;
    return true;
}

static void match_destroy(match_state_t *m)
{
    if (!m) return;
    diff_free(m->allocator, m->to2_by1);
    diff_free(m->allocator, m->to1_by2);
    diff_free(m->allocator, m->sim_by1);
    diff_free(m->allocator, m->anchor_by1);
    memset(m, 0, sizeof(*m));
}

static bool match_add(match_state_t *m, uint32_t i, uint32_t j, float sim, bool anchor)
{
    if (!m || i >= m->n1 || j >= m->n2) return false;
    if (m->to2_by1[i] >= 0 || m->to1_by2[j] >= 0) return false;
    m->to2_by1[i] = (int)j;
    m->to1_by2[j] = (int)i;
    m->sim_by1[i] = sim;
    m->anchor_by1[i] = anchor ? 1u : 0u;
    return true;
}

static size_t count_name(const class_bucket_t *b, const graph_side_t *g, bool left,
                         const match_state_t *m, const char *name)
{
    size_t c = 0;
    if (left) {
        for (size_t i = 0; i < b->count1; i++) {
            uint32_t idx = b->idx1[i];
            if (m->to2_by1[idx] >= 0) continue;
            if (strcmp(norm_name(g->nodes[idx].name), name) == 0) c++;
        }
    } else {
        for (size_t i = 0; i < b->count2; i++) {
            uint32_t idx = b->idx2[i];
            if (m->to1_by2[idx] >= 0) continue;
            if (strcmp(norm_name(g->nodes[idx].name), name) == 0) c++;
        }
    }
    return c;
}

static int find_name(const class_bucket_t *b, const graph_side_t *g, bool left,
                     const match_state_t *m, const char *name)
{
    if (left) {
        for (size_t i = 0; i < b->count1; i++) {
            uint32_t idx = b->idx1[i];
            if (m->to2_by1[idx] >= 0) continue;
            if (strcmp(norm_name(g->nodes[idx].name), name) == 0) return (int)idx;
        }
    } else {
        for (size_t i = 0; i < b->count2; i++) {
            uint32_t idx = b->idx2[i];
            if (m->to1_by2[idx] >= 0) continue;
            if (strcmp(norm_name(g->nodes[idx].name), name) == 0) return (int)idx;
        }
    }
    return -1;
}

static size_t count_sig(const class_bucket_t *b, const graph_side_t *g, bool left,
                        const match_state_t *m, uint64_t sig)
{
    size_t c = 0;
    if (left) {
        for (size_t i = 0; i < b->count1; i++) {
            uint32_t idx = b->idx1[i];
            if (m->to2_by1[idx] >= 0) continue;
            if (g->nodes[idx].sig == sig) c++;
        }
    } else {
        for (size_t i = 0; i < b->count2; i++) {
            uint32_t idx = b->idx2[i];
            if (m->to1_by2[idx] >= 0) continue;
            if (g->nodes[idx].sig == sig) c++;
        }
    }
    return c;
}

static int find_sig(const class_bucket_t *b, const graph_side_t *g, bool left,
                    const match_state_t *m, uint64_t sig)
{
    if (left) {
        for (size_t i = 0; i < b->count1; i++) {
            uint32_t idx = b->idx1[i];
            if (m->to2_by1[idx] >= 0) continue;
            if (g->nodes[idx].sig == sig) return (int)idx;
        }
    } else {
        for (size_t i = 0; i < b->count2; i++) {
            uint32_t idx = b->idx2[i];
            if (m->to1_by2[idx] >= 0) continue;
            if (g->nodes[idx].sig == sig) return (int)idx;
        }
    }
    return -1;
}

static void anchor_bucket(const class_bucket_t *b,
                          const graph_side_t *g1,
                          const graph_side_t *g2,
                          const diff_side_t *s1,
                          const diff_side_t *s2,
                          match_state_t *m)
{
    for (size_t i = 0; i < b->count1; i++) {
        uint32_t idx1 = b->idx1[i];
        if (m->to2_by1[idx1] >= 0) continue;
        const char *name = norm_name(g1->nodes[idx1].name);
        if (!name[0]) continue;
        if (count_name(b, g1, true, m, name) != 1) continue;
        if (count_name(b, g2, false, m, name) != 1) continue;
        int idx2 = find_name(b, g2, false, m, name);
        if (idx2 < 0) continue;
        float sim = initial_pair_similarity(g1->nodes[idx1].obj, g2->nodes[idx2].obj, s1, s2);
        (void)match_add(m, idx1, (uint32_t)idx2, sim, true);
    }
    for (size_t i = 0; i < b->count1; i++) {
        uint32_t idx1 = b->idx1[i];
        if (m->to2_by1[idx1] >= 0) continue;
        uint64_t sig = g1->nodes[idx1].sig;
        if (count_sig(b, g1, true, m, sig) != 1) continue;
        if (count_sig(b, g2, false, m, sig) != 1) continue;
        int idx2 = find_sig(b, g2, false, m, sig);
        if (idx2 < 0) continue;
        float sim = initial_pair_similarity(g1->nodes[idx1].obj, g2->nodes[idx2].obj, s1, s2);
        (void)match_add(m, idx1, (uint32_t)idx2, sim, true);
    }
}

static void sort_indices_by_id(uint32_t *arr, size_t n, const graph_side_t *g)
{
    if (!arr || !g || n < 2) return;
    for (size_t i = 1; i < n; i++) {
        uint32_t key = arr[i];
        nmo_object_id_t key_id = g->nodes[key].id;
        size_t j = i;
        while (j > 0) {
            uint32_t prev = arr[j - 1];
            nmo_object_id_t prev_id = g->nodes[prev].id;
            if (prev_id < key_id) break;
            if (prev_id == key_id && prev < key) break;
            arr[j] = prev;
            j--;
        }
        arr[j] = key;
    }
}

static class_matrix_t *mat_add(class_matrix_array_t *arr,
                               nmo_class_id_t cid,
                               size_t n1,
                               size_t n2)
{
    if (!arr || !n1 || !n2) return NULL;
    if (arr->count >= arr->cap) {
        size_t nc = arr->cap < 8 ? 8 : arr->cap * 2;
        class_matrix_t *ni = (class_matrix_t *)diff_realloc_array(
            arr->allocator, arr->items, nc, sizeof(class_matrix_t), _Alignof(class_matrix_t));
        if (!ni) return NULL;
        arr->items = ni;
        arr->cap = nc;
    }
    class_matrix_t *m = &arr->items[arr->count++];
    memset(m, 0, sizeof(*m));
    m->cid = cid;
    m->n1 = n1;
    m->n2 = n2;
    m->u1 = (uint32_t *)diff_alloc_array(arr->allocator, n1, sizeof(uint32_t), _Alignof(uint32_t), false);
    m->u2 = (uint32_t *)diff_alloc_array(arr->allocator, n2, sizeof(uint32_t), _Alignof(uint32_t), false);
    m->s0 = (float *)diff_alloc_array(arr->allocator, n1 * n2, sizeof(float), _Alignof(float), true);
    m->s = (float *)diff_alloc_array(arr->allocator, n1 * n2, sizeof(float), _Alignof(float), true);
    m->tmp = (float *)diff_alloc_array(arr->allocator, n1 * n2, sizeof(float), _Alignof(float), true);
    if (!m->u1 || !m->u2 || !m->s0 || !m->s || !m->tmp) return NULL;
    return m;
}

static void mats_destroy(class_matrix_array_t *mats)
{
    if (!mats) return;
    for (size_t i = 0; i < mats->count; i++) {
        diff_free(mats->allocator, mats->items[i].u1);
        diff_free(mats->allocator, mats->items[i].u2);
        diff_free(mats->allocator, mats->items[i].s0);
        diff_free(mats->allocator, mats->items[i].s);
        diff_free(mats->allocator, mats->items[i].tmp);
    }
    diff_free(mats->allocator, mats->items);
    memset(mats, 0, sizeof(*mats));
}

static bool build_unmatched_matrices(const class_bucket_array_t *buckets,
                                     const graph_side_t *g1,
                                     const graph_side_t *g2,
                                     const diff_side_t *s1,
                                     const diff_side_t *s2,
                                     const match_state_t *match,
                                     class_matrix_array_t *out)
{
    memset(out, 0, sizeof(*out));
    out->allocator = g1 ? g1->allocator : NULL;
    for (size_t bi = 0; bi < buckets->count; bi++) {
        const class_bucket_t *b = &buckets->items[bi];
        size_t n1 = 0, n2 = 0;
        for (size_t i = 0; i < b->count1; i++) {
            if (match->to2_by1[b->idx1[i]] < 0) n1++;
        }
        for (size_t j = 0; j < b->count2; j++) {
            if (match->to1_by2[b->idx2[j]] < 0) n2++;
        }
        if (!n1 || !n2) continue;

        class_matrix_t *m = mat_add(out, b->cid, n1, n2);
        if (!m) return false;

        size_t w = 0;
        for (size_t i = 0; i < b->count1; i++) {
            uint32_t idx = b->idx1[i];
            if (match->to2_by1[idx] < 0) m->u1[w++] = idx;
        }
        w = 0;
        for (size_t j = 0; j < b->count2; j++) {
            uint32_t idx = b->idx2[j];
            if (match->to1_by2[idx] < 0) m->u2[w++] = idx;
        }
        sort_indices_by_id(m->u1, m->n1, g1);
        sort_indices_by_id(m->u2, m->n2, g2);

        for (size_t i = 0; i < m->n1; i++) {
            nmo_object_t *o1 = g1->nodes[m->u1[i]].obj;
            for (size_t j = 0; j < m->n2; j++) {
                nmo_object_t *o2 = g2->nodes[m->u2[j]].obj;
                float init = initial_pair_similarity(o1, o2, s1, s2);
                m->s0[i * m->n2 + j] = init;
                m->s[i * m->n2 + j] = init;
            }
        }
    }
    return true;
}

static float lookup_score(const flood_env_t *env, uint32_t idx1, uint32_t idx2)
{
    if (!env || idx1 >= env->g1->count || idx2 >= env->g2->count) return 0.0f;
    int j = env->match->to2_by1[idx1];
    if (j >= 0) return ((uint32_t)j == idx2) ? 1.0f : 0.0f;
    if (env->match->to1_by2[idx2] >= 0) return 0.0f;

    int s1 = env->slot1[idx1];
    int s2 = env->slot2[idx2];
    if (s1 < 0 || s2 < 0 || s1 != s2) return 0.0f;
    int p1 = env->pos1[idx1];
    int p2 = env->pos2[idx2];
    if (p1 < 0 || p2 < 0) return 0.0f;
    const class_matrix_t *m = &env->mats->items[(size_t)s1];
    if ((size_t)p1 >= m->n1 || (size_t)p2 >= m->n2) return 0.0f;
    return m->s[(size_t)p1 * m->n2 + (size_t)p2];
}

static float edge_ctx(const flood_env_t *env,
                      const topo_edge_t *a, size_t na,
                      const topo_edge_t *b, size_t nb)
{
    if (!na || !nb || !a || !b) return 0.0f;

    float sum_a = 0.0f;
    for (size_t i = 0; i < na; i++) {
        float best = 0.0f;
        for (size_t j = 0; j < nb; j++) {
            if (a[i].kind != b[j].kind) continue;
            if (a[i].other_class != b[j].other_class) continue;
            float s = lookup_score(env, a[i].other, b[j].other);
            if (s > best) best = s;
        }
        sum_a += best;
    }

    float sum_b = 0.0f;
    for (size_t j = 0; j < nb; j++) {
        float best = 0.0f;
        for (size_t i = 0; i < na; i++) {
            if (a[i].kind != b[j].kind) continue;
            if (a[i].other_class != b[j].other_class) continue;
            float s = lookup_score(env, a[i].other, b[j].other);
            if (s > best) best = s;
        }
        sum_b += best;
    }

    float avg_a = sum_a / (float)na;
    float avg_b = sum_b / (float)nb;
    return 0.5f * (avg_a + avg_b);
}

static bool run_flood(const graph_side_t *g1,
                      const graph_side_t *g2,
                      const match_state_t *match,
                      class_matrix_array_t *mats)
{
    if (!g1 || !g2 || !match || !mats) return false;
    if (!mats->count) return true;

    flood_env_t env;
    memset(&env, 0, sizeof(env));
    env.g1 = g1;
    env.g2 = g2;
    env.match = match;
    env.mats = mats;
    nmo_allocator_t *allocator = mats->allocator;
    env.slot1 = (int *)diff_alloc_array(allocator, g1->count, sizeof(int), _Alignof(int), false);
    env.slot2 = (int *)diff_alloc_array(allocator, g2->count, sizeof(int), _Alignof(int), false);
    env.pos1 = (int *)diff_alloc_array(allocator, g1->count, sizeof(int), _Alignof(int), false);
    env.pos2 = (int *)diff_alloc_array(allocator, g2->count, sizeof(int), _Alignof(int), false);
    if ((g1->count && (!env.slot1 || !env.pos1)) || (g2->count && (!env.slot2 || !env.pos2))) {
        diff_free(allocator, env.slot1);
        diff_free(allocator, env.slot2);
        diff_free(allocator, env.pos1);
        diff_free(allocator, env.pos2);
        return false;
    }
    for (size_t i = 0; i < g1->count; i++) env.slot1[i] = env.pos1[i] = -1;
    for (size_t j = 0; j < g2->count; j++) env.slot2[j] = env.pos2[j] = -1;
    for (size_t mi = 0; mi < mats->count; mi++) {
        class_matrix_t *m = &mats->items[mi];
        for (size_t i = 0; i < m->n1; i++) {
            env.slot1[m->u1[i]] = (int)mi;
            env.pos1[m->u1[i]] = (int)i;
        }
        for (size_t j = 0; j < m->n2; j++) {
            env.slot2[m->u2[j]] = (int)mi;
            env.pos2[m->u2[j]] = (int)j;
        }
    }

    const int kMaxIter = 6;
    const float kEps = 1e-3f;
    for (int iter = 0; iter < kMaxIter; iter++) {
        float max_delta = 0.0f;
        for (size_t mi = 0; mi < mats->count; mi++) {
            class_matrix_t *m = &mats->items[mi];
            for (size_t i = 0; i < m->n1; i++) {
                uint32_t gi = m->u1[i];
                const diff_node_t *n1 = &g1->nodes[gi];
                const topo_edge_t *o1 = n1->out_count ? &g1->out_edges[n1->out_start] : NULL;
                const topo_edge_t *in1 = n1->in_count ? &g1->in_edges[n1->in_start] : NULL;
                for (size_t j = 0; j < m->n2; j++) {
                    uint32_t gj = m->u2[j];
                    const diff_node_t *n2 = &g2->nodes[gj];
                    const topo_edge_t *o2 = n2->out_count ? &g2->out_edges[n2->out_start] : NULL;
                    const topo_edge_t *in2 = n2->in_count ? &g2->in_edges[n2->in_start] : NULL;
                    float out_ctx = edge_ctx(&env, o1, n1->out_count, o2, n2->out_count);
                    float in_ctx = edge_ctx(&env, in1, n1->in_count, in2, n2->in_count);
                    float s0 = m->s0[i * m->n2 + j];
                    float ns = 0.6f * s0 + 0.25f * out_ctx + 0.15f * in_ctx;
                    if (ns < 0.0f) ns = 0.0f;
                    if (ns > 1.0f) ns = 1.0f;
                    m->tmp[i * m->n2 + j] = ns;
                }
            }
            for (size_t k = 0; k < m->n1 * m->n2; k++) {
                float d = fabsf(m->tmp[k] - m->s[k]);
                if (d > max_delta) max_delta = d;
                m->s[k] = m->tmp[k];
            }
        }
        if (max_delta < kEps) break;
    }

    diff_free(allocator, env.slot1);
    diff_free(allocator, env.slot2);
    diff_free(allocator, env.pos1);
    diff_free(allocator, env.pos2);
    return true;
}

static bool hungarian_min(const float *cost, size_t n, int *assign, nmo_allocator_t *allocator)
{
    if (!cost || !assign || !n) return false;
    size_t np1 = n + 1;
    double *u = (double *)diff_alloc_array(allocator, np1, sizeof(double), _Alignof(double), true);
    double *v = (double *)diff_alloc_array(allocator, np1, sizeof(double), _Alignof(double), true);
    int *p = (int *)diff_alloc_array(allocator, np1, sizeof(int), _Alignof(int), true);
    int *way = (int *)diff_alloc_array(allocator, np1, sizeof(int), _Alignof(int), true);
    double *minv = (double *)diff_alloc_array(allocator, np1, sizeof(double), _Alignof(double), true);
    uint8_t *used = (uint8_t *)diff_alloc_array(allocator, np1, sizeof(uint8_t), _Alignof(uint8_t), true);
    if (!u || !v || !p || !way || !minv || !used) {
        diff_free(allocator, u);
        diff_free(allocator, v);
        diff_free(allocator, p);
        diff_free(allocator, way);
        diff_free(allocator, minv);
        diff_free(allocator, used);
        return false;
    }

    for (size_t i = 0; i < n; i++) assign[i] = -1;
    const double INF = 1e18;
    for (size_t i = 1; i <= n; i++) {
        p[0] = (int)i;
        int j0 = 0;
        for (size_t j = 0; j <= n; j++) {
            minv[j] = INF;
            used[j] = 0;
        }
        do {
            used[j0] = 1;
            int i0 = p[j0];
            double delta = INF;
            int j1 = 0;
            for (size_t j = 1; j <= n; j++) {
                if (used[j]) continue;
                double cur = (double)cost[((size_t)(i0 - 1)) * n + (j - 1)] - u[i0] - v[j];
                if (cur < minv[j] - 1e-12) {
                    minv[j] = cur;
                    way[j] = j0;
                }
                if (minv[j] < delta - 1e-12 ||
                    (fabs(minv[j] - delta) <= 1e-12 && (int)j < j1)) {
                    delta = minv[j];
                    j1 = (int)j;
                }
            }
            for (size_t j = 0; j <= n; j++) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
        } while (p[j0] != 0);

        do {
            int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }

    for (size_t j = 1; j <= n; j++) {
        if (p[j] > 0) assign[(size_t)(p[j] - 1)] = (int)(j - 1);
    }
    diff_free(allocator, u);
    diff_free(allocator, v);
    diff_free(allocator, p);
    diff_free(allocator, way);
    diff_free(allocator, minv);
    diff_free(allocator, used);
    return true;
}

static bool apply_hungarian(class_matrix_array_t *mats,
                            match_state_t *match,
                            float min_similarity)
{
    if (!mats || !match) return false;
    if (min_similarity < 0.0f) min_similarity = 0.0f;
    if (min_similarity > 1.0f) min_similarity = 1.0f;
    float dummy_cost = 1.0f - min_similarity;

    for (size_t mi = 0; mi < mats->count; mi++) {
        class_matrix_t *m = &mats->items[mi];
        size_t n = m->n1 > m->n2 ? m->n1 : m->n2;
        if (!n) continue;
        float *cost = (float *)diff_alloc_array(
            mats->allocator, n * n, sizeof(float), _Alignof(float), true);
        int *assign = (int *)diff_alloc_array(
            mats->allocator, n, sizeof(int), _Alignof(int), false);
        if (!cost || !assign) {
            diff_free(mats->allocator, cost);
            diff_free(mats->allocator, assign);
            return false;
        }

        for (size_t i = 0; i < m->n1; i++) {
            for (size_t j = 0; j < m->n2; j++) {
                cost[i * n + j] = 1.0f - m->s[i * m->n2 + j];
            }
            for (size_t j = m->n2; j < n; j++) {
                cost[i * n + j] = dummy_cost;
            }
        }
        for (size_t i = m->n1; i < n; i++) {
            for (size_t j = 0; j < m->n2; j++) {
                cost[i * n + j] = dummy_cost;
            }
        }

        if (!hungarian_min(cost, n, assign, mats->allocator)) {
            diff_free(mats->allocator, cost);
            diff_free(mats->allocator, assign);
            return false;
        }

        for (size_t i = 0; i < m->n1; i++) {
            int j = assign[i];
            if (j < 0 || (size_t)j >= m->n2) continue;
            float sim = m->s[i * m->n2 + (size_t)j];
            if (sim + 1e-6f < min_similarity) continue;
            (void)match_add(match, m->u1[i], m->u2[(size_t)j], sim, false);
        }
        diff_free(mats->allocator, cost);
        diff_free(mats->allocator, assign);
    }
    return true;
}

static void format_field_value(char *buf, size_t buf_size,
                               const nmo_type_field_t *field,
                               const void *ptr,
                               const nmo_object_repository_t *repo,
                               nmo_context_t *ctx)
{
    if (!buf || !buf_size) return;
    if (!field || !ptr) {
        snprintf(buf, buf_size, "(null)");
        return;
    }

    /* Pointer fields: dereference before formatting */
    if (field->flags & NMO_FIELD_POINTER) {
        const void *pointed = *(const void *const *)ptr;
        if (!pointed) {
            snprintf(buf, buf_size, "(null)");
            return;
        }
        if (field->flags & NMO_FIELD_REPEATED) {
            snprintf(buf, buf_size, "<ptr array>");
            return;
        }
        ptr = pointed;
    }

    if ((field->flags & NMO_FIELD_REPEATED) && field->size == sizeof(nmo_array_t)) {
        const nmo_array_t *arr = (const nmo_array_t *)ptr;
        snprintf(buf, buf_size, "<array count=%zu elem_size=%zu>",
                 arr ? arr->count : 0u, arr ? arr->element_size : 0u);
        return;
    }

    if (is_object_ref(field) && field->size >= sizeof(nmo_object_id_t)) {
        nmo_object_id_t id = *(const nmo_object_id_t *)ptr;
        nmo_object_format_ref(buf, buf_size, id, repo, ctx);
        return;
    }

    if (nmo_guid_equals(field->type_guid, CKPGUID_STRING) && field->size == sizeof(char *)) {
        const char *s = *(const char *const *)ptr;
        if (!s) snprintf(buf, buf_size, "(null)");
        else snprintf(buf, buf_size, "%s", s);
        return;
    }

    nmo_type_registry_t *registry = ctx ? nmo_context_get_type_registry(ctx) : NULL;
    const nmo_type_descriptor_t *field_type = registry
        ? nmo_type_registry_find_by_guid(registry, field->type_guid)
        : NULL;
    if (field_type && nmo_type_value_to_string(ptr, field_type, registry, buf, buf_size) == NMO_OK) {
        return;
    }

    if (field->size == 1) snprintf(buf, buf_size, "0x%02X", (unsigned)*(const uint8_t *)ptr);
    else if (field->size == 2) snprintf(buf, buf_size, "0x%04X", (unsigned)*(const uint16_t *)ptr);
    else if (field->size == 4) snprintf(buf, buf_size, "0x%08X", (unsigned)*(const uint32_t *)ptr);
    else if (field->size == 8) snprintf(buf, buf_size, "0x%016llX", (unsigned long long)*(const uint64_t *)ptr);
    else snprintf(buf, buf_size, "<%u bytes>", field->size);
}

static bool chunk_equal(const nmo_object_t *obj1, const nmo_object_t *obj2)
{
    nmo_chunk_t *c1 = obj1 ? nmo_object_get_chunk(obj1) : NULL;
    nmo_chunk_t *c2 = obj2 ? nmo_object_get_chunk(obj2) : NULL;
    if (!c1 || !c2) return c1 == c2;
    size_t sz1 = 0, sz2 = 0;
    const void *d1 = nmo_chunk_get_data(c1, &sz1);
    const void *d2 = nmo_chunk_get_data(c2, &sz2);
    if (sz1 != sz2) return false;
    if (sz1 == 0) return true;
    if (!d1 || !d2) return d1 == d2;
    return memcmp(d1, d2, sz1) == 0;
}

static bool build_field_diffs(const nmo_object_t *obj1,
                              const nmo_object_t *obj2,
                              const diff_side_t *s1,
                              const diff_side_t *s2,
                              const match_lookup_t *lookup,
                              uint32_t max_fields,
                              nmo_arena_t *arena,
                              nmo_field_diff_t **out_diffs,
                              size_t *out_count,
                              size_t *out_total)
{
    if (!out_diffs || !out_count || !out_total || !arena) return false;
    *out_diffs = NULL;
    *out_count = 0;
    *out_total = 0;

    const nmo_type_descriptor_t *t1 = resolve_object_type(s1->registry, obj1);
    const nmo_type_descriptor_t *t2 = resolve_object_type(s2->registry, obj2);
    const void *st1 = nmo_object_get_state(obj1);
    const void *st2 = nmo_object_get_state(obj2);

    size_t limit = max_fields == 0 ? SIZE_MAX : (size_t)max_fields;
    if (t1 && t2 && st1 && st2 && nmo_type_has_reflection(t1) && nmo_type_has_reflection(t2)) {
        size_t total = 0;
        size_t fields = nmo_type_get_field_count(t1);
        for (size_t i = 0; i < fields; i++) {
            const nmo_type_field_t *f1 = nmo_type_get_field_by_index(t1, i);
            if (!f1 || !f1->name || is_base_embedding(t1, f1)) continue;
            const nmo_type_field_t *f2 = nmo_type_get_field_by_name(t2, f1->name);
            const void *p1 = nmo_field_get_ptr_const(st1, f1);
            const void *p2 = f2 ? nmo_field_get_ptr_const(st2, f2) : NULL;
            bool same = false;
            if (!f2) {
                same = false;
            } else if ((f1->flags & NMO_FIELD_REPEATED) || (f2->flags & NMO_FIELD_REPEATED)) {
                same = repeated_equal(f1, f2, p1, p2, s1->repo, s2->repo, lookup);
            } else if (f1->size != f2->size) {
                same = false;
            } else {
                same = scalar_equal(f1, p1, p2, s1->repo, s2->repo, lookup, s1->registry);
            }
            if (same) continue;

            char before[NMO_DIFF_VALUE_MAX], after[NMO_DIFF_VALUE_MAX];
            format_field_value(before, sizeof(before), f1, p1, s1->repo, s1->ctx);
            if (f2) format_field_value(after, sizeof(after), f2, p2, s2->repo, s2->ctx);
            else snprintf(after, sizeof(after), "(missing)");
            if (strcmp(before, after) == 0) continue;
            total++;
        }
        *out_total = total;
        size_t emit = total < limit ? total : limit;
        *out_count = emit;
        if (!emit) return true;

        nmo_field_diff_t *fds = (nmo_field_diff_t *)nmo_arena_alloc(
            arena, emit * sizeof(nmo_field_diff_t), _Alignof(nmo_field_diff_t));
        if (!fds) return false;
        memset(fds, 0, emit * sizeof(nmo_field_diff_t));

        size_t wi = 0;
        for (size_t i = 0; i < fields && wi < emit; i++) {
            const nmo_type_field_t *f1 = nmo_type_get_field_by_index(t1, i);
            if (!f1 || !f1->name || is_base_embedding(t1, f1)) continue;
            const nmo_type_field_t *f2 = nmo_type_get_field_by_name(t2, f1->name);
            const void *p1 = nmo_field_get_ptr_const(st1, f1);
            const void *p2 = f2 ? nmo_field_get_ptr_const(st2, f2) : NULL;
            bool same = false;
            if (!f2) {
                same = false;
            } else if ((f1->flags & NMO_FIELD_REPEATED) || (f2->flags & NMO_FIELD_REPEATED)) {
                same = repeated_equal(f1, f2, p1, p2, s1->repo, s2->repo, lookup);
            } else if (f1->size != f2->size) {
                same = false;
            } else {
                same = scalar_equal(f1, p1, p2, s1->repo, s2->repo, lookup, s1->registry);
            }
            if (same) continue;

            format_field_value(fds[wi].before, sizeof(fds[wi].before), f1, p1, s1->repo, s1->ctx);
            if (f2) format_field_value(fds[wi].after, sizeof(fds[wi].after), f2, p2, s2->repo, s2->ctx);
            else snprintf(fds[wi].after, sizeof(fds[wi].after), "(missing)");
            if (strcmp(fds[wi].before, fds[wi].after) == 0) continue;
            fds[wi].field_name = nmo_arena_strdup(arena, f1->name);
            if (!fds[wi].field_name) return false;
            wi++;
        }
        *out_count = wi;
        *out_diffs = fds;
        return true;
    }

    if (chunk_equal(obj1, obj2)) return true;
    *out_total = 1;
    *out_count = limit ? 1 : 0;
    if (!*out_count) return true;

    nmo_field_diff_t *fds = (nmo_field_diff_t *)nmo_arena_alloc(
        arena, sizeof(nmo_field_diff_t), _Alignof(nmo_field_diff_t));
    if (!fds) return false;
    memset(fds, 0, sizeof(*fds));
    fds->field_name = nmo_arena_strdup(arena, "chunk_data");
    if (!fds->field_name) return false;

    nmo_chunk_t *c1 = nmo_object_get_chunk(obj1);
    nmo_chunk_t *c2 = nmo_object_get_chunk(obj2);
    size_t sz1 = 0, sz2 = 0;
    if (c1) (void)nmo_chunk_get_data(c1, &sz1);
    if (c2) (void)nmo_chunk_get_data(c2, &sz2);
    snprintf(fds->before, sizeof(fds->before), "<chunk size=%zu>", sz1);
    snprintf(fds->after, sizeof(fds->after), "<chunk size=%zu>", sz2);
    *out_diffs = fds;
    return true;
}

typedef struct {
    uint32_t idx1;
    uint32_t idx2;
    float sim;
} pair_match_t;

static void sort_pairs(pair_match_t *pairs, size_t n, const graph_side_t *g1, const graph_side_t *g2)
{
    if (!pairs || n < 2) return;
    for (size_t i = 1; i < n; i++) {
        pair_match_t key = pairs[i];
        nmo_object_id_t key1 = g1->nodes[key.idx1].id;
        nmo_object_id_t key2 = g2->nodes[key.idx2].id;
        size_t j = i;
        while (j > 0) {
            nmo_object_id_t p1 = g1->nodes[pairs[j - 1].idx1].id;
            nmo_object_id_t p2 = g2->nodes[pairs[j - 1].idx2].id;
            if (p1 < key1) break;
            if (p1 == key1 && p2 <= key2) break;
            pairs[j] = pairs[j - 1];
            j--;
        }
        pairs[j] = key;
    }
}

static bool build_lookup(const graph_side_t *g1,
                         const graph_side_t *g2,
                         const match_state_t *match,
                         nmo_allocator_t *allocator,
                         match_lookup_t *lookup)
{
    memset(lookup, 0, sizeof(*lookup));
    lookup->allocator = allocator;
    size_t count = 0;
    for (size_t i = 0; i < g1->count; i++) if (match->to2_by1[i] >= 0) count++;
    if (!count) return true;
    lookup->ids1 = (nmo_object_id_t *)diff_alloc_array(
        allocator, count, sizeof(nmo_object_id_t), _Alignof(nmo_object_id_t), false);
    lookup->ids2 = (nmo_object_id_t *)diff_alloc_array(
        allocator, count, sizeof(nmo_object_id_t), _Alignof(nmo_object_id_t), false);
    if (!lookup->ids1 || !lookup->ids2) {
        match_lookup_reset(lookup);
        return false;
    }
    lookup->count = count;
    size_t w = 0;
    for (size_t i = 0; i < g1->count; i++) {
        int j = match->to2_by1[i];
        if (j < 0) continue;
        lookup->ids1[w] = g1->nodes[i].id;
        lookup->ids2[w] = g2->nodes[(size_t)j].id;
        w++;
    }
    for (size_t i = 1; i < lookup->count; i++) {
        nmo_object_id_t k1 = lookup->ids1[i];
        nmo_object_id_t k2 = lookup->ids2[i];
        size_t j = i;
        while (j > 0 && lookup->ids1[j - 1] > k1) {
            lookup->ids1[j] = lookup->ids1[j - 1];
            lookup->ids2[j] = lookup->ids2[j - 1];
            j--;
        }
        lookup->ids1[j] = k1;
        lookup->ids2[j] = k2;
    }
    return true;
}

nmo_status_t nmo_diff_objects(
    const nmo_document_t *document1,
    const nmo_document_t *document2,
    const nmo_diff_config_t *config,
    nmo_diff_result_t *result)
{
    nmo_context_t *ctx1 = NULL;
    nmo_context_t *ctx2 = NULL;
    if (!document1 || !document2 || !result) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(result, 0, sizeof(*result));

    nmo_diff_config_t cfg = config ? *config : nmo_diff_config_default();
    if (cfg.min_similarity < 0.0f) cfg.min_similarity = 0.0f;
    if (cfg.min_similarity > 1.0f) cfg.min_similarity = 1.0f;
    if (cfg.rename_similarity < 0.0f) cfg.rename_similarity = 0.0f;
    if (cfg.rename_similarity > 1.0f) cfg.rename_similarity = 1.0f;

    ctx1 = nmo_document_get_context(document1);
    ctx2 = nmo_document_get_context(document2);
    if (!ctx1 || !ctx2) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_allocator_t default_allocator = nmo_allocator_default();
    nmo_allocator_t *allocator = nmo_context_get_allocator(ctx1);
    if (!allocator || !allocator->alloc || !allocator->free) {
        allocator = nmo_context_get_allocator(ctx2);
    }
    if (!allocator || !allocator->alloc || !allocator->free) {
        allocator = &default_allocator;
    }

    nmo_arena_t *arena = nmo_arena_create(allocator, 128 * 1024);
    if (!arena) {
        return NMO_ERR_NOMEM;
    }
    result->arena_ = arena;

    diff_side_t s1 = {
        .ctx = ctx1,
        .registry = nmo_context_get_type_registry(ctx1),
        .repo = nmo_document_get_repository(document1),
    };
    diff_side_t s2 = {
        .ctx = ctx2,
        .registry = nmo_context_get_type_registry(ctx2),
        .repo = nmo_document_get_repository(document2),
    };

    graph_side_t g1, g2;
    if (!build_graph_side(&s1, allocator, &g1) || !build_graph_side(&s2, allocator, &g2)) {
        graph_side_destroy(&g1);
        graph_side_destroy(&g2);
        nmo_diff_result_destroy(result);
        return NMO_ERR_NOMEM;
    }
    result->total_objects1 = g1.count;
    result->total_objects2 = g2.count;

    class_bucket_array_t buckets;
    if (!build_buckets(&g1, &g2, &buckets)) {
        graph_side_destroy(&g1);
        graph_side_destroy(&g2);
        nmo_diff_result_destroy(result);
        return NMO_ERR_NOMEM;
    }

    match_state_t match;
    if (!match_init(&match, g1.count, g2.count, allocator)) {
        buckets_destroy(&buckets);
        graph_side_destroy(&g1);
        graph_side_destroy(&g2);
        nmo_diff_result_destroy(result);
        return NMO_ERR_NOMEM;
    }

    for (size_t i = 0; i < buckets.count; i++) {
        anchor_bucket(&buckets.items[i], &g1, &g2, &s1, &s2, &match);
    }

    class_matrix_array_t mats;
    if (!build_unmatched_matrices(&buckets, &g1, &g2, &s1, &s2, &match, &mats) ||
        !run_flood(&g1, &g2, &match, &mats) ||
        !apply_hungarian(&mats, &match, cfg.min_similarity)) {
        mats_destroy(&mats);
        match_destroy(&match);
        buckets_destroy(&buckets);
        graph_side_destroy(&g1);
        graph_side_destroy(&g2);
        nmo_diff_result_destroy(result);
        return NMO_ERR_NOMEM;
    }

    size_t matched_count = 0;
    for (size_t i = 0; i < g1.count; i++) if (match.to2_by1[i] >= 0) matched_count++;
    pair_match_t *pairs = (pair_match_t *)diff_alloc_array(
        allocator, matched_count, sizeof(pair_match_t), _Alignof(pair_match_t), false);
    if (matched_count && !pairs) {
        mats_destroy(&mats);
        match_destroy(&match);
        buckets_destroy(&buckets);
        graph_side_destroy(&g1);
        graph_side_destroy(&g2);
        nmo_diff_result_destroy(result);
        return NMO_ERR_NOMEM;
    }
    size_t pw = 0;
    for (size_t i = 0; i < g1.count; i++) {
        int j = match.to2_by1[i];
        if (j < 0) continue;
        pairs[pw].idx1 = (uint32_t)i;
        pairs[pw].idx2 = (uint32_t)j;
        pairs[pw].sim = match.sim_by1[i];
        pw++;
    }
    sort_pairs(pairs, matched_count, &g1, &g2);

    match_lookup_t lookup;
    if (!build_lookup(&g1, &g2, &match, allocator, &lookup)) {
        diff_free(allocator, pairs);
        mats_destroy(&mats);
        match_destroy(&match);
        buckets_destroy(&buckets);
        graph_side_destroy(&g1);
        graph_side_destroy(&g2);
        nmo_diff_result_destroy(result);
        return NMO_ERR_NOMEM;
    }

    size_t changed_cap = matched_count;
    if (cfg.max_objects > 0 && changed_cap > cfg.max_objects) changed_cap = cfg.max_objects;
    if (changed_cap) {
        result->changed = (nmo_object_diff_t *)nmo_arena_alloc(
            arena, changed_cap * sizeof(nmo_object_diff_t), _Alignof(nmo_object_diff_t));
        if (!result->changed) {
            match_lookup_reset(&lookup);
            diff_free(allocator, pairs);
            mats_destroy(&mats);
            match_destroy(&match);
            buckets_destroy(&buckets);
            graph_side_destroy(&g1);
            graph_side_destroy(&g2);
            nmo_diff_result_destroy(result);
            return NMO_ERR_NOMEM;
        }
        memset(result->changed, 0, changed_cap * sizeof(nmo_object_diff_t));
    }
    if (matched_count) {
        result->renamed = (nmo_rename_diff_t *)nmo_arena_alloc(
            arena, matched_count * sizeof(nmo_rename_diff_t), _Alignof(nmo_rename_diff_t));
        if (!result->renamed) {
            match_lookup_reset(&lookup);
            diff_free(allocator, pairs);
            mats_destroy(&mats);
            match_destroy(&match);
            buckets_destroy(&buckets);
            graph_side_destroy(&g1);
            graph_side_destroy(&g2);
            nmo_diff_result_destroy(result);
            return NMO_ERR_NOMEM;
        }
        memset(result->renamed, 0, matched_count * sizeof(nmo_rename_diff_t));
    }
    if (g1.count) {
        result->removed = (const nmo_object_t **)nmo_arena_alloc(
            arena, g1.count * sizeof(nmo_object_t *), _Alignof(nmo_object_t *));
        if (!result->removed) {
            match_lookup_reset(&lookup);
            diff_free(allocator, pairs);
            mats_destroy(&mats);
            match_destroy(&match);
            buckets_destroy(&buckets);
            graph_side_destroy(&g1);
            graph_side_destroy(&g2);
            nmo_diff_result_destroy(result);
            return NMO_ERR_NOMEM;
        }
    }
    if (g2.count) {
        result->added = (const nmo_object_t **)nmo_arena_alloc(
            arena, g2.count * sizeof(nmo_object_t *), _Alignof(nmo_object_t *));
        if (!result->added) {
            match_lookup_reset(&lookup);
            diff_free(allocator, pairs);
            mats_destroy(&mats);
            match_destroy(&match);
            buckets_destroy(&buckets);
            graph_side_destroy(&g1);
            graph_side_destroy(&g2);
            nmo_diff_result_destroy(result);
            return NMO_ERR_NOMEM;
        }
    }

    size_t changed_w = 0;
    size_t renamed_w = 0;
    size_t identical = 0;
    for (size_t p = 0; p < matched_count; p++) {
        uint32_t i = pairs[p].idx1;
        uint32_t j = pairs[p].idx2;
        const diff_node_t *n1 = &g1.nodes[i];
        const diff_node_t *n2 = &g2.nodes[j];
        bool renamed = strcmp(norm_name(n1->name), norm_name(n2->name)) != 0 &&
                       pairs[p].sim >= cfg.rename_similarity;

        if (renamed) {
            nmo_rename_diff_t *rd = &result->renamed[renamed_w++];
            rd->obj1 = n1->obj;
            rd->obj2 = n2->obj;
            rd->before_name = nmo_arena_strdup(arena, norm_name(n1->name));
            rd->after_name = nmo_arena_strdup(arena, norm_name(n2->name));
            rd->similarity = pairs[p].sim;
            if (!rd->before_name || !rd->after_name) {
                match_lookup_reset(&lookup);
                diff_free(allocator, pairs);
                mats_destroy(&mats);
                match_destroy(&match);
                buckets_destroy(&buckets);
                graph_side_destroy(&g1);
                graph_side_destroy(&g2);
                nmo_diff_result_destroy(result);
                return NMO_ERR_NOMEM;
            }
        }

        nmo_field_diff_t *fds = NULL;
        size_t fd_count = 0, fd_total = 0;
        if (!build_field_diffs(n1->obj, n2->obj, &s1, &s2, &lookup, cfg.max_fields,
                               arena, &fds, &fd_count, &fd_total)) {
            match_lookup_reset(&lookup);
            diff_free(allocator, pairs);
            mats_destroy(&mats);
            match_destroy(&match);
            buckets_destroy(&buckets);
            graph_side_destroy(&g1);
            graph_side_destroy(&g2);
            nmo_diff_result_destroy(result);
            return NMO_ERR_NOMEM;
        }
        if (fd_total == 0) {
            if (!renamed) identical++;
            continue;
        }
        if (changed_w < changed_cap) {
            nmo_object_diff_t *od = &result->changed[changed_w++];
            od->obj1 = n1->obj;
            od->obj2 = n2->obj;
            od->field_diffs = fds;
            od->field_diff_count = fd_count;
            od->field_diff_total = fd_total;
            od->similarity = pairs[p].sim;
        }
    }
    result->changed_count = changed_w;
    result->renamed_count = renamed_w;
    result->identical_count = identical;

    for (size_t i = 0; i < g1.count; i++) {
        if (match.to2_by1[i] >= 0) continue;
        result->removed[result->removed_count++] = g1.nodes[i].obj;
    }
    for (size_t j = 0; j < g2.count; j++) {
        if (match.to1_by2[j] >= 0) continue;
        result->added[result->added_count++] = g2.nodes[j].obj;
    }

    match_lookup_reset(&lookup);
    diff_free(allocator, pairs);
    mats_destroy(&mats);
    match_destroy(&match);
    buckets_destroy(&buckets);
    graph_side_destroy(&g1);
    graph_side_destroy(&g2);
    return NMO_OK;
}

void nmo_diff_result_destroy(nmo_diff_result_t *result)
{
    if (!result) return;
    if (result->arena_) nmo_arena_destroy(result->arena_);
    memset(result, 0, sizeof(*result));
}
