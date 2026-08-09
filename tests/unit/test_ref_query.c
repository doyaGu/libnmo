#include "test_framework.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_session_pipeline.h"
#include "../../src/runtime/runtime_internal.h"
#include "document/nmo_document.h"
#include "object/nmo_object_refs.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_place_schemas.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "object/nmo_ref_enumerate.h"
#include "format/nmo_object.h"
#include "core/nmo_array.h"
#include "type/nmo_reflection.h"

#include <stdalign.h>

typedef struct explicit_ref_state {
    nmo_object_id_t target;
} explicit_ref_state_t;

typedef struct strided_ref_element {
    nmo_object_id_t target;
    uint32_t padding[3];
} strided_ref_element_t;

typedef struct strided_ref_state {
    uint32_t element_count;
    strided_ref_element_t *elements;
} strided_ref_state_t;

static const nmo_guid_t explicit_ref_type_guid =
    NMO_GUID_INIT(0xB4A50A10u, 0x00000001u);
static const nmo_guid_t strided_ref_element_guid =
    NMO_GUID_INIT(0xB4A50A11u, 0x00000001u);
static const nmo_guid_t strided_ref_type_guid =
    NMO_GUID_INIT(0xB4A50A12u, 0x00000001u);

static const nmo_type_field_t explicit_ref_fields[] = {
    NMO_FIELD_REF(explicit_ref_state_t, target),
};

#define STRIDED_REF_ELEMENT_GUID_INIT NMO_GUID_INIT(0xB4A50A11u, 0x00000001u)
static const nmo_type_field_t strided_ref_element_fields[] = {
    NMO_FIELD_REF(strided_ref_element_t, target),
};
static const nmo_type_field_t strided_ref_fields[] = {
    NMO_FIELD(strided_ref_state_t, element_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(
        strided_ref_state_t, elements, element_count, 1,
        STRIDED_REF_ELEMENT_GUID),
};

typedef struct direct_ref_capture {
    size_t count;
    nmo_object_id_t targets[4];
} direct_ref_capture_t;

static bool capture_direct_ref(
    void *user_data,
    uint32_t target_id,
    nmo_ref_kind_t kind,
    const char *field_name,
    uint32_t index)
{
    (void)kind;
    (void)field_name;
    (void)index;
    direct_ref_capture_t *capture = (direct_ref_capture_t *)user_data;
    if (capture->count < sizeof(capture->targets) / sizeof(capture->targets[0])) {
        capture->targets[capture->count] = target_id;
    }
    capture->count++;
    return true;
}

static void set_group_members(
    nmo_session_t *session,
    nmo_object_id_t group_id,
    const nmo_object_id_t *member_ids,
    size_t member_count)
{
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *group_obj = nmo_object_repository_find_by_id(repo, group_id);
    ASSERT_NOT_NULL(group_obj);

    nmo_group_state_t *state = (nmo_group_state_t *)group_obj->state;
    ASSERT_NOT_NULL(state);

    nmo_array_clear(&state->object_ids);
    ASSERT_EQ(NMO_OK, nmo_array_reserve(&state->object_ids, member_count));
    if (member_count == 0) {
        return;
    }

    nmo_ref_t *refs = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(&state->object_ids, member_count, (void **)&refs));
    ASSERT_NOT_NULL(refs);

    for (size_t i = 0; i < member_count; ++i) {
        refs[i] = nmo_ref_from_id(member_ids[i]);
    }
}

typedef struct ref_edge_capture {
    size_t count;
    nmo_object_id_t first_from;
    nmo_object_id_t first_to;
    uint32_t first_kind;
} ref_edge_capture_t;

static bool capture_object_ref_edge(
    const nmo_object_refs_edge_t *edge,
    void *user_data)
{
    ref_edge_capture_t *capture = (ref_edge_capture_t *)user_data;
    if (capture == NULL || edge == NULL || edge->edge == NULL) {
        return false;
    }

    if (capture->count == 0) {
        capture->first_from = edge->edge->from;
        capture->first_to = edge->edge->to;
        capture->first_kind = (uint32_t)edge->edge->kind;
    }
    capture->count++;
    return true;
}

static bool count_object_ref_edge(
    const nmo_object_refs_edge_t *edge,
    void *user_data)
{
    size_t *count = (size_t *)user_data;
    if (edge == NULL || edge->edge == NULL || count == NULL) {
        return false;
    }
    (*count)++;
    return true;
}

TEST(ref_query, counts_session_references_without_graph_handles) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_document_t *document = nmo_document_create(ctx);
    ASSERT_NOT_NULL(document);
    nmo_session_t *session = nmo_document_internal_session(document);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t member_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "member",
            (nmo_guid_t){0, 0}, &member_id, NULL));

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_GROUP, "group",
            (nmo_guid_t){0, 0}, &group_id, NULL));

    set_group_members(session, group_id, &member_id, 1);
    nmo_session_invalidate_ref_graph(session);

    nmo_object_refs_result_t result = {0};
    ASSERT_EQ(NMO_OK, nmo_object_refs_iterate(
        document, group_id, NMO_OBJECT_REFS_BOTH, NULL, NULL, &result));
    ASSERT_EQ(1u, result.outgoing);
    ASSERT_EQ(0u, result.incoming);

    ASSERT_EQ(NMO_OK, nmo_object_refs_iterate(
        document, member_id, NMO_OBJECT_REFS_BOTH, NULL, NULL, &result));
    ASSERT_EQ(0u, result.outgoing);
    ASSERT_EQ(1u, result.incoming);

    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(graph);
    nmo_ref_graph_stats_t stats = {0};
    ASSERT_EQ(NMO_OK, nmo_ref_graph_get_stats(graph, &stats));
    ASSERT_EQ(1u, stats.total_edges);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(ref_query, reports_broken_reference_count) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_document_t *document = nmo_document_create(ctx);
    ASSERT_NOT_NULL(document);
    nmo_session_t *session = nmo_document_internal_session(document);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_GROUP, "group",
            (nmo_guid_t){0, 0}, &group_id, NULL));

    nmo_object_id_t missing_member = 999999u;
    set_group_members(session, group_id, &missing_member, 1);
    nmo_session_invalidate_ref_graph(session);

    nmo_object_refs_result_t result = {0};
    ASSERT_EQ(NMO_OK, nmo_object_refs_iterate(
        document, group_id, NMO_OBJECT_REFS_OUTGOING, NULL, NULL, &result));
    ASSERT_EQ(1u, result.outgoing);

    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(graph);
    nmo_ref_edge_t *broken = NULL;
    size_t broken_edges = 0;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_ref_graph_validate(graph, &broken, &broken_edges));
    ASSERT_EQ(1u, broken_edges);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(ref_query, visits_edges_without_exposing_graph_handles) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_document_t *document = nmo_document_create(ctx);
    ASSERT_NOT_NULL(document);
    nmo_session_t *session = nmo_document_internal_session(document);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t member_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "member",
            (nmo_guid_t){0, 0}, &member_id, NULL));

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_GROUP, "group",
            (nmo_guid_t){0, 0}, &group_id, NULL));

    set_group_members(session, group_id, &member_id, 1);
    nmo_session_invalidate_ref_graph(session);

    ref_edge_capture_t capture = {0};
    nmo_object_refs_result_t result = {0};
    ASSERT_EQ(NMO_OK, nmo_object_refs_iterate(
        document,
        group_id,
        NMO_OBJECT_REFS_BOTH,
        capture_object_ref_edge,
        &capture,
        &result));
    ASSERT_EQ(1u, result.outgoing);
    ASSERT_EQ(0u, result.incoming);
    ASSERT_EQ(1u, capture.count);
    ASSERT_EQ(group_id, capture.first_from);
    ASSERT_EQ(member_id, capture.first_to);

    size_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_object_refs_iterate(
        document,
        group_id,
        NMO_OBJECT_REFS_BOTH,
        count_object_ref_edge,
        &count,
        NULL));
    ASSERT_EQ(1u, count);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(ref_query, explicit_type_guid_drives_reference_enumeration) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);
    ASSERT_EQ(NMO_OK, nmo_type_registry_begin_update(registry));

    nmo_type_descriptor_t descriptor = {
        .guid = explicit_ref_type_guid,
        .id = NMO_TYPE_ID_INVALID,
        .class_id = 0,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .name = "ExplicitRefState",
        .size = (uint32_t)sizeof(explicit_ref_state_t),
        .alignment = (uint32_t)alignof(explicit_ref_state_t),
        .fields = explicit_ref_fields,
        .field_count = sizeof(explicit_ref_fields) / sizeof(explicit_ref_fields[0]),
        .base_type = NMO_NULL_GUID,
        .base_type_id = NMO_TYPE_ID_INVALID,
        .creator_plugin_guid = NMO_NULL_GUID,
        .specialized_index = NMO_SPECIALIZED_INDEX_INVALID,
        .valid = true,
    };
    ASSERT_EQ(NMO_OK, nmo_type_registry_register(registry, &descriptor));

    nmo_object_t *obj = nmo_object_create(NULL, 1u, 0);
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(obj, explicit_ref_type_guid));
    ASSERT_EQ(NMO_OK, nmo_object_alloc_state(obj, sizeof(explicit_ref_state_t)));
    explicit_ref_state_t *state =
        (explicit_ref_state_t *)nmo_object_get_state(obj);
    ASSERT_NOT_NULL(state);
    state->target = 42u;

    direct_ref_capture_t capture = {0};
    ASSERT_EQ(NMO_OK, nmo_ref_enumerate_object(
                          registry, obj, capture_direct_ref, &capture));
    ASSERT_EQ(1u, capture.count);
    ASSERT_EQ(42u, capture.targets[0]);

    nmo_object_destroy(obj);
    nmo_context_release(ctx);
}

TEST(ref_query, struct_array_uses_reflected_storage_stride) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);
    ASSERT_EQ(NMO_OK, nmo_type_registry_begin_update(registry));

    nmo_type_descriptor_t element_descriptor = {
        .guid = strided_ref_element_guid,
        .id = NMO_TYPE_ID_INVALID,
        .class_id = 0,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .name = "StridedRefElement",
        .size = sizeof(nmo_object_id_t),
        .alignment = (uint32_t)alignof(nmo_object_id_t),
        .fields = strided_ref_element_fields,
        .field_count = sizeof(strided_ref_element_fields) /
                       sizeof(strided_ref_element_fields[0]),
        .base_type = NMO_NULL_GUID,
        .base_type_id = NMO_TYPE_ID_INVALID,
        .creator_plugin_guid = NMO_NULL_GUID,
        .specialized_index = NMO_SPECIALIZED_INDEX_INVALID,
        .valid = true,
    };
    ASSERT_EQ(NMO_OK, nmo_type_registry_register(registry, &element_descriptor));

    nmo_type_descriptor_t owner_descriptor = {
        .guid = strided_ref_type_guid,
        .id = NMO_TYPE_ID_INVALID,
        .class_id = 0,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .name = "StridedRefState",
        .size = (uint32_t)sizeof(strided_ref_state_t),
        .alignment = (uint32_t)alignof(strided_ref_state_t),
        .fields = strided_ref_fields,
        .field_count = sizeof(strided_ref_fields) / sizeof(strided_ref_fields[0]),
        .base_type = NMO_NULL_GUID,
        .base_type_id = NMO_TYPE_ID_INVALID,
        .creator_plugin_guid = NMO_NULL_GUID,
        .specialized_index = NMO_SPECIALIZED_INDEX_INVALID,
        .valid = true,
    };
    ASSERT_EQ(NMO_OK, nmo_type_registry_register(registry, &owner_descriptor));

    strided_ref_element_t elements[2] = {
        {.target = 11u, .padding = {101u, 102u, 103u}},
        {.target = 22u, .padding = {201u, 202u, 203u}},
    };
    nmo_object_t *obj = nmo_object_create(NULL, 1u, 0);
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(obj, strided_ref_type_guid));
    ASSERT_EQ(NMO_OK, nmo_object_alloc_state(obj, sizeof(strided_ref_state_t)));
    strided_ref_state_t *state =
        (strided_ref_state_t *)nmo_object_get_state(obj);
    ASSERT_NOT_NULL(state);
    state->element_count = 2u;
    state->elements = elements;

    direct_ref_capture_t capture = {0};
    ASSERT_EQ(NMO_OK, nmo_ref_enumerate_object(
                          registry, obj, capture_direct_ref, &capture));
    ASSERT_EQ(2u, capture.count);
    ASSERT_EQ(11u, capture.targets[0]);
    ASSERT_EQ(22u, capture.targets[1]);

    nmo_object_destroy(obj);
    nmo_context_release(ctx);
}

TEST(ref_query, rejects_malformed_struct_array_storage) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_id_t place_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_PLACE, "place", NMO_NULL_GUID,
        &place_id, NULL));
    nmo_object_t *place_object =
        nmo_object_repository_find_by_id(repo, place_id);
    ASSERT_NOT_NULL(place_object);
    nmo_place_state_t *place = (nmo_place_state_t *)place_object->state;
    ASSERT_NOT_NULL(place);

    const nmo_array_t saved_portals = place->portals;
    nmo_place_portal_entry_t portal = {0};
    place->portals.data = &portal;
    place->portals.count = 1;
    place->portals.capacity = 1;
    place->portals.element_size = 1;

    direct_ref_capture_t capture = {0};
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_ref_enumerate_object(
        nmo_context_get_type_registry(ctx), place_object,
        capture_direct_ref, &capture));
    ASSERT_EQ(0u, capture.count);

    place->portals = saved_portals;
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(ref_query, scene_base_references_are_enumerated_once) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = nmo_document_create(ctx);
    ASSERT_NOT_NULL(document);
    nmo_session_t *session = nmo_document_internal_session(document);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t target_id = 0;
    nmo_object_id_t scene_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_BEHAVIOR, "script", (nmo_guid_t){0, 0},
        &target_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_SCENE, "scene", (nmo_guid_t){0, 0},
        &scene_id, NULL));

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_scene_state_t *scene = (nmo_scene_state_t *)
        nmo_object_repository_find_by_id(repo, scene_id)->state;
    ASSERT_NOT_NULL(scene);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &scene->base.scripts, target_id));
    nmo_session_invalidate_ref_graph(session);

    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(graph);
    nmo_ref_edge_t *edges = NULL;
    size_t edge_count = 0;
    ASSERT_EQ(NMO_OK, nmo_ref_graph_get_object_edges(
        graph, scene_id, NMO_REF_DIR_OUTGOING, &edges, &edge_count));
    ASSERT_EQ(1, (int)edge_count);
    ASSERT_EQ(target_id, edges[0].to);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(ref_query, scene_enumeration_rejects_malformed_descriptors) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_id_t target_id = 0;
    nmo_object_id_t scene_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_BEHAVIOR, "target", NMO_NULL_GUID,
        &target_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_SCENE, "scene", NMO_NULL_GUID,
        &scene_id, NULL));
    nmo_object_t *scene_object =
        nmo_object_repository_find_by_id(repo, scene_id);
    ASSERT_NOT_NULL(scene_object);
    nmo_scene_state_t *scene = (nmo_scene_state_t *)scene_object->state;
    ASSERT_NOT_NULL(scene);

    scene->level = nmo_ref_from_id(target_id);
    const size_t saved_element_size = scene->object_descs.element_size;
    scene->object_descs.element_size = 1;

    direct_ref_capture_t capture = {0};
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_ref_enumerate_object(
        nmo_context_get_type_registry(ctx), scene_object,
        capture_direct_ref, &capture));
    ASSERT_EQ(0u, capture.count);

    scene->object_descs.element_size = saved_element_size;
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(ref_query, legacy_beobject_attribute_reference_is_enumerated) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = nmo_document_create(ctx);
    ASSERT_NOT_NULL(document);
    nmo_session_t *session = nmo_document_internal_session(document);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t parameter_id = 0;
    nmo_object_id_t group_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_PARAMETER, "parameter", (nmo_guid_t){0, 0},
        &parameter_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_GROUP, "group", (nmo_guid_t){0, 0},
        &group_id, NULL));

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_group_state_t *group = (nmo_group_state_t *)
        nmo_object_repository_find_by_id(repo, group_id)->state;
    ASSERT_NOT_NULL(group);
    nmo_beobject_legacy_attribute_t attribute = {
        .parameter = nmo_ref_from_id(parameter_id),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &group->base.legacy_attributes, &attribute));
    nmo_session_invalidate_ref_graph(session);

    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(graph);
    nmo_ref_edge_t *edges = NULL;
    size_t edge_count = 0;
    ASSERT_EQ(NMO_OK, nmo_ref_graph_get_object_edges(
        graph, group_id, NMO_REF_DIR_OUTGOING, &edges, &edge_count));
    ASSERT_EQ(1u, edge_count);
    ASSERT_EQ(parameter_id, edges[0].to);
    ASSERT_EQ(NMO_REF_KIND_PARAMETER, edges[0].kind);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(ref_query, character_part_reference_is_enumerated) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = nmo_document_create(ctx);
    ASSERT_NOT_NULL(document);
    nmo_session_t *session = nmo_document_internal_session(document);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t character_id = 0;
    nmo_object_id_t bodypart_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_CHARACTER, "character", (nmo_guid_t){0, 0},
        &character_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_BODYPART, "bodypart", (nmo_guid_t){0, 0},
        &bodypart_id, NULL));

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_character_state_t *character = (nmo_character_state_t *)
        nmo_object_repository_find_by_id(repo, character_id)->state;
    ASSERT_NOT_NULL(character);
    nmo_character_part_t part = {
        .ref = nmo_ref_from_id(bodypart_id),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&character->body_parts, &part));
    nmo_session_invalidate_ref_graph(session);

    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(graph);
    nmo_ref_edge_t *edges = NULL;
    size_t edge_count = 0;
    ASSERT_EQ(NMO_OK, nmo_ref_graph_get_object_edges(
        graph, character_id, NMO_REF_DIR_OUTGOING, &edges, &edge_count));
    ASSERT_EQ(1u, edge_count);
    ASSERT_EQ(bodypart_id, edges[0].to);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(ref_query, explicit_type_guid_drives_reference_enumeration);
    REGISTER_TEST(ref_query, struct_array_uses_reflected_storage_stride);
    REGISTER_TEST(ref_query, rejects_malformed_struct_array_storage);
    REGISTER_TEST(ref_query, counts_session_references_without_graph_handles);
    REGISTER_TEST(ref_query, reports_broken_reference_count);
    REGISTER_TEST(ref_query, visits_edges_without_exposing_graph_handles);
    REGISTER_TEST(ref_query, scene_base_references_are_enumerated_once);
    REGISTER_TEST(ref_query, scene_enumeration_rejects_malformed_descriptors);
    REGISTER_TEST(ref_query, legacy_beobject_attribute_reference_is_enumerated);
    REGISTER_TEST(ref_query, character_part_reference_is_enumerated);
TEST_MAIN_END()



