#include "test_framework.h"

#include "document/nmo_document.h"
#include "runtime/nmo_context.h"
#include "object/nmo_object_diff.h"
#include "session/nmo_session.h"
#include "session/nmo_session_pipeline.h"
#include "core/nmo_array.h"
#include "core/nmo_guid.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_system.h"

#include <string.h>

#define CID_PAIR        0x7F010001u
#define CID_OWNER       0x7F010002u
#define CID_NODE        0x7F010003u
#define CID_REFHOLDER   0x7F010004u
#define CID_TRAP        0x7F010005u
#define CID_COUNTED     0x7F010006u
#define CID_REFARRAY    0x7F010007u
#define CID_REFRECORD   0x7F010008u

#define GUID_PAIR       NMO_GUID(0xA0010001u, 0xB0010001u)
#define GUID_OWNER      NMO_GUID(0xA0010002u, 0xB0010002u)
#define GUID_NODE       NMO_GUID(0xA0010003u, 0xB0010003u)
#define GUID_REFHOLDER  NMO_GUID(0xA0010004u, 0xB0010004u)
#define GUID_TRAP       NMO_GUID(0xA0010005u, 0xB0010005u)
#define GUID_PAIR_ALIAS NMO_GUID(0xA0010006u, 0xB0010006u)
#define GUID_COUNTED    NMO_GUID(0xA0010007u, 0xB0010007u)
#define GUID_REFARRAY   NMO_GUID(0xA0010008u, 0xB0010008u)
#define GUID_REFRECORD  NMO_GUID(0xA0010009u, 0xB0010009u)

typedef struct {
    int32_t a;
    int32_t b;
} pair_state_t;

typedef struct {
    nmo_object_id_t child;
    int32_t tag;
} owner_state_t;

typedef struct {
    nmo_object_id_t parent;
    int32_t value;
} node_state_t;

typedef struct {
    nmo_object_id_t target;
} ref_state_t;

typedef struct {
    int32_t f0;
    int32_t f1;
    int32_t f2;
    int32_t f3;
    int32_t f4;
} trap_state_t;

typedef struct {
    uint32_t value_count;
    int32_t *values;
} counted_state_t;

typedef struct {
    nmo_array_t targets;
} ref_array_state_t;

typedef struct {
    nmo_ref_t target;
} ref_record_state_t;

typedef struct {
    nmo_context_t *ctx;
    nmo_session_t *ses1;
    nmo_session_t *ses2;
    nmo_document_t *doc1;
    nmo_document_t *doc2;
} diff_fixture_t;

static void fixture_destroy(diff_fixture_t *fx);

static nmo_status_t dummy_serialize(const void *instance,
                                    struct nmo_chunk *chunk,
                                    const nmo_type_descriptor_t *type,
                                    void *context) {
    (void)instance;
    (void)chunk;
    (void)type;
    (void)context;
    return NMO_OK;
}

static nmo_status_t dummy_deserialize(void *instance,
                                      struct nmo_chunk *chunk,
                                      const nmo_type_descriptor_t *type,
                                      void *context) {
    (void)instance;
    (void)chunk;
    (void)type;
    (void)context;
    return NMO_OK;
}

static const nmo_type_vtable_t k_dummy_object_vtable = {
    .serialize = dummy_serialize,
    .deserialize = dummy_deserialize,
};

static bool register_test_types(nmo_context_t *ctx) {
    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    if (!registry) return false;
    if (nmo_type_registry_begin_update(registry) != NMO_OK) return false;

    static const nmo_type_field_t pair_fields[] = {
        NMO_FIELD(pair_state_t, a, CKPGUID_INT),
        NMO_FIELD(pair_state_t, b, CKPGUID_INT),
    };
    static const nmo_type_field_t owner_fields[] = {
        NMO_FIELD_REF(owner_state_t, child),
        NMO_FIELD(owner_state_t, tag, CKPGUID_INT),
    };
    static const nmo_type_field_t node_fields[] = {
        NMO_FIELD_REF(node_state_t, parent),
        NMO_FIELD(node_state_t, value, CKPGUID_INT),
    };
    static const nmo_type_field_t ref_fields[] = {
        NMO_FIELD_REF(ref_state_t, target),
    };
    static const nmo_type_field_t trap_fields[] = {
        NMO_FIELD(trap_state_t, f0, CKPGUID_INT),
        NMO_FIELD(trap_state_t, f1, CKPGUID_INT),
        NMO_FIELD(trap_state_t, f2, CKPGUID_INT),
        NMO_FIELD(trap_state_t, f3, CKPGUID_INT),
        NMO_FIELD(trap_state_t, f4, CKPGUID_INT),
    };
    static const nmo_type_field_t counted_fields[] = {
        NMO_FIELD(counted_state_t, value_count, CKPGUID_UINT32),
        NMO_FIELD_ARRAY_COUNTED(
            counted_state_t, values, value_count, 1, CKPGUID_INT),
    };
    static const nmo_type_field_t ref_array_fields[] = {
        NMO_FIELD_REF_RECORD_ARRAY(ref_array_state_t, targets),
    };
    static const nmo_type_field_t ref_record_fields[] = {
        NMO_FIELD_REF_VALUE(ref_record_state_t, target),
    };

    nmo_type_descriptor_t pair_desc = {
        .guid = GUID_PAIR,
        .id = 0,
        .class_id = CID_PAIR,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .name = "DiffPair",
        .description = NULL,
        .base_type = NMO_GUID_NULL,
        .base_type_id = 0,
        .size = sizeof(pair_state_t),
        .alignment = (uint32_t)_Alignof(pair_state_t),
        .fields = pair_fields,
        .field_count = NMO_FIELD_COUNT(pair_fields),
        .vtable = &k_dummy_object_vtable,
    };
    nmo_type_descriptor_t owner_desc = {
        .guid = GUID_OWNER,
        .id = 0,
        .class_id = CID_OWNER,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .name = "DiffOwner",
        .description = NULL,
        .base_type = NMO_GUID_NULL,
        .base_type_id = 0,
        .size = sizeof(owner_state_t),
        .alignment = (uint32_t)_Alignof(owner_state_t),
        .fields = owner_fields,
        .field_count = NMO_FIELD_COUNT(owner_fields),
        .vtable = &k_dummy_object_vtable,
    };
    nmo_type_descriptor_t node_desc = {
        .guid = GUID_NODE,
        .id = 0,
        .class_id = CID_NODE,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .name = "DiffNode",
        .description = NULL,
        .base_type = NMO_GUID_NULL,
        .base_type_id = 0,
        .size = sizeof(node_state_t),
        .alignment = (uint32_t)_Alignof(node_state_t),
        .fields = node_fields,
        .field_count = NMO_FIELD_COUNT(node_fields),
        .vtable = &k_dummy_object_vtable,
    };
    nmo_type_descriptor_t ref_desc = {
        .guid = GUID_REFHOLDER,
        .id = 0,
        .class_id = CID_REFHOLDER,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .name = "DiffRefHolder",
        .description = NULL,
        .base_type = NMO_GUID_NULL,
        .base_type_id = 0,
        .size = sizeof(ref_state_t),
        .alignment = (uint32_t)_Alignof(ref_state_t),
        .fields = ref_fields,
        .field_count = NMO_FIELD_COUNT(ref_fields),
        .vtable = &k_dummy_object_vtable,
    };
    nmo_type_descriptor_t trap_desc = {
        .guid = GUID_TRAP,
        .id = 0,
        .class_id = CID_TRAP,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .name = "DiffTrap",
        .description = NULL,
        .base_type = NMO_GUID_NULL,
        .base_type_id = 0,
        .size = sizeof(trap_state_t),
        .alignment = (uint32_t)_Alignof(trap_state_t),
        .fields = trap_fields,
        .field_count = NMO_FIELD_COUNT(trap_fields),
        .vtable = &k_dummy_object_vtable,
    };
    nmo_type_descriptor_t pair_alias_desc = {
        .guid = GUID_PAIR_ALIAS,
        .id = 0,
        .class_id = 0,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .name = "DiffPairAlias",
        .description = NULL,
        .base_type = NMO_GUID_NULL,
        .base_type_id = 0,
        .size = sizeof(pair_state_t),
        .alignment = (uint32_t)_Alignof(pair_state_t),
        .fields = pair_fields,
        .field_count = NMO_FIELD_COUNT(pair_fields),
        .vtable = NULL,
    };
    nmo_type_descriptor_t counted_desc = {
        .guid = GUID_COUNTED,
        .id = 0,
        .class_id = CID_COUNTED,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_COPYABLE,
        .name = "DiffCounted",
        .description = NULL,
        .base_type = NMO_GUID_NULL,
        .base_type_id = 0,
        .size = sizeof(counted_state_t),
        .alignment = (uint32_t)_Alignof(counted_state_t),
        .fields = counted_fields,
        .field_count = NMO_FIELD_COUNT(counted_fields),
        .vtable = &k_dummy_object_vtable,
    };
    nmo_type_descriptor_t ref_array_desc = {
        .guid = GUID_REFARRAY,
        .id = 0,
        .class_id = CID_REFARRAY,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_COPYABLE,
        .name = "DiffRefArray",
        .description = NULL,
        .base_type = NMO_GUID_NULL,
        .base_type_id = 0,
        .size = sizeof(ref_array_state_t),
        .alignment = (uint32_t)_Alignof(ref_array_state_t),
        .fields = ref_array_fields,
        .field_count = NMO_FIELD_COUNT(ref_array_fields),
        .vtable = &k_dummy_object_vtable,
    };
    nmo_type_descriptor_t ref_record_desc = {
        .guid = GUID_REFRECORD,
        .id = 0,
        .class_id = CID_REFRECORD,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_COPYABLE,
        .name = "DiffRefRecord",
        .description = NULL,
        .base_type = NMO_GUID_NULL,
        .base_type_id = 0,
        .size = sizeof(ref_record_state_t),
        .alignment = (uint32_t)_Alignof(ref_record_state_t),
        .fields = ref_record_fields,
        .field_count = NMO_FIELD_COUNT(ref_record_fields),
        .vtable = &k_dummy_object_vtable,
    };

    if (nmo_type_registry_register(registry, &pair_desc) != NMO_OK) return false;
    if (nmo_type_registry_register(registry, &owner_desc) != NMO_OK) return false;
    if (nmo_type_registry_register(registry, &node_desc) != NMO_OK) return false;
    if (nmo_type_registry_register(registry, &ref_desc) != NMO_OK) return false;
    if (nmo_type_registry_register(registry, &trap_desc) != NMO_OK) return false;
    if (nmo_type_registry_register(registry, &pair_alias_desc) != NMO_OK) return false;
    if (nmo_type_registry_register(registry, &counted_desc) != NMO_OK) return false;
    if (nmo_type_registry_register(registry, &ref_array_desc) != NMO_OK) return false;
    if (nmo_type_registry_register(registry, &ref_record_desc) != NMO_OK) return false;
    if (nmo_type_registry_finalize(registry) != NMO_OK) return false;
    return true;
}

static bool fixture_init(diff_fixture_t *fx) {
    memset(fx, 0, sizeof(*fx));
    nmo_context_desc_t desc = {0};
    fx->ctx = nmo_context_create(&desc);
    if (!fx->ctx) return false;
    if (!register_test_types(fx->ctx)) {
        fixture_destroy(fx);
        return false;
    }
    fx->ses1 = nmo_session_create(fx->ctx);
    fx->ses2 = nmo_session_create(fx->ctx);
    if (!fx->ses1 || !fx->ses2) {
        fixture_destroy(fx);
        return false;
    }
    if (nmo_session_borrow_document(fx->ses1, &fx->doc1) != NMO_OK ||
        nmo_session_borrow_document(fx->ses2, &fx->doc2) != NMO_OK) {
        fixture_destroy(fx);
        return false;
    }
    return true;
}

static void fixture_destroy(diff_fixture_t *fx) {
    if (fx->doc1) nmo_document_destroy(fx->doc1);
    if (fx->doc2) nmo_document_destroy(fx->doc2);
    if (fx->ses1) nmo_session_destroy(fx->ses1);
    if (fx->ses2) nmo_session_destroy(fx->ses2);
    if (fx->ctx) nmo_context_release(fx->ctx);
    memset(fx, 0, sizeof(*fx));
}

static nmo_object_t *add_object(nmo_context_t *ctx,
                                nmo_session_t *ses,
                                nmo_object_id_t id,
                                nmo_class_id_t class_id,
                                const char *name,
                                size_t state_size) {
    const nmo_allocator_t *allocator = nmo_context_get_allocator(ctx);
    nmo_object_repository_t *repo = nmo_session_get_repository(ses);
    nmo_object_t *obj = nmo_object_create(allocator, id, class_id);
    if (!obj) return NULL;
    if (name && nmo_object_set_name(obj, name) != NMO_OK) {
        nmo_object_destroy(obj);
        return NULL;
    }
    if (state_size > 0 && nmo_object_alloc_state(obj, (uint32_t)state_size) != NMO_OK) {
        nmo_object_destroy(obj);
        return NULL;
    }
    if (nmo_object_repository_add(repo, &obj) != NMO_OK) {
        if (obj) nmo_object_destroy(obj);
        return NULL;
    }
    return nmo_object_repository_find_by_id(repo, id);
}

static nmo_status_t run_diff(diff_fixture_t *fx,
                             const nmo_diff_config_t *cfg,
                             nmo_diff_result_t *out) {
    return nmo_diff_objects(fx->doc1, fx->doc2, cfg, out);
}

TEST(object_diff, pure_rename_not_add_remove_not_identical) {
    diff_fixture_t fx;
    ASSERT_TRUE(fixture_init(&fx));

    nmo_object_t *o1 = add_object(fx.ctx, fx.ses1, 1, CID_PAIR, "OldName", sizeof(pair_state_t));
    nmo_object_t *o2 = add_object(fx.ctx, fx.ses2, 101, CID_PAIR, "NewName", sizeof(pair_state_t));
    ASSERT_NOT_NULL(o1);
    ASSERT_NOT_NULL(o2);

    pair_state_t *s1 = (pair_state_t *)nmo_object_get_state(o1);
    pair_state_t *s2 = (pair_state_t *)nmo_object_get_state(o2);
    s1->a = 7; s1->b = 8;
    s2->a = 7; s2->b = 8;

    nmo_diff_result_t diff;
    ASSERT_EQ(NMO_OK, run_diff(&fx, NULL, &diff));
    ASSERT_EQ(1u, diff.renamed_count);
    ASSERT_EQ(0u, diff.changed_count);
    ASSERT_EQ(0u, diff.added_count);
    ASSERT_EQ(0u, diff.removed_count);
    ASSERT_EQ(0u, diff.identical_count);
    nmo_diff_result_destroy(&diff);

    fixture_destroy(&fx);
}

TEST(object_diff, rename_and_changed_both_reported) {
    diff_fixture_t fx;
    ASSERT_TRUE(fixture_init(&fx));

    nmo_object_t *o1 = add_object(fx.ctx, fx.ses1, 1, CID_PAIR, "Before", sizeof(pair_state_t));
    nmo_object_t *o2 = add_object(fx.ctx, fx.ses2, 101, CID_PAIR, "After", sizeof(pair_state_t));
    ASSERT_NOT_NULL(o1);
    ASSERT_NOT_NULL(o2);

    pair_state_t *s1 = (pair_state_t *)nmo_object_get_state(o1);
    pair_state_t *s2 = (pair_state_t *)nmo_object_get_state(o2);
    s1->a = 100; s1->b = 200;
    s2->a = 100; s2->b = 201;

    nmo_diff_config_t cfg = nmo_diff_config_default();
    cfg.rename_similarity = 0.2f;

    nmo_diff_result_t diff;
    ASSERT_EQ(NMO_OK, run_diff(&fx, &cfg, &diff));
    ASSERT_EQ(1u, diff.renamed_count);
    ASSERT_EQ(1u, diff.changed_count);
    ASSERT_EQ(0u, diff.added_count);
    ASSERT_EQ(0u, diff.removed_count);
    nmo_diff_result_destroy(&diff);

    fixture_destroy(&fx);
}

TEST(object_diff, topology_disambiguates_unnamed_nodes) {
    diff_fixture_t fx;
    ASSERT_TRUE(fixture_init(&fx));

    nmo_object_t *oa1 = add_object(fx.ctx, fx.ses1, 1, CID_OWNER, "OwnerA", sizeof(owner_state_t));
    nmo_object_t *ob1 = add_object(fx.ctx, fx.ses1, 2, CID_OWNER, "OwnerB", sizeof(owner_state_t));
    nmo_object_t *na1 = add_object(fx.ctx, fx.ses1, 10, CID_NODE, NULL, sizeof(node_state_t));
    nmo_object_t *nb1 = add_object(fx.ctx, fx.ses1, 11, CID_NODE, NULL, sizeof(node_state_t));
    ASSERT_NOT_NULL(oa1); ASSERT_NOT_NULL(ob1); ASSERT_NOT_NULL(na1); ASSERT_NOT_NULL(nb1);

    nmo_object_t *oa2 = add_object(fx.ctx, fx.ses2, 101, CID_OWNER, "OwnerA", sizeof(owner_state_t));
    nmo_object_t *ob2 = add_object(fx.ctx, fx.ses2, 102, CID_OWNER, "OwnerB", sizeof(owner_state_t));
    nmo_object_t *na2 = add_object(fx.ctx, fx.ses2, 210, CID_NODE, NULL, sizeof(node_state_t));
    nmo_object_t *nb2 = add_object(fx.ctx, fx.ses2, 211, CID_NODE, NULL, sizeof(node_state_t));
    ASSERT_NOT_NULL(oa2); ASSERT_NOT_NULL(ob2); ASSERT_NOT_NULL(na2); ASSERT_NOT_NULL(nb2);

    ((owner_state_t *)nmo_object_get_state(oa1))->child = 10;
    ((owner_state_t *)nmo_object_get_state(oa1))->tag = 1;
    ((owner_state_t *)nmo_object_get_state(ob1))->child = 11;
    ((owner_state_t *)nmo_object_get_state(ob1))->tag = 2;
    ((node_state_t *)nmo_object_get_state(na1))->parent = 1;
    ((node_state_t *)nmo_object_get_state(na1))->value = 42;
    ((node_state_t *)nmo_object_get_state(nb1))->parent = 2;
    ((node_state_t *)nmo_object_get_state(nb1))->value = 42;

    ((owner_state_t *)nmo_object_get_state(oa2))->child = 211;
    ((owner_state_t *)nmo_object_get_state(oa2))->tag = 1;
    ((owner_state_t *)nmo_object_get_state(ob2))->child = 210;
    ((owner_state_t *)nmo_object_get_state(ob2))->tag = 2;
    ((node_state_t *)nmo_object_get_state(na2))->parent = 102;
    ((node_state_t *)nmo_object_get_state(na2))->value = 42;
    ((node_state_t *)nmo_object_get_state(nb2))->parent = 101;
    ((node_state_t *)nmo_object_get_state(nb2))->value = 42;

    nmo_diff_result_t diff;
    ASSERT_EQ(NMO_OK, run_diff(&fx, NULL, &diff));
    ASSERT_EQ(0u, diff.changed_count);
    ASSERT_EQ(0u, diff.renamed_count);
    ASSERT_EQ(0u, diff.added_count);
    ASSERT_EQ(0u, diff.removed_count);
    ASSERT_EQ(4u, diff.identical_count);
    nmo_diff_result_destroy(&diff);

    fixture_destroy(&fx);
}

TEST(object_diff, hungarian_beats_greedy_trap) {
    diff_fixture_t fx;
    ASSERT_TRUE(fixture_init(&fx));

    nmo_object_t *a = add_object(fx.ctx, fx.ses1, 1, CID_TRAP, NULL, sizeof(trap_state_t));
    nmo_object_t *b = add_object(fx.ctx, fx.ses1, 2, CID_TRAP, NULL, sizeof(trap_state_t));
    nmo_object_t *x = add_object(fx.ctx, fx.ses2, 101, CID_TRAP, NULL, sizeof(trap_state_t));
    nmo_object_t *y = add_object(fx.ctx, fx.ses2, 102, CID_TRAP, NULL, sizeof(trap_state_t));
    ASSERT_NOT_NULL(a); ASSERT_NOT_NULL(b); ASSERT_NOT_NULL(x); ASSERT_NOT_NULL(y);

    *(trap_state_t *)nmo_object_get_state(a) = (trap_state_t){0, 0, 0, 0, 0};
    *(trap_state_t *)nmo_object_get_state(b) = (trap_state_t){0, 0, 1, 1, 1};
    *(trap_state_t *)nmo_object_get_state(x) = (trap_state_t){0, 0, 0, 0, 1};
    *(trap_state_t *)nmo_object_get_state(y) = (trap_state_t){1, 1, 0, 0, 0};

    nmo_diff_result_t diff;
    ASSERT_EQ(NMO_OK, run_diff(&fx, NULL, &diff));
    ASSERT_EQ(2u, diff.changed_count);
    ASSERT_EQ(0u, diff.added_count);
    ASSERT_EQ(0u, diff.removed_count);

    ASSERT_EQ(1u, nmo_object_get_id(diff.changed[0].obj1));
    ASSERT_EQ(102u, nmo_object_get_id(diff.changed[0].obj2));
    ASSERT_EQ(2u, nmo_object_get_id(diff.changed[1].obj1));
    ASSERT_EQ(101u, nmo_object_get_id(diff.changed[1].obj2));
    ASSERT_TRUE(diff.changed[0].similarity > 0.2f);
    ASSERT_TRUE(diff.changed[1].similarity > 0.2f);
    ASSERT_EQ(4u, diff.changed[0].field_diff_total + diff.changed[1].field_diff_total);
    nmo_diff_result_destroy(&diff);

    fixture_destroy(&fx);
}

TEST(object_diff, reference_target_rename_compares_equal) {
    diff_fixture_t fx;
    ASSERT_TRUE(fixture_init(&fx));

    nmo_object_t *t1 = add_object(fx.ctx, fx.ses1, 10, CID_PAIR, "TargetOld", sizeof(pair_state_t));
    nmo_object_t *h1 = add_object(fx.ctx, fx.ses1, 20, CID_REFHOLDER, "Holder", sizeof(ref_state_t));
    nmo_object_t *t2 = add_object(fx.ctx, fx.ses2, 110, CID_PAIR, "TargetNew", sizeof(pair_state_t));
    nmo_object_t *h2 = add_object(fx.ctx, fx.ses2, 120, CID_REFHOLDER, "Holder", sizeof(ref_state_t));
    ASSERT_NOT_NULL(t1); ASSERT_NOT_NULL(h1); ASSERT_NOT_NULL(t2); ASSERT_NOT_NULL(h2);

    ((pair_state_t *)nmo_object_get_state(t1))->a = 1;
    ((pair_state_t *)nmo_object_get_state(t1))->b = 2;
    ((pair_state_t *)nmo_object_get_state(t2))->a = 1;
    ((pair_state_t *)nmo_object_get_state(t2))->b = 2;
    ((ref_state_t *)nmo_object_get_state(h1))->target = 10;
    ((ref_state_t *)nmo_object_get_state(h2))->target = 110;

    nmo_diff_result_t diff;
    ASSERT_EQ(NMO_OK, run_diff(&fx, NULL, &diff));
    ASSERT_EQ(1u, diff.renamed_count);
    ASSERT_EQ(0u, diff.changed_count);
    ASSERT_EQ(0u, diff.added_count);
    ASSERT_EQ(0u, diff.removed_count);
    ASSERT_EQ(1u, diff.identical_count);
    nmo_diff_result_destroy(&diff);

    fixture_destroy(&fx);
}

TEST(object_diff, counted_pointer_array_changes_are_reported) {
    diff_fixture_t fx;
    ASSERT_TRUE(fixture_init(&fx));

    nmo_object_t *o1 = add_object(
        fx.ctx, fx.ses1, 1, CID_COUNTED, "Array", sizeof(counted_state_t));
    nmo_object_t *o2 = add_object(
        fx.ctx, fx.ses2, 101, CID_COUNTED, "Array", sizeof(counted_state_t));
    ASSERT_NOT_NULL(o1);
    ASSERT_NOT_NULL(o2);

    int32_t values1[] = {10, 20, 30};
    int32_t values2[] = {10, 20, 30};
    counted_state_t *state1 = nmo_object_get_state(o1);
    counted_state_t *state2 = nmo_object_get_state(o2);
    state1->value_count = 3u;
    state1->values = values1;
    state2->value_count = 3u;
    state2->values = values2;

    nmo_diff_result_t diff;
    ASSERT_EQ(NMO_OK, run_diff(&fx, NULL, &diff));
    ASSERT_EQ(0u, diff.changed_count);
    ASSERT_EQ(1u, diff.identical_count);
    nmo_diff_result_destroy(&diff);

    values2[1] = 21;
    ASSERT_EQ(NMO_OK, run_diff(&fx, NULL, &diff));
    ASSERT_EQ(1u, diff.changed_count);
    ASSERT_EQ(1u, diff.changed[0].field_diff_total);
    ASSERT_EQ(1u, diff.changed[0].field_diff_count);
    ASSERT_STR_EQ("values", diff.changed[0].field_diffs[0].field_name);
    ASSERT_EQ(0u, diff.identical_count);
    nmo_diff_result_destroy(&diff);

    fixture_destroy(&fx);
}

TEST(object_diff, ref_record_arrays_use_matches_and_preserve_invalid_raw_ids) {
    diff_fixture_t fx;
    ASSERT_TRUE(fixture_init(&fx));

    nmo_object_t *target1 = add_object(
        fx.ctx, fx.ses1, 10, CID_PAIR, "Target", sizeof(pair_state_t));
    nmo_object_t *holder1 = add_object(
        fx.ctx, fx.ses1, 20, CID_REFARRAY, "Holder", sizeof(ref_array_state_t));
    nmo_object_t *target2 = add_object(
        fx.ctx, fx.ses2, 110, CID_PAIR, "Target", sizeof(pair_state_t));
    nmo_object_t *holder2 = add_object(
        fx.ctx, fx.ses2, 120, CID_REFARRAY, "Holder", sizeof(ref_array_state_t));
    ASSERT_NOT_NULL(target1);
    ASSERT_NOT_NULL(holder1);
    ASSERT_NOT_NULL(target2);
    ASSERT_NOT_NULL(holder2);

    nmo_ref_t refs1[] = {nmo_ref_from_id(10)};
    nmo_ref_t refs2[] = {nmo_ref_from_id(110)};
    ref_array_state_t *state1 = nmo_object_get_state(holder1);
    ref_array_state_t *state2 = nmo_object_get_state(holder2);
    state1->targets = (nmo_array_t){
        .data = refs1,
        .count = 1u,
        .capacity = 1u,
        .element_size = sizeof(nmo_ref_t),
    };
    state2->targets = (nmo_array_t){
        .data = refs2,
        .count = 1u,
        .capacity = 1u,
        .element_size = sizeof(nmo_ref_t),
    };

    nmo_diff_result_t diff;
    ASSERT_EQ(NMO_OK, run_diff(&fx, NULL, &diff));
    ASSERT_EQ(0u, diff.changed_count);
    ASSERT_EQ(2u, diff.identical_count);
    nmo_diff_result_destroy(&diff);

    refs1[0] = nmo_ref_from_raw(700u);
    refs2[0] = nmo_ref_from_raw(701u);
    ASSERT_EQ(NMO_OK, run_diff(&fx, NULL, &diff));
    ASSERT_EQ(1u, diff.changed_count);
    ASSERT_EQ(1u, diff.changed[0].field_diff_total);
    ASSERT_STR_EQ("targets", diff.changed[0].field_diffs[0].field_name);
    ASSERT_EQ(1u, diff.identical_count);
    nmo_diff_result_destroy(&diff);

    fixture_destroy(&fx);
}

TEST(object_diff, scalar_ref_records_preserve_invalid_raw_ids) {
    diff_fixture_t fx;
    ASSERT_TRUE(fixture_init(&fx));

    nmo_object_t *holder1 = add_object(
        fx.ctx, fx.ses1, 20, CID_REFRECORD, "Holder",
        sizeof(ref_record_state_t));
    nmo_object_t *holder2 = add_object(
        fx.ctx, fx.ses2, 120, CID_REFRECORD, "Holder",
        sizeof(ref_record_state_t));
    ASSERT_NOT_NULL(holder1);
    ASSERT_NOT_NULL(holder2);
    ref_record_state_t *state1 = nmo_object_get_state(holder1);
    ref_record_state_t *state2 = nmo_object_get_state(holder2);
    state1->target = nmo_ref_from_raw(700u);
    state2->target = nmo_ref_from_raw(701u);

    nmo_diff_result_t diff;
    ASSERT_EQ(NMO_OK, run_diff(&fx, NULL, &diff));
    ASSERT_EQ(1u, diff.changed_count);
    ASSERT_EQ(1u, diff.changed[0].field_diff_total);
    ASSERT_STR_EQ("target", diff.changed[0].field_diffs[0].field_name);
    nmo_diff_result_destroy(&diff);

    state2->target = nmo_ref_from_raw(700u);
    ASSERT_EQ(NMO_OK, run_diff(&fx, NULL, &diff));
    ASSERT_EQ(0u, diff.changed_count);
    ASSERT_EQ(1u, diff.identical_count);
    nmo_diff_result_destroy(&diff);

    fixture_destroy(&fx);
}

TEST(object_diff, min_similarity_rejects_low_pairs) {
    diff_fixture_t fx;
    ASSERT_TRUE(fixture_init(&fx));

    nmo_object_t *a = add_object(fx.ctx, fx.ses1, 1, CID_TRAP, NULL, sizeof(trap_state_t));
    nmo_object_t *b = add_object(fx.ctx, fx.ses2, 101, CID_TRAP, NULL, sizeof(trap_state_t));
    ASSERT_NOT_NULL(a); ASSERT_NOT_NULL(b);

    *(trap_state_t *)nmo_object_get_state(a) = (trap_state_t){0, 0, 0, 0, 0};
    *(trap_state_t *)nmo_object_get_state(b) = (trap_state_t){1, 1, 0, 0, 0}; /* sim = 0.6 */

    nmo_diff_config_t cfg = nmo_diff_config_default();
    cfg.min_similarity = 0.7f;

    nmo_diff_result_t diff;
    ASSERT_EQ(NMO_OK, run_diff(&fx, &cfg, &diff));
    ASSERT_EQ(0u, diff.changed_count);
    ASSERT_EQ(0u, diff.renamed_count);
    ASSERT_EQ(1u, diff.removed_count);
    ASSERT_EQ(1u, diff.added_count);
    ASSERT_EQ(0u, diff.identical_count);
    nmo_diff_result_destroy(&diff);

    fixture_destroy(&fx);
}

TEST(object_diff, result_stable_across_repeated_runs) {
    diff_fixture_t fx;
    ASSERT_TRUE(fixture_init(&fx));

    nmo_object_t *a = add_object(fx.ctx, fx.ses1, 1, CID_TRAP, NULL, sizeof(trap_state_t));
    nmo_object_t *b = add_object(fx.ctx, fx.ses1, 2, CID_TRAP, NULL, sizeof(trap_state_t));
    nmo_object_t *x = add_object(fx.ctx, fx.ses2, 101, CID_TRAP, NULL, sizeof(trap_state_t));
    nmo_object_t *y = add_object(fx.ctx, fx.ses2, 102, CID_TRAP, NULL, sizeof(trap_state_t));
    ASSERT_NOT_NULL(a); ASSERT_NOT_NULL(b); ASSERT_NOT_NULL(x); ASSERT_NOT_NULL(y);

    *(trap_state_t *)nmo_object_get_state(a) = (trap_state_t){0, 0, 0, 0, 0};
    *(trap_state_t *)nmo_object_get_state(b) = (trap_state_t){0, 0, 1, 1, 1};
    *(trap_state_t *)nmo_object_get_state(x) = (trap_state_t){0, 0, 0, 0, 1};
    *(trap_state_t *)nmo_object_get_state(y) = (trap_state_t){1, 1, 0, 0, 0};

    nmo_diff_result_t d1;
    nmo_diff_result_t d2;
    ASSERT_EQ(NMO_OK, run_diff(&fx, NULL, &d1));
    ASSERT_EQ(NMO_OK, run_diff(&fx, NULL, &d2));
    ASSERT_EQ(d1.changed_count, d2.changed_count);
    ASSERT_EQ(d1.renamed_count, d2.renamed_count);
    ASSERT_EQ(d1.added_count, d2.added_count);
    ASSERT_EQ(d1.removed_count, d2.removed_count);
    for (size_t i = 0; i < d1.changed_count; i++) {
        ASSERT_EQ(nmo_object_get_id(d1.changed[i].obj1), nmo_object_get_id(d2.changed[i].obj1));
        ASSERT_EQ(nmo_object_get_id(d1.changed[i].obj2), nmo_object_get_id(d2.changed[i].obj2));
        ASSERT_FLOAT_EQ(d1.changed[i].similarity, d2.changed[i].similarity, 0.0001f);
    }
    nmo_diff_result_destroy(&d1);
    nmo_diff_result_destroy(&d2);

    fixture_destroy(&fx);
}

TEST(object_diff, format_path_prefers_explicit_type_view_name) {
    diff_fixture_t fx;
    ASSERT_TRUE(fixture_init(&fx));

    nmo_object_t *obj = add_object(fx.ctx, fx.ses1, 1, CID_PAIR, "AliasTarget", sizeof(pair_state_t));
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(obj, GUID_PAIR_ALIAS));

    char path[128];
    memset(path, 0, sizeof(path));
    nmo_object_format_path(path, sizeof(path), fx.ctx, obj);
    ASSERT_STR_EQ("DiffPairAlias/AliasTarget", path);

    fixture_destroy(&fx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(object_diff, pure_rename_not_add_remove_not_identical);
    REGISTER_TEST(object_diff, rename_and_changed_both_reported);
    REGISTER_TEST(object_diff, topology_disambiguates_unnamed_nodes);
    REGISTER_TEST(object_diff, hungarian_beats_greedy_trap);
    REGISTER_TEST(object_diff, reference_target_rename_compares_equal);
    REGISTER_TEST(object_diff, counted_pointer_array_changes_are_reported);
    REGISTER_TEST(object_diff, ref_record_arrays_use_matches_and_preserve_invalid_raw_ids);
    REGISTER_TEST(object_diff, scalar_ref_records_preserve_invalid_raw_ids);
    REGISTER_TEST(object_diff, min_similarity_rejects_low_pairs);
    REGISTER_TEST(object_diff, result_stable_across_repeated_runs);
    REGISTER_TEST(object_diff, format_path_prefers_explicit_type_view_name);
TEST_MAIN_END()

