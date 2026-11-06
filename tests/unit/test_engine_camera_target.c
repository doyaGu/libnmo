#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof(*(a)))
#define GUID_SET_TARGET NMO_GUID(0x74123741,0x4563ffff)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct target_fixture {
    engine_fixture_t f;
    nmo_object_id_t camera, second_camera, first, second, child, derived, block, after, camera_value, target_value;
} target_fixture_t;

static void place(target_fixture_t *g, nmo_object_id_t id, eng_vector_t p, nmo_object_id_t parent)
{
    nmo_3dentity_state_t *state = fixture_state(&g->f,id); ASSERT_NOT_NULL(state);
    state->has_entityndata_chunk = 1; state->parent = nmo_ref_from_id(parent);
    state->world_matrix[12] = p.x; state->world_matrix[13] = p.y; state->world_matrix[14] = p.z;
}

static void begin(target_fixture_t *g, bool attached)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->camera = fixture_create(&g->f,NMO_CID_TARGETCAMERA,"Target Camera"); g->second_camera = fixture_create(&g->f,NMO_CID_TARGETCAMERA,"Other Target Camera");
    g->first = fixture_create(&g->f,NMO_CID_3DENTITY,"First Aim"); g->second = fixture_create(&g->f,NMO_CID_3DENTITY,"Second Aim");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Aim Child"); g->derived = fixture_create(&g->f,NMO_CID_CAMERA,"Derived Entity");
    ASSERT_TRUE(g->camera && g->second_camera && g->first && g->second && g->child && g->derived);
    place(g,g->camera,(eng_vector_t){0,0,0},0); place(g,g->second_camera,(eng_vector_t){0,0,0},0);
    place(g,g->first,(eng_vector_t){10,0,10},0); place(g,g->second,(eng_vector_t){-10,0,10},0); place(g,g->child,(eng_vector_t){11,2,13},g->first);
    place(g,g->derived,(eng_vector_t){20,5,30},0);
    nmo_targetcamera_state_t *camera = fixture_state(&g->f,g->camera);
    camera->has_target = attached; camera->target = nmo_ref_from_id(attached ? g->first : 0);
    ((nmo_3dentity_state_t *)fixture_state(&g->f,g->first))->entity_flags = attached ? CK_3DENTITY_TARGETCAMERA : CK_3DENTITY_FRAME;
    ((nmo_3dentity_state_t *)fixture_state(&g->f,g->second))->entity_flags = CK_3DENTITY_FRAME | CK_3DENTITY_TARGETLIGHT;
    g->f.script_id = fixture_add_script(&g->f,"Camera Target Script",&g->f.script_in); ASSERT_TRUE(g->f.script_id != 0); fixture_begin_edit(&g->f);
}

static void ready(target_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL)); g->f.engine->render.camera = g->camera;
}

static uint32_t flags(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = eng_entity_index(engine,id); return index >= 0 ? engine->entities.data[index].entity_flags : 0;
}

static void direction(target_fixture_t *g, eng_vector_t expected)
{
    nmo_engine_frame_t view; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g->f.engine,&view)); ASSERT_EQ(g->camera,view.camera);
    ASSERT_TRUE(isfinite(view.view[8]) && isfinite(view.view[9]) && isfinite(view.view[10]));
    ASSERT_FLOAT_EQ(expected.x,view.view[8],1e-5f); ASSERT_FLOAT_EQ(expected.y,view.view[9],1e-5f); ASSERT_FLOAT_EQ(expected.z,view.view[10],1e-5f);
}

TEST(engine_camera_target, runtime_targets_override_file_references_and_follow_moving_entities_without_document_changes)
{
    target_fixture_t g; begin(&g,true); ready(&g); float h = sqrtf(.5f);
    ASSERT_EQ(g.first,eng_camera_target(g.f.engine,g.camera)); direction(&g,(eng_vector_t){h,0,h});
    ASSERT_TRUE(eng_set_camera_target(g.f.engine,g.camera,g.second)); direction(&g,(eng_vector_t){-h,0,h});
    eng_vector_t moved = {0,10,10}; eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.second),&moved,-1,false); direction(&g,(eng_vector_t){0,h,h});
    const nmo_targetcamera_state_t *source = fixture_state(&g.f,g.camera); ASSERT_EQ(g.first,nmo_ref_runtime_id(&source->target));
    ASSERT_EQ(CK_3DENTITY_TARGETCAMERA,((nmo_3dentity_state_t *)fixture_state(&g.f,g.first))->entity_flags);
    nmo_engine_t *other = NULL; ASSERT_EQ(NMO_OK,nmo_engine_create(g.f.workspace,NULL,&other)); ASSERT_EQ(NMO_OK,nmo_engine_start(other,NMO_ENGINE_START_ALL));
    ASSERT_EQ(g.first,eng_camera_target(other,g.camera)); nmo_engine_destroy(other); fixture_end(&g.f);
}

TEST(engine_camera_target, attaching_detaching_and_self_assignments_keep_native_target_flags)
{
    target_fixture_t g; begin(&g,false); ready(&g);
    ASSERT_TRUE(eng_set_camera_target(g.f.engine,g.camera,g.first)); ASSERT_EQ(CK_3DENTITY_TARGETCAMERA,flags(g.f.engine,g.first));
    ASSERT_TRUE(eng_set_camera_target(g.f.engine,g.camera,g.camera)); ASSERT_EQ(g.first,eng_camera_target(g.f.engine,g.camera));
    ASSERT_TRUE(eng_set_camera_target(g.f.engine,g.camera,g.second)); ASSERT_EQ(CK_3DENTITY_FRAME,flags(g.f.engine,g.first));
    ASSERT_EQ(CK_3DENTITY_TARGETCAMERA | CK_3DENTITY_TARGETLIGHT,flags(g.f.engine,g.second));
    ASSERT_TRUE(eng_set_camera_target(g.f.engine,g.camera,0)); ASSERT_EQ(0u,eng_camera_target(g.f.engine,g.camera));
    ASSERT_EQ(CK_3DENTITY_FRAME | CK_3DENTITY_TARGETLIGHT,flags(g.f.engine,g.second));
    ASSERT_FALSE(eng_set_camera_target(g.f.engine,g.derived,g.first)); fixture_end(&g.f);
}

TEST(engine_camera_target, clearing_a_file_target_stays_cleared_and_repeating_the_same_target_does_not_repair_flags)
{
    target_fixture_t g; begin(&g,true); ready(&g);
    ASSERT_TRUE(eng_set_camera_target(g.f.engine,g.camera,0)); ASSERT_EQ(0u,eng_camera_target(g.f.engine,g.camera));
    ASSERT_TRUE(eng_set_camera_target(g.f.engine,g.camera,g.first));
    g.f.engine->entities.data[eng_entity_index(g.f.engine,g.first)].entity_flags = CK_3DENTITY_FRAME;
    ASSERT_TRUE(eng_set_camera_target(g.f.engine,g.camera,g.first)); ASSERT_EQ(CK_3DENTITY_FRAME,flags(g.f.engine,g.first));
    fixture_end(&g.f);
}

TEST(engine_camera_target, deleting_a_target_clears_all_attachments_and_deleting_a_camera_releases_its_target)
{
    target_fixture_t g; begin(&g,true); ready(&g);
    ASSERT_TRUE(eng_set_camera_target(g.f.engine,g.second_camera,g.first)); nmo_engine_destroy_object(g.f.engine,g.first);
    ASSERT_EQ(0u,eng_camera_target(g.f.engine,g.camera)); ASSERT_EQ(0u,eng_camera_target(g.f.engine,g.second_camera));
    ASSERT_TRUE(eng_set_camera_target(g.f.engine,g.camera,g.second)); nmo_engine_destroy_object(g.f.engine,g.camera);
    ASSERT_TRUE(eng_entity_index(g.f.engine,g.second) >= 0); ASSERT_EQ(CK_3DENTITY_FRAME | CK_3DENTITY_TARGETLIGHT,flags(g.f.engine,g.second));
    ASSERT_FALSE(eng_set_camera_target(g.f.engine,g.camera,g.second)); fixture_end(&g.f);
}

static void behavior_begin(target_fixture_t *g, bool attached, uint32_t version)
{
    begin(g,attached); g->block = fixture_block(&g->f,GUID_SET_TARGET,"Set Camera Target"); g->after = fixture_block(&g->f,GUID_NOP,"After Camera Target");
    ASSERT_TRUE(g->block != 0 && g->after != 0); ((nmo_behavior_state_t *)fixture_state(&g->f,g->block))->block_version = version;
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->camera);
    g->camera_value = nmo_parameterin_source_id(fixture_state(&g->f,fixture_target(&g->f,g->block)));
    ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(g->f.tx,BLOCK_PIN(&g->f,g->block,0),&g->target_value));
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->after,0),0);
}

static void fire(target_fixture_t *g, nmo_object_id_t target)
{
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g->f.engine,g->target_value,&target,sizeof(target)));
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,0));
}

static void position(target_fixture_t *g, nmo_object_id_t id, eng_vector_t expected)
{
    float world[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g->f.engine,id,world)); ASSERT_TRUE(isfinite(world[12]) && isfinite(world[13]) && isfinite(world[14]));
    ASSERT_FLOAT_EQ(expected.x,world[12],1e-5f); ASSERT_FLOAT_EQ(expected.y,world[13],1e-5f); ASSERT_FLOAT_EQ(expected.z,world[14],1e-5f);
}

TEST(engine_camera_target, modern_default_clears_the_attachment_and_runs_once)
{
    target_fixture_t g; behavior_begin(&g,true,0x20000); ready(&g); ASSERT_TRUE(nmo_engine_has_block(g.f.engine,GUID_SET_TARGET));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(0u,eng_camera_target(g.f.engine,g.camera)); ASSERT_EQ(CK_3DENTITY_FRAME,flags(g.f.engine,g.first));
    ASSERT_EQ(1u,runs(&g.f,g.after)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_FALSE(nmo_engine_behavior_is_active(g.f.engine,g.block));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); ASSERT_EQ(1u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST(engine_camera_target, modern_versions_accept_exact_entities_and_clear_derived_missing_or_deleted_targets)
{
    const uint32_t versions[] = {0x10001,0x1ffff,0x20000};
    for (size_t i = 0; i < COUNT(versions); i++) {
        target_fixture_t g; behavior_begin(&g,true,versions[i]); ready(&g);
        fire(&g,g.second); ASSERT_EQ(g.second,eng_camera_target(g.f.engine,g.camera));
        fire(&g,g.derived); ASSERT_EQ(0u,eng_camera_target(g.f.engine,g.camera));
        fire(&g,g.child); ASSERT_EQ(g.child,eng_camera_target(g.f.engine,g.camera));
        fire(&g,0); ASSERT_EQ(0u,eng_camera_target(g.f.engine,g.camera));
        fire(&g,g.second); nmo_engine_destroy_object(g.f.engine,g.second); fire(&g,g.second); ASSERT_EQ(0u,eng_camera_target(g.f.engine,g.camera));
        ASSERT_EQ(6u,runs(&g.f,g.after)); fixture_end(&g.f);
    }
}

TEST(engine_camera_target, legacy_behavior_moves_the_existing_target_and_its_children_to_the_input_world_position)
{
    target_fixture_t g; behavior_begin(&g,true,0x10000); ready(&g);
    fire(&g,g.second); ASSERT_EQ(g.first,eng_camera_target(g.f.engine,g.camera)); position(&g,g.first,(eng_vector_t){-10,0,10}); position(&g,g.child,(eng_vector_t){-9,2,13});
    ASSERT_EQ(CK_3DENTITY_FRAME | CK_3DENTITY_TARGETLIGHT,flags(g.f.engine,g.second));
    fire(&g,g.derived); position(&g,g.first,(eng_vector_t){20,5,30}); position(&g,g.child,(eng_vector_t){21,7,33});
    ASSERT_EQ(g.first,eng_camera_target(g.f.engine,g.camera)); fixture_end(&g.f);
}

TEST(engine_camera_target, legacy_behavior_attaches_derived_entities_when_empty_and_ignores_self_attachment)
{
    target_fixture_t g; behavior_begin(&g,false,0x10000); ready(&g);
    fire(&g,g.camera); ASSERT_EQ(0u,eng_camera_target(g.f.engine,g.camera));
    fire(&g,g.derived); ASSERT_EQ(g.derived,eng_camera_target(g.f.engine,g.camera)); ASSERT_TRUE((flags(g.f.engine,g.derived) & CK_3DENTITY_TARGETCAMERA) != 0);
    fixture_end(&g.f);
}

TEST(engine_camera_target, legacy_missing_targets_fail_after_consuming_input_and_activating_output)
{
    for (int deleted = 0; deleted < 2; deleted++) {
        target_fixture_t g; behavior_begin(&g,true,0x10000); ready(&g); nmo_object_id_t value = deleted ? g.second : 0;
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&value,sizeof(value))); if (deleted) nmo_engine_destroy_object(g.f.engine,g.second);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t error; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&error)); ASSERT_EQ(NMO_ENGINE_BR_PARAMETERERROR,error.result);
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0)));
        ASSERT_EQ(g.first,eng_camera_target(g.f.engine,g.camera)); ASSERT_EQ(0u,runs(&g.f,g.after)); fixture_end(&g.f);
    }
}

TEST(engine_camera_target, invalid_camera_owners_fail_before_advancing_io)
{
    for (int kind = 0; kind < 5; kind++) {
        target_fixture_t g; behavior_begin(&g,true,0x10000); ready(&g);
        nmo_object_id_t value = kind == 0 ? 0 : kind == 1 ? g.derived : kind == 2 ? g.first : kind == 3 ? g.block : g.camera;
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.camera_value,&value,sizeof(value))); if (kind == 4) nmo_engine_destroy_object(g.f.engine,g.camera);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t error; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&error)); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,error.result);
        ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); ASSERT_EQ(0u,runs(&g.f,g.after)); fixture_end(&g.f);
    }
}

TEST(engine_camera_target, nonstrict_legacy_parameter_errors_still_advance_the_native_output_flow)
{
    target_fixture_t g; behavior_begin(&g,true,0x10000); nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = 0;
    fixture_commit(&g.f,&options); ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    ASSERT_EQ(1u,runs(&g.f,g.after)); ASSERT_EQ(g.first,eng_camera_target(g.f.engine,g.camera)); fixture_end(&g.f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_camera_target, runtime_targets_override_file_references_and_follow_moving_entities_without_document_changes);
REGISTER_TEST(engine_camera_target, attaching_detaching_and_self_assignments_keep_native_target_flags);
REGISTER_TEST(engine_camera_target, clearing_a_file_target_stays_cleared_and_repeating_the_same_target_does_not_repair_flags);
REGISTER_TEST(engine_camera_target, deleting_a_target_clears_all_attachments_and_deleting_a_camera_releases_its_target);
REGISTER_TEST(engine_camera_target, modern_default_clears_the_attachment_and_runs_once);
REGISTER_TEST(engine_camera_target, modern_versions_accept_exact_entities_and_clear_derived_missing_or_deleted_targets);
REGISTER_TEST(engine_camera_target, legacy_behavior_moves_the_existing_target_and_its_children_to_the_input_world_position);
REGISTER_TEST(engine_camera_target, legacy_behavior_attaches_derived_entities_when_empty_and_ignores_self_attachment);
REGISTER_TEST(engine_camera_target, legacy_missing_targets_fail_after_consuming_input_and_activating_output);
REGISTER_TEST(engine_camera_target, invalid_camera_owners_fail_before_advancing_io);
REGISTER_TEST(engine_camera_target, nonstrict_legacy_parameter_errors_still_advance_the_native_output_flow);
TEST_MAIN_END()
