#include "test_framework.h"
#include "runtime/nmo_context.h"
#include "type/nmo_type_system.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_manager_guids.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_serialize_context.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const nmo_class_id_t k_registered_object_cids[] = {
    NMO_CID_OBJECT,
    NMO_CID_SCENEOBJECT,
    NMO_CID_BEOBJECT,
    NMO_CID_RENDEROBJECT,
    NMO_CID_PARAMETER,
    NMO_CID_BEHAVIOR,
    NMO_CID_BEHAVIORIO,
    NMO_CID_BEHAVIORLINK,
    NMO_CID_PARAMETERIN,
    NMO_CID_PARAMETEROUT,
    NMO_CID_PARAMETERLOCAL,
    NMO_CID_PARAMETEROPERATION,
    NMO_CID_SCENE,
    NMO_CID_LEVEL,
    NMO_CID_GROUP,
    NMO_CID_DATAARRAY,
    NMO_CID_PLACE,
    NMO_CID_2DENTITY,
    NMO_CID_SPRITE,
    NMO_CID_SPRITETEXT,
    NMO_CID_3DENTITY,
    NMO_CID_3DOBJECT,
    NMO_CID_CAMERA,
    NMO_CID_TARGETCAMERA,
    NMO_CID_LIGHT,
    NMO_CID_TARGETLIGHT,
    NMO_CID_CHARACTER,
    NMO_CID_BODYPART,
    NMO_CID_SPRITE3D,
    NMO_CID_CURVE,
    NMO_CID_CURVEPOINT,
    NMO_CID_MATERIAL,
    NMO_CID_TEXTURE,
    NMO_CID_MESH,
    NMO_CID_PATCHMESH,
    NMO_CID_GRID,
    NMO_CID_LAYER,
    NMO_CID_ANIMATION,
    NMO_CID_KEYEDANIMATION,
    NMO_CID_OBJECTANIMATION,
    NMO_CID_RENDERCONTEXT,
    NMO_CID_KINEMATICCHAIN,
    NMO_CID_SYNCHRO,
    NMO_CID_STATE,
    NMO_CID_CRITICALSECTION,
    NMO_CID_SOUND,
    NMO_CID_WAVESOUND,
    NMO_CID_MIDISOUND,
    NMO_CID_INTERFACEOBJECTMANAGER
};

static const nmo_class_id_t k_builtin_schema_cids[] = {
    NMO_CID_OBJECT,
    NMO_CID_SCENEOBJECT,
    NMO_CID_BEOBJECT,
    NMO_CID_RENDEROBJECT,
    NMO_CID_PARAMETER,
    NMO_CID_BEHAVIOR,
    NMO_CID_BEHAVIORIO,
    NMO_CID_BEHAVIORLINK,
    NMO_CID_PARAMETERIN,
    NMO_CID_PARAMETEROUT,
    NMO_CID_PARAMETERLOCAL,
    NMO_CID_PARAMETEROPERATION,
    NMO_CID_SCENE,
    NMO_CID_LEVEL,
    NMO_CID_GROUP,
    NMO_CID_DATAARRAY,
    NMO_CID_PLACE,
    NMO_CID_2DENTITY,
    NMO_CID_SPRITE,
    NMO_CID_SPRITETEXT,
    NMO_CID_3DENTITY,
    NMO_CID_3DOBJECT,
    NMO_CID_CAMERA,
    NMO_CID_TARGETCAMERA,
    NMO_CID_LIGHT,
    NMO_CID_TARGETLIGHT,
    NMO_CID_CHARACTER,
    NMO_CID_SPRITE3D,
    NMO_CID_CURVE,
    NMO_CID_MATERIAL,
    NMO_CID_TEXTURE,
    NMO_CID_MESH,
    NMO_CID_PATCHMESH,
    NMO_CID_GRID,
    NMO_CID_LAYER,
    NMO_CID_ANIMATION,
    NMO_CID_RENDERCONTEXT,
    NMO_CID_KINEMATICCHAIN,
    NMO_CID_SYNCHRO,
    NMO_CID_SOUND,
    NMO_CID_INTERFACEOBJECTMANAGER,
};

TEST(type_runtime_hook_coverage, registered_types_have_explicit_remap_hook) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);

    for (size_t i = 0; i < sizeof(k_registered_object_cids) / sizeof(k_registered_object_cids[0]); i++) {
        const nmo_class_id_t cid = k_registered_object_cids[i];
        const nmo_type_descriptor_t *type = nmo_type_registry_find_by_class_id_inherited(registry, cid);
        ASSERT_NOT_NULL(type);
        ASSERT_NOT_NULL(type->vtable);
        ASSERT_NOT_NULL(type->vtable->prepare_dependencies);
        ASSERT_NOT_NULL(type->vtable->remap_dependencies);
    }

    nmo_context_release(ctx);
}

TEST(type_runtime_hook_coverage, builtin_schemas_round_trip_default_state) {
    const size_t object_schema_count = sizeof(k_builtin_schema_cids) /
                                       sizeof(k_builtin_schema_cids[0]);
    const size_t manager_schema_count = 2u;
    ASSERT_EQ(43u, object_schema_count + manager_schema_count);

    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);
    const nmo_type_runtime_t *runtime = nmo_context_get_type_runtime(ctx);
    ASSERT_NOT_NULL(runtime);

    const nmo_type_descriptor_t *schema_types[43];
    for (size_t i = 0; i < object_schema_count; ++i) {
        schema_types[i] = nmo_type_registry_find_by_class_id(
            registry, k_builtin_schema_cids[i]);
    }
    schema_types[object_schema_count] = nmo_type_registry_find_by_guid(
        registry, NMO_MANAGER_GUID_ATTRIBUTE);
    schema_types[object_schema_count + 1u] = nmo_type_registry_find_by_guid(
        registry, NMO_MANAGER_GUID_MESSAGE);

    for (size_t i = 0; i < 43u; ++i) {
        const nmo_type_descriptor_t *type = schema_types[i];
        if (type == NULL) {
            fprintf(stderr, "built-in schema %zu is not registered\n", i);
        }
        ASSERT_NOT_NULL(type);
        const nmo_class_id_t cid = type->class_id;
        if (i < object_schema_count) {
            ASSERT_EQ(k_builtin_schema_cids[i], cid);
        }
        ASSERT_NOT_NULL(type->vtable);
        ASSERT_NOT_NULL(type->vtable->create);
        ASSERT_NOT_NULL(type->vtable->destroy);
        ASSERT_NOT_NULL(type->vtable->copy);
        ASSERT_NOT_NULL(type->vtable->serialize);
        ASSERT_NOT_NULL(type->vtable->deserialize);
        ASSERT_NOT_NULL(type->vtable->equals);
        ASSERT_NOT_NULL(type->vtable->hash);

        nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
        ASSERT_NOT_NULL(arena);
        void *source = calloc(1u, type->size);
        void *loaded = calloc(1u, type->size);
        void *copied = calloc(1u, type->size);
        ASSERT_NOT_NULL(source);
        ASSERT_NOT_NULL(loaded);
        ASSERT_NOT_NULL(copied);
        ASSERT_EQ(NMO_OK, type->vtable->create(source, type, NULL));
        ASSERT_EQ(NMO_OK, type->vtable->create(loaded, type, NULL));
        ASSERT_EQ(NMO_OK, type->vtable->create(copied, type, NULL));

        nmo_chunk_t *chunk = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(chunk);
        chunk->class_id = cid;
        chunk->chunk_version = NMO_CHUNK_VERSION4;
        chunk->data_version = 8;
        chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
        nmo_serialize_context_t serialize_context =
            nmo_serialize_context_create(
                arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
        nmo_status_t status = type->vtable->serialize(
            source, chunk, type, &serialize_context);
        if (status != NMO_OK) {
            fprintf(stderr, "schema %s (%u) serialize failed: %d\n",
                    type->name, cid, status);
        }
        ASSERT_EQ(NMO_OK, status);
        nmo_chunk_close(chunk);

        status = type->vtable->copy(source, copied, type, arena);
        if (status != NMO_OK) {
            fprintf(stderr, "schema %s (%u) copy failed: %d\n",
                    type->name, cid, status);
        }
        ASSERT_EQ(NMO_OK, status);
        ASSERT_EQ(NMO_OK, type->vtable->validate(copied, type, NULL));
        ASSERT_TRUE(type->vtable->equals(source, copied));
        ASSERT_EQ(type->vtable->hash(source), type->vtable->hash(copied));

        nmo_deserialize_context_t deserialize_context =
            nmo_deserialize_context_create(
                arena, NULL, runtime, NMO_DESER_FLAG_FILE_MODE);
        ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
        status = type->vtable->deserialize(
            loaded, chunk, type, &deserialize_context);
        if (status != NMO_OK) {
            fprintf(stderr, "schema %s (%u) deserialize failed: %d\n",
                    type->name, cid, status);
            const uint32_t *dwords = NMO_ARENA_ARRAY_DATA(
                uint32_t, &chunk->data);
            for (size_t j = 0; j < chunk->data.count; ++j) {
                fprintf(stderr, "  [%zu] = 0x%08X\n", j, dwords[j]);
            }
        }
        ASSERT_EQ(NMO_OK, status);
        ASSERT_EQ(NMO_OK, type->vtable->validate(loaded, type, NULL));

        nmo_chunk_t *round_trip = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(round_trip);
        round_trip->class_id = cid;
        round_trip->chunk_version = NMO_CHUNK_VERSION4;
        round_trip->data_version = 8;
        round_trip->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, type->vtable->serialize(
            loaded, round_trip, type, &serialize_context));
        nmo_chunk_close(round_trip);

        void *serialized = NULL;
        void *round_trip_serialized = NULL;
        size_t serialized_size = 0;
        size_t round_trip_size = 0;
        ASSERT_EQ(NMO_OK, nmo_chunk_serialize(
            chunk, &serialized, &serialized_size, arena));
        ASSERT_EQ(NMO_OK, nmo_chunk_serialize(
            round_trip, &round_trip_serialized, &round_trip_size, arena));
        if (serialized_size != round_trip_size ||
            memcmp(serialized, round_trip_serialized, serialized_size) != 0) {
            fprintf(stderr, "schema %s (%u) is not byte-stable\n",
                    type->name, cid);
            const size_t common_dwords =
                (serialized_size < round_trip_size
                    ? serialized_size : round_trip_size) / sizeof(uint32_t);
            const uint32_t *first = (const uint32_t *)serialized;
            const uint32_t *second = (const uint32_t *)round_trip_serialized;
            for (size_t j = 0; j < common_dwords; ++j) {
                if (first[j] != second[j]) {
                    fprintf(stderr,
                            "  [%zu] 0x%08X -> 0x%08X\n",
                            j, first[j], second[j]);
                }
            }
            fprintf(stderr,
                    "  data=%zu/%zu ids=%zu/%zu chunks=%zu/%zu refs=%zu/%zu options=0x%X/0x%X\n",
                    chunk->data.count, round_trip->data.count,
                    chunk->ids.count, round_trip->ids.count,
                    chunk->chunks.count, round_trip->chunks.count,
                    chunk->chunk_refs.count, round_trip->chunk_refs.count,
                    chunk->chunk_options, round_trip->chunk_options);
            const size_t common_data = chunk->data.count < round_trip->data.count
                ? chunk->data.count : round_trip->data.count;
            const uint32_t *first_data = NMO_ARENA_ARRAY_DATA(
                uint32_t, &chunk->data);
            const uint32_t *second_data = NMO_ARENA_ARRAY_DATA(
                uint32_t, &round_trip->data);
            for (size_t j = 0; j < common_data; ++j) {
                if (first_data[j] != second_data[j]) {
                    fprintf(stderr,
                            "  data[%zu] 0x%08X -> 0x%08X\n",
                            j, first_data[j], second_data[j]);
                }
            }
        }
        ASSERT_EQ(serialized_size, round_trip_size);
        ASSERT_EQ(0, memcmp(
            serialized, round_trip_serialized, serialized_size));

        if (chunk->data.count > 0u) {
            nmo_chunk_t *truncated = nmo_chunk_clone(chunk, arena);
            ASSERT_NOT_NULL(truncated);
            truncated->data.count = 1u;
            void *failed = calloc(1u, type->size);
            ASSERT_NOT_NULL(failed);
            ASSERT_EQ(NMO_OK, type->vtable->create(failed, type, NULL));
            ASSERT_EQ(NMO_OK, nmo_chunk_start_read(truncated));
            status = type->vtable->deserialize(
                failed, truncated, type, &deserialize_context);
            if (status == NMO_OK) {
                fprintf(stderr, "schema %s (%u) accepted truncated output\n",
                        type->name, cid);
            }
            ASSERT_NE(NMO_OK, status);
            ASSERT_TRUE(type->vtable->equals(failed, source));
            type->vtable->destroy(failed, type, NULL);
            free(failed);
        }

        type->vtable->destroy(copied, type, NULL);
        type->vtable->destroy(loaded, type, NULL);
        type->vtable->destroy(source, type, NULL);
        free(copied);
        free(loaded);
        free(source);
        nmo_arena_destroy(arena);
    }

    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(type_runtime_hook_coverage, registered_types_have_explicit_remap_hook);
REGISTER_TEST(type_runtime_hook_coverage, builtin_schemas_round_trip_default_state);
TEST_MAIN_END()

