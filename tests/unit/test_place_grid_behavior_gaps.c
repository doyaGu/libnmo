/**
 * @file test_place_grid_behavior_gaps.c
 * @brief Place, grid, behavior and parameter details compared with the engine's Load
 */

#include "test_framework.h"

#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_grid_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_place_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_statesave_ids.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"

#include <string.h>

/* Old time GUID and the current one the engine maps it to. */
#define OLD_TIME_GUID ((nmo_guid_t){0x4a4d4867u, 0x3c28773fu})

static nmo_chunk_t *new_chunk(nmo_arena_t *arena, nmo_class_id_t class_id,
                              uint32_t data_version, uint32_t options)
{
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    if (chunk == NULL) return NULL;
    chunk->class_id = class_id;
    chunk->data_version = data_version;
    chunk->chunk_options |= options;
    if (nmo_chunk_start_write(chunk) != NMO_OK) return NULL;
    return chunk;
}

static void append_behavior_ref(nmo_array_t *array, nmo_object_id_t id)
{
    nmo_behavior_ref_t item = {.ref = nmo_ref_from_raw(id), .chunk = NULL};
    nmo_array_append(array, &item);
}

TEST(place_grid_behavior_gaps, legacy_building_block_keeps_parameter_and_io_sections)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    nmo_behavior_state_t block;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(&block, NULL, NULL));
    block.flags = CKBEHAVIOR_BUILDINGBLOCK;
    block.block_guid = (nmo_guid_t){0x12345678u, 0x9ABCDEF0u};
    block.block_version = 0x00010203u;
    block.owner = nmo_ref_from_raw(715);
    append_behavior_ref(&block.in_parameters, 721);
    append_behavior_ref(&block.local_parameters, 722);
    append_behavior_ref(&block.out_parameters, 723);
    append_behavior_ref(&block.inputs, 724);
    append_behavior_ref(&block.inputs, 725);
    append_behavior_ref(&block.outputs, 726);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_BEHAVIOR;
    chunk->data_version = 4;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_behavior_serialize(&block, chunk, NULL, &ser_ctx));
    nmo_chunk_close(chunk);

    /* The engine reads the five sections of a building block as well. */
    size_t dwords = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_BEHAVIORINPUTS, &dwords));
    ASSERT_TRUE(dwords > 0u);

    nmo_behavior_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_deserialize(&loaded, chunk, NULL, NULL));
    ASSERT_EQ(1u, loaded.in_parameters.count);
    ASSERT_EQ(1u, loaded.local_parameters.count);
    ASSERT_EQ(1u, loaded.out_parameters.count);
    ASSERT_EQ(2u, loaded.inputs.count);
    ASSERT_EQ(1u, loaded.outputs.count);
    ASSERT_EQ(0u, loaded.sub_behaviors.count);

    /* Only the graph arrays have no place in a building block. */
    append_behavior_ref(&block.sub_behaviors, 730);
    nmo_chunk_t *refused = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(refused);
    refused->class_id = NMO_CID_BEHAVIOR;
    refused->data_version = 4;
    refused->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_behavior_serialize(&block, refused, NULL, &ser_ctx));

    nmo_behavior_vtable.destroy(&block, NULL, NULL);
    nmo_behavior_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(place_grid_behavior_gaps, legacy_building_block_chunk_with_foreign_sections_is_read)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    /* A pre-v5 building block as an older engine wrote it: the fixed
     * NEWDATA record followed by the parameter and input/output sections. */
    nmo_chunk_t *chunk = new_chunk(arena, NMO_CID_BEHAVIOR, 4, NMO_CHUNK_OPTION_FILE);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_BEHAVIORNEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(chunk, (nmo_guid_t){1u, 2u}));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, CKBEHAVIOR_BUILDINGBLOCK));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, NMO_CID_BEOBJECT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(chunk, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0x10000u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_BEHAVIORINPARAMS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(chunk, 2u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(chunk, 801u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(chunk, 802u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_BEHAVIOROUTPUTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(chunk, 1u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(chunk, 803u));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));

    nmo_behavior_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_deserialize(&loaded, chunk, NULL, NULL));
    ASSERT_TRUE((loaded.flags & CKBEHAVIOR_BUILDINGBLOCK) != 0u);
    ASSERT_EQ(2u, loaded.in_parameters.count);
    ASSERT_EQ(1u, loaded.outputs.count);

    nmo_behavior_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

static nmo_status_t load_parameterin_default_data(
    nmo_arena_t *arena, nmo_object_id_t shared, nmo_object_id_t direct,
    nmo_parameterin_state_t *out_state)
{
    nmo_chunk_t *chunk = new_chunk(arena, NMO_CID_PARAMETERIN, 5, 0u);
    if (chunk == NULL) return NMO_ERR_NOMEM;
    nmo_chunk_write_identifier(chunk, CK_STATESAVE_PARAMETERIN_DEFAULTDATA);
    nmo_chunk_write_guid(chunk, CKPGUID_FLOAT);
    nmo_chunk_write_raw_object_id(chunk, 810u);
    nmo_chunk_write_raw_object_id(chunk, shared);
    nmo_chunk_write_raw_object_id(chunk, direct);
    nmo_chunk_close(chunk);
    nmo_status_t result = nmo_chunk_start_read(chunk);
    if (result != NMO_OK) return result;
    result = nmo_parameterin_vtable.create(out_state, NULL, NULL);
    if (result != NMO_OK) return result;
    return nmo_parameterin_deserialize(out_state, chunk, NULL, NULL);
}

TEST(place_grid_behavior_gaps, parameterin_default_data_second_reference_is_the_shared_source)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    /* CKParameterIn::Load: the second reference shares, the third is direct. */
    nmo_parameterin_state_t shared;
    ASSERT_EQ(NMO_OK, load_parameterin_default_data(arena, 811u, 0u, &shared));
    ASSERT_EQ(1u, shared.is_shared);
    ASSERT_EQ(811u, shared.source.raw_id);

    nmo_parameterin_state_t direct;
    ASSERT_EQ(NMO_OK, load_parameterin_default_data(arena, 0u, 812u, &direct));
    ASSERT_EQ(0u, direct.is_shared);
    ASSERT_EQ(812u, direct.source.raw_id);

    /* The same layout is written back: shared source in the second slot. */
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create_nonfile(
        arena, NULL, CK_STATESAVE_PARAMETERIN_ALL);
    nmo_chunk_t *written = new_chunk(arena, NMO_CID_PARAMETERIN, 5, 0u);
    ASSERT_NOT_NULL(written);
    ASSERT_EQ(NMO_OK, nmo_parameterin_serialize(&shared, written, NULL, &ser_ctx));
    nmo_chunk_close(written);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(written));
    size_t dwords = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        written, CK_STATESAVE_PARAMETERIN_DEFAULTDATA, &dwords));
    ASSERT_EQ(5u, dwords);
    nmo_parameterin_state_t again;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(&again, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterin_deserialize(&again, written, NULL, NULL));
    ASSERT_EQ(1u, again.is_shared);
    ASSERT_EQ(811u, again.source.raw_id);

    nmo_chunk_t *written_direct = new_chunk(arena, NMO_CID_PARAMETERIN, 5, 0u);
    ASSERT_NOT_NULL(written_direct);
    ASSERT_EQ(NMO_OK, nmo_parameterin_serialize(
        &direct, written_direct, NULL, &ser_ctx));
    nmo_chunk_close(written_direct);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(written_direct));
    nmo_parameterin_state_t direct_again;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(&direct_again, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterin_deserialize(
        &direct_again, written_direct, NULL, NULL));
    ASSERT_EQ(0u, direct_again.is_shared);
    ASSERT_EQ(812u, direct_again.source.raw_id);

    nmo_arena_destroy(arena);
}

TEST(place_grid_behavior_gaps, parameterin_keeps_the_legacy_type_guid_the_file_holds)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = new_chunk(arena, NMO_CID_PARAMETERIN, 5, 0u);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_PARAMETERIN_DATASOURCE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(chunk, OLD_TIME_GUID));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(chunk, 820u));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));

    nmo_parameterin_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterin_deserialize(&loaded, chunk, NULL, NULL));
    ASSERT_TRUE(nmo_guid_equals(loaded.type_guid, CKPGUID_TIME));
    ASSERT_TRUE(loaded.has_file_type_guid != 0u);
    ASSERT_TRUE(nmo_guid_equals(loaded.file_type_guid, OLD_TIME_GUID));

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create_nonfile(
        arena, NULL, CK_STATESAVE_PARAMETERIN_ALL);
    nmo_chunk_t *written = new_chunk(arena, NMO_CID_PARAMETERIN, 5, 0u);
    ASSERT_NOT_NULL(written);
    ASSERT_EQ(NMO_OK, nmo_parameterin_serialize(&loaded, written, NULL, &ser_ctx));
    nmo_chunk_close(written);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(written));
    size_t dwords = 0u;
    nmo_guid_t guid = NMO_GUID_NULL;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        written, CK_STATESAVE_PARAMETERIN_DATASOURCE, &dwords));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_guid(written, &guid));
    ASSERT_TRUE(nmo_guid_equals(guid, OLD_TIME_GUID));

    /* Once the type is changed the new type is what gets written. */
    loaded.type_guid = CKPGUID_FLOAT;
    nmo_chunk_t *edited = new_chunk(arena, NMO_CID_PARAMETERIN, 5, 0u);
    ASSERT_NOT_NULL(edited);
    ASSERT_EQ(NMO_OK, nmo_parameterin_serialize(&loaded, edited, NULL, &ser_ctx));
    nmo_chunk_close(edited);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(edited));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        edited, CK_STATESAVE_PARAMETERIN_DATASOURCE, &dwords));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_guid(edited, &guid));
    ASSERT_TRUE(nmo_guid_equals(guid, CKPGUID_FLOAT));

    nmo_arena_destroy(arena);
}

TEST(place_grid_behavior_gaps, operation_guid_is_mapped_like_verify_guid_and_the_file_guid_kept)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    const nmo_guid_t old_guid = {0x48C20DEFu, 0xFECD00A0u};
    const nmo_guid_t new_guid = {0x12926657u, 0x6228322Eu};
    const nmo_guid_t other_guid = {0x11111111u, 0x22222222u};

    nmo_chunk_t *chunk = new_chunk(arena, NMO_CID_PARAMETEROPERATION, 7,
                                   NMO_CHUNK_OPTION_FILE);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_OPERATIONOP));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(chunk, old_guid));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));

    nmo_parameteroperation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_deserialize(&loaded, chunk, NULL, NULL));
    ASSERT_TRUE(nmo_guid_equals(loaded.operation_guid, new_guid));
    ASSERT_TRUE(loaded.has_file_operation_guid != 0u);
    ASSERT_TRUE(nmo_guid_equals(loaded.file_operation_guid, old_guid));

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_chunk_t *written = new_chunk(arena, NMO_CID_PARAMETEROPERATION, 7,
                                     NMO_CHUNK_OPTION_FILE);
    ASSERT_NOT_NULL(written);
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_serialize(
        &loaded, written, NULL, &ser_ctx));
    nmo_chunk_close(written);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(written));
    size_t dwords = 0u;
    nmo_guid_t guid = NMO_GUID_NULL;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        written, CK_STATESAVE_OPERATIONOP, &dwords));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_guid(written, &guid));
    ASSERT_TRUE(nmo_guid_equals(guid, old_guid));

    loaded.operation_guid = other_guid;
    nmo_chunk_t *edited = new_chunk(arena, NMO_CID_PARAMETEROPERATION, 7,
                                    NMO_CHUNK_OPTION_FILE);
    ASSERT_NOT_NULL(edited);
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_serialize(
        &loaded, edited, NULL, &ser_ctx));
    nmo_chunk_close(edited);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(edited));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        edited, CK_STATESAVE_OPERATIONOP, &dwords));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_guid(edited, &guid));
    ASSERT_TRUE(nmo_guid_equals(guid, other_guid));

    nmo_arena_destroy(arena);
}

TEST(place_grid_behavior_gaps, parameter_effective_type_guid_maps_the_four_legacy_types)
{
    nmo_parameter_state_t state;
    memset(&state, 0, sizeof(state));

    state.type_guid = CKPGUID_OLDMESSAGE;
    ASSERT_TRUE(nmo_guid_equals(nmo_parameter_effective_type_guid(&state), CKPGUID_MESSAGE));
    state.type_guid = CKPGUID_OLDATTRIBUTE;
    ASSERT_TRUE(nmo_guid_equals(nmo_parameter_effective_type_guid(&state), CKPGUID_ATTRIBUTE));
    state.type_guid = CKPGUID_OLDTIME;
    ASSERT_TRUE(nmo_guid_equals(nmo_parameter_effective_type_guid(&state), CKPGUID_TIME));
    state.type_guid = CKPGUID_ID;
    ASSERT_TRUE(nmo_guid_equals(nmo_parameter_effective_type_guid(&state),
                                (nmo_guid_t){0x30EC20ABu, 0x6DF6517Du}));
    state.type_guid = CKPGUID_FLOAT;
    ASSERT_TRUE(nmo_guid_equals(nmo_parameter_effective_type_guid(&state), CKPGUID_FLOAT));
    /* The state itself keeps what the file holds. */
    ASSERT_TRUE(nmo_guid_equals(state.type_guid, CKPGUID_FLOAT));
}

TEST(place_grid_behavior_gaps, place_keeps_the_portal_entries_the_engine_drops)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    /* RCKPlace::Load reads the count, then two objects per entry, and keeps
     * only the entries with a place. The state keeps all of them. */
    nmo_chunk_t *chunk = new_chunk(arena, NMO_CID_PLACE, 7, NMO_CHUNK_OPTION_FILE);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_PLACEPORTALS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(chunk, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(chunk, 901u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(chunk, 902u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(chunk, 903u));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));

    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_place_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_place_deserialize(&loaded, chunk, NULL, &des_ctx));
    ASSERT_EQ(1u, loaded.has_portals);
    ASSERT_EQ(2u, loaded.portals.count);

    nmo_place_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(place_grid_behavior_gaps, grid_keeps_the_layer_entries_the_engine_checks_away)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    /* RCKGrid::Load runs XObjectArray::Check, which drops entries that do not
     * resolve. The state keeps what the file holds. */
    nmo_chunk_t *chunk = new_chunk(arena, NMO_CID_GRID, 7, NMO_CHUNK_OPTION_FILE);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_GRIDDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 4));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 5));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 3));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(chunk, 3u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(chunk, 910u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(chunk, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(chunk, 912u));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));

    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_grid_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_grid_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_grid_deserialize(&loaded, chunk, NULL, &des_ctx));
    ASSERT_EQ(4, loaded.width);
    ASSERT_EQ(5, loaded.length);
    ASSERT_EQ(3, loaded.priority);
    ASSERT_EQ(3u, loaded.layers.count);

    nmo_grid_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(place_grid_behavior_gaps, legacy_building_block_keeps_parameter_and_io_sections);
    REGISTER_TEST(place_grid_behavior_gaps, legacy_building_block_chunk_with_foreign_sections_is_read);
    REGISTER_TEST(place_grid_behavior_gaps, parameterin_default_data_second_reference_is_the_shared_source);
    REGISTER_TEST(place_grid_behavior_gaps, parameterin_keeps_the_legacy_type_guid_the_file_holds);
    REGISTER_TEST(place_grid_behavior_gaps, operation_guid_is_mapped_like_verify_guid_and_the_file_guid_kept);
    REGISTER_TEST(place_grid_behavior_gaps, parameter_effective_type_guid_maps_the_four_legacy_types);
    REGISTER_TEST(place_grid_behavior_gaps, place_keeps_the_portal_entries_the_engine_drops);
    REGISTER_TEST(place_grid_behavior_gaps, grid_keeps_the_layer_entries_the_engine_checks_away);
TEST_MAIN_END()
