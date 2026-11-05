#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "type/nmo_object_guids.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_LOCAL NMO_GUID(0x21f5f30d,0x08d5a1db)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct matrix_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, other_parent, target, child, grandchild, block, out, matrix;
} matrix_fixture_t;

static eng_matrix_t requested(void)
{
    return (eng_matrix_t){.m={{1,0,0,0},{0,0,2,0},{0,-3,0,0},{1,2,3,1}}};
}

static eng_matrix_t expected_world(void)
{
    return (eng_matrix_t){.m={{0,2,0,0},{0,0,8,0},{9,0,0,0},{4,22,42,1}}};
}

static void input(matrix_fixture_t *g, const eng_matrix_t *matrix)
{
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_bytes(g->f.tx,g->matrix,(const uint8_t *)matrix->m,sizeof(matrix->m)));
}

static void begin(matrix_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Parent"); g->other_parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Other Parent");
    g->target = fixture_create(&g->f,NMO_CID_3DENTITY,"Matrix Target"); g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child");
    g->grandchild = fixture_create(&g->f,NMO_CID_3DENTITY,"Grandchild");
    nmo_3dentity_state_t *parent = fixture_state(&g->f,g->parent), *other = fixture_state(&g->f,g->other_parent);
    nmo_3dentity_state_t *target = fixture_state(&g->f,g->target), *child = fixture_state(&g->f,g->child), *grand = fixture_state(&g->f,g->grandchild);
    ASSERT_NOT_NULL(parent); ASSERT_NOT_NULL(other); ASSERT_NOT_NULL(target); ASSERT_NOT_NULL(child); ASSERT_NOT_NULL(grand);
    const eng_matrix_t p = {.m={{0,2,0,0},{-3,0,0,0},{0,0,4,0},{10,20,30,1}}}; memcpy(parent->world_matrix,p.m,sizeof(p.m));
    other->world_matrix[12] = 100; other->world_matrix[13] = 200; other->world_matrix[14] = 300;
    target->parent = nmo_ref_from_id(g->parent); target->has_entityndata_chunk = 1;
    target->world_matrix[0] = 2; target->world_matrix[5] = 3; target->world_matrix[10] = 4;
    target->world_matrix[12] = 3; target->world_matrix[13] = 4; target->world_matrix[14] = 5;
    child->parent = nmo_ref_from_id(g->target); child->has_entityndata_chunk = 1;
    child->world_matrix[12] = 5; child->world_matrix[13] = 7; child->world_matrix[14] = 9;
    grand->parent = nmo_ref_from_id(g->child); grand->has_entityndata_chunk = 1;
    grand->world_matrix[12] = 6; grand->world_matrix[13] = 9; grand->world_matrix[14] = 12;
    g->f.script_id = fixture_add_script(&g->f,"Matrix Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_LOCAL,"Set Local Matrix"); g->out = fixture_block(&g->f,GUID_NOP,"After Matrix"); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->target);
    ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(g->f.tx,BLOCK_PIN(&g->f,g->block,0),&g->matrix));
    eng_matrix_t matrix = requested(); input(g,&matrix);
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void commit(matrix_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_LOCAL));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void same_matrix(const float *actual, const eng_matrix_t *expected)
{
    for (int i = 0; i < 16; i++) { ASSERT_TRUE(isfinite(actual[i])); ASSERT_FLOAT_EQ(expected->m[i/4][i%4],actual[i],1e-4f); }
}

static void world(matrix_fixture_t *g, nmo_object_id_t id, const eng_matrix_t *expected)
{
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g->f.engine,id,matrix)); same_matrix(matrix,expected);
}

static void position(matrix_fixture_t *g, nmo_object_id_t id, eng_vector_t expected)
{
    eng_vector_t actual = eng_entity_position(g->f.engine,eng_entity_index(g->f.engine,id),-1);
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z));
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,1e-4f);
}

static void trigger(matrix_fixture_t *g)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,0));
}

TEST(engine_local_matrix, composes_local_pose_with_scaled_parent_and_propagates_or_preserves_descendants)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        matrix_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),hierarchy);
        eng_matrix_t matrix = requested(); matrix.m[0][3] = .25f; matrix.m[1][3] = .5f; matrix.m[2][3] = .75f; matrix.m[3][3] = 2; input(&g,&matrix); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); eng_matrix_t expected = expected_world(); world(&g,g.target,&expected);
        same_matrix(g.f.engine->entities.data[eng_entity_index(g.f.engine,g.target)].local.m[0],&matrix);
        position(&g,g.child,hierarchy ? (eng_vector_t){13,24,50} : (eng_vector_t){5,7,9});
        position(&g,g.grandchild,hierarchy ? (eng_vector_t){19.75f,25,50+16.0f/3} : (eng_vector_t){6,9,12});
        ASSERT_EQ(g.target,nmo_engine_entity_parent(g.f.engine,g.child)); ASSERT_EQ(g.child,nmo_engine_entity_parent(g.f.engine,g.grandchild));
        ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(1u,runs(&g.f,g.block));
        eng_matrix_t bytes; read_value(&g.f,g.matrix,&bytes,sizeof(bytes)); same_matrix(bytes.m[0],&matrix);
        ASSERT_FLOAT_EQ(3,((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[12],0); fixture_end(&g.f);
    }
}

TEST(engine_local_matrix, root_entities_copy_all_sixteen_values_including_shear_reflection_and_singular_axes)
{
    const eng_matrix_t matrices[] = {
        {.m={{-2,1,0,.125f},{.5f,3,0,.25f},{0,0,.25f,.5f},{7,8,9,2}}},
        {.m={{0,0,0,0},{0,0,0,0},{0,0,0,0},{-4,5,6,1}}},
        {.m={{0,0,-2,0},{0,3,0,0},{4,0,0,0},{1,2,3,1}}}
    };
    for (size_t i = 0; i < COUNT(matrices); i++) {
        matrix_fixture_t g; begin(&g); nmo_3dentity_state_t *target = fixture_state(&g.f,g.target); target->parent = nmo_ref_from_id(0);
        input(&g,&matrices[i]); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); world(&g,g.target,&matrices[i]);
        same_matrix(g.f.engine->entities.data[eng_entity_index(g.f.engine,g.target)].local.m[0],&matrices[i]); fixture_end(&g.f);
    }
}

TEST(engine_local_matrix, reapplying_a_matrix_is_absolute_and_kept_children_still_follow_later_parent_motion)
{
    matrix_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),0); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); eng_matrix_t expected = expected_world(); trigger(&g); world(&g,g.target,&expected);
    position(&g,g.child,(eng_vector_t){5,7,9}); position(&g,g.grandchild,(eng_vector_t){6,9,12});
    eng_vector_t moved = {110,20,30}; eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.parent),&moved,-1,false);
    expected.m[3][0] += 100; world(&g,g.target,&expected); position(&g,g.child,(eng_vector_t){105,7,9}); position(&g,g.grandchild,(eng_vector_t){106,9,12});
    ASSERT_EQ(2u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST(engine_local_matrix, reads_live_matrix_values_and_the_current_parent_even_when_target_parent_updates_are_disabled)
{
    for (int disabled = 0; disabled < 2; disabled++) {
        matrix_fixture_t g; begin(&g); if (disabled) ((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->moveable_flags |= VX_MOVEABLE_DONTUPDATEFROMPARENT;
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,(eng_vector_t){4,22,42});
        eng_matrix_t parent = g.f.engine->entities.data[eng_entity_index(g.f.engine,g.parent)].world;
        parent.m[3][0] = 20; eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.parent),&parent,true); trigger(&g); position(&g,g.target,(eng_vector_t){14,22,42});
        eng_matrix_t changed = {.m={{-2,0,0,0},{0,3,0,0},{0,0,.5f,0},{-2,4,1,1}}};
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.matrix,changed.m,sizeof(changed.m))); trigger(&g);
        eng_matrix_t expected = {.m={{0,-4,0,0},{-9,0,0,0},{0,0,2,0},{8,16,34,1}}}; world(&g,g.target,&expected);
        ASSERT_TRUE(eng_entity_set_parent(g.f.engine,eng_entity_index(g.f.engine,g.target),eng_entity_index(g.f.engine,g.other_parent),true));
        trigger(&g); expected = changed; expected.m[3][0] = 98; expected.m[3][1] = 204; expected.m[3][2] = 301; world(&g,g.target,&expected); fixture_end(&g.f);
    }
}

TEST(engine_local_matrix, children_that_ignore_parent_updates_keep_world_and_local_matrices_until_reenabled)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        matrix_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),hierarchy);
        ((nmo_3dentity_state_t *)fixture_state(&g.f,g.child))->moveable_flags |= VX_MOVEABLE_DONTUPDATEFROMPARENT; commit(&g);
        int32_t child = eng_entity_index(g.f.engine,g.child); eng_matrix_t local = g.f.engine->entities.data[child].local;
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.child,(eng_vector_t){5,7,9}); position(&g,g.grandchild,(eng_vector_t){6,9,12});
        same_matrix(g.f.engine->entities.data[child].local.m[0],&local);
        g.f.engine->entities.data[child].moveable_flags &= ~VX_MOVEABLE_DONTUPDATEFROMPARENT;
        nmo_object_id_t source = nmo_parameterin_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,1))); int32_t yes = 1;
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,source,&yes,sizeof(yes))); trigger(&g);
        position(&g,g.child,(eng_vector_t){13,24,50}); position(&g,g.grandchild,(eng_vector_t){19.75f,25,50+16.0f/3}); fixture_end(&g.f);
    }
}

TEST(engine_local_matrix, disconnected_matrices_use_native_zero_initialization_and_hierarchy_defaults)
{
    for (int root = 0; root < 2; root++) {
        matrix_fixture_t g; begin(&g); if (root) ((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->parent = nmo_ref_from_id(0);
        nmo_parameterin_set_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,0)),0); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); eng_matrix_t expected = {0};
        if (!root) { expected.m[3][0] = 10; expected.m[3][1] = 20; expected.m[3][2] = 30; expected.m[3][3] = 1; }
        world(&g,g.target,&expected); position(&g,g.child,root ? (eng_vector_t){0,0,0} : (eng_vector_t){10,20,30}); fixture_end(&g.f);
    }
}

TEST(engine_local_matrix, missing_deleted_and_nonentity_targets_report_owner_errors_before_consuming_input)
{
    for (int kind = 0; kind < 3; kind++) {
        matrix_fixture_t g; begin(&g); if (kind != 1) fixture_input_int(&g.f,fixture_target(&g.f,g.block),kind == 0 ? 0 : (int32_t)g.block);
        commit(&g); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.target);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); fixture_end(&g.f);
    }
}

static nmo_object_id_t saved_input(matrix_fixture_t *g, nmo_object_id_t pin, const char *text)
{
    nmo_object_id_t parameter = 0;
    if (nmo_script_edit_ensure_input_parameter_source(g->f.tx,pin,&parameter) != NMO_OK ||
        nmo_script_edit_set_parameter_value(g->f.tx,parameter,text) != NMO_OK) return 0;
    return parameter;
}

static const char *resolve_file(void *data, const char *name)
{
    (void)name; return data;
}

TEST(engine_local_matrix, loaded_and_copied_graphs_keep_full_matrix_parameters_and_parent_bindings_independent)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        matrix_fixture_t g; begin(&g); nmo_object_id_t parameters[3]; char text[32];
        snprintf(text,sizeof(text),"%u",g.target); parameters[0] = saved_input(&g,fixture_target(&g.f,g.block),text); parameters[1] = g.matrix;
        parameters[2] = saved_input(&g,BLOCK_PIN(&g.f,g.block,1),hierarchy ? "TRUE" : "FALSE");
        for (size_t i = 0; i < COUNT(parameters); i++) ASSERT_TRUE(parameters[i] != 0);
        eng_matrix_t matrix = requested(); matrix.m[0][3] = .25f; matrix.m[3][3] = 2; input(&g,&matrix); fixture_commit_bare(&g.f,NULL);
        nmo_beobject_state_t *owner = fixture_state(&g.f,g.f.owner_id);
        owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
        script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        const char *path = NMO_TEST_SCRATCH_FILE("engine_local_matrix.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(g.f.session,path,NULL,NULL));
        for (int started = 0; started < 2; started++) {
            engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host);
            nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&host,&options);
            if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
            nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
            int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"matrix.cmo",true,&file));
            if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
            nmo_object_id_t block = nmo_engine_object_named(host.engine,"Set Local Matrix",NMO_CID_BEHAVIOR); ASSERT_TRUE(block != 0);
            const eng_module_t *module = eng_module_of(host.engine,block,NULL);
            const nmo_object_id_t original[] = {g.f.owner_id,g.parent,g.other_parent,g.target,g.child,g.grandchild,g.f.script_id,g.block,g.out};
            nmo_object_id_t ids[12] = {0}, copies[12] = {0};
            for (size_t i = 0; i < COUNT(original); i++) { ids[i] = eng_module_id(module,original[i]); ASSERT_TRUE(ids[i] != 0); }
            for (size_t i = 0; i < COUNT(parameters); i++) ids[9+i] = eng_module_id(module,parameters[i]);
            ASSERT_TRUE(ids[3] != g.target); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
            matrix_fixture_t view = {.f.engine=host.engine}; eng_matrix_t expected = expected_world(); world(&view,ids[3],&expected);
            same_matrix(host.engine->entities.data[eng_entity_index(host.engine,ids[3])].local.m[0],&matrix);
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,ids,COUNT(ids),false,copies));
            ASSERT_EQ(copies[1],nmo_engine_entity_parent(host.engine,copies[3])); ASSERT_EQ(copies[3],nmo_engine_entity_parent(host.engine,copies[4]));
            ASSERT_EQ(copies[4],nmo_engine_entity_parent(host.engine,copies[5])); eng_matrix_t bytes;
            read_value(&host,copies[10],&bytes,sizeof(bytes)); same_matrix(bytes.m[0],&matrix);
            eng_vector_t moved = {110,20,30}; eng_entity_set_position(host.engine,eng_entity_index(host.engine,copies[1]),&moved,-1,true);
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[6],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
            world(&view,ids[3],&expected); expected.m[3][0] += 100; world(&view,copies[3],&expected);
            position(&view,copies[4],hierarchy ? (eng_vector_t){113,24,50} : (eng_vector_t){5,7,9});
            position(&view,copies[5],hierarchy ? (eng_vector_t){119.75f,25,50+16.0f/3} : (eng_vector_t){6,9,12});
            same_matrix(host.engine->entities.data[eng_entity_index(host.engine,copies[3])].local.m[0],&matrix);
            nmo_engine_destroy_object(host.engine,ids[3]); nmo_engine_destroy_object(host.engine,ids[1]); nmo_engine_destroy_object(host.engine,ids[2]);
            ASSERT_TRUE(eng_entity_set_parent(host.engine,eng_entity_index(host.engine,copies[3]),eng_entity_index(host.engine,copies[2]),true));
            eng_matrix_t changed = {.m={{-2,0,0,0},{0,3,0,0},{0,0,.5f,0},{-2,4,1,1}}};
            ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[10],changed.m,sizeof(changed.m)));
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[6],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,100));
            expected = changed; expected.m[3][0] = 98; expected.m[3][1] = 204; expected.m[3][2] = 301; world(&view,copies[3],&expected);
            position(&view,copies[4],hierarchy ? (eng_vector_t){96,207,301.5f} : (eng_vector_t){5,7,9});
            position(&view,copies[5],hierarchy ? (eng_vector_t){95,209,301.875f} : (eng_vector_t){6,9,12});
            ASSERT_EQ(1u,runs(&host,ids[7])); ASSERT_EQ(2u,runs(&host,copies[7])); fixture_end(&host);
        }
        ASSERT_FLOAT_EQ(3,((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[12],0); fixture_end(&g.f); remove(path);
    }
}

static void count_operations(const nmo_engine_event_t *event, void *data)
{
    if (event->kind == NMO_ENGINE_EVENT_OPERATION) (*(size_t *)data)++;
}

TEST(engine_local_matrix, local_matrix_operations_read_the_preceding_block_pose_and_are_skipped_for_invalid_targets)
{
    for (int valid = 0; valid < 2; valid++) {
        matrix_fixture_t g; begin(&g); if (!valid) fixture_input_int(&g.f,fixture_target(&g.f,g.block),0);
        nmo_object_id_t source_entity = fixture_create(&g.f,NMO_CID_3DENTITY,"Source Entity"); nmo_3dentity_state_t *source = fixture_state(&g.f,source_entity); ASSERT_NOT_NULL(source);
        source->parent = nmo_ref_from_id(g.other_parent); source->has_entityndata_chunk = 1;
        const nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
        nmo_object_id_t first_link = nmo_behavior_ref_array_get_id(&script->sub_behavior_links,0);
        ASSERT_EQ(NMO_OK,nmo_script_edit_remove_behavior_link(g.f.tx,g.f.script_id,first_link));
        nmo_object_id_t producer = fixture_block(&g.f,GUID_LOCAL,"Set Source Local Matrix"); ASSERT_TRUE(producer != 0);
        fixture_input_int(&g.f,fixture_target(&g.f,producer),(int32_t)source_entity);
        nmo_object_id_t producer_matrix = 0; ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(g.f.tx,BLOCK_PIN(&g.f,producer,0),&producer_matrix));
        eng_matrix_t first = {.m={{2,0,0,0},{0,3,0,0},{0,0,4,0},{4,5,6,1}}};
        ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_bytes(g.f.tx,producer_matrix,(const uint8_t *)first.m,sizeof(first.m)));
        nmo_object_id_t entity_parameter = 0, operation = 0;
        ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g.f.tx,g.f.script_id,NMO_SCRIPT_EDIT_PARAM_LOCAL,CKPGUID_3DENTITY,"Source Entity ID",&entity_parameter));
        char text[32]; snprintf(text,sizeof(text),"%u",source_entity); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,entity_parameter,text));
        ASSERT_EQ(NMO_OK,nmo_script_edit_add_operation(g.f.tx,g.f.script_id,NMO_GUID(0x14a024c7,0x050d4228),entity_parameter,0,0,&operation));
        const nmo_parameteroperation_state_t *op = fixture_state(&g.f,operation); ASSERT_NOT_NULL(op);
        ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,nmo_parameteroperation_out_id(op),BLOCK_PIN(&g.f,g.block,0)));
        fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,producer,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,producer,0),BLOCK_IN(&g.f,g.block,0),0);
        commit(&g); size_t operations = 0; nmo_engine_set_trace(g.f.engine,count_operations,&operations);
        ASSERT_EQ(valid ? NMO_OK : NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
        ASSERT_EQ(valid ? 1u : 0u,operations); position(&g,source_entity,(eng_vector_t){104,205,306});
        if (!valid) { position(&g,g.target,(eng_vector_t){3,4,5}); fixture_end(&g.f); continue; }
        eng_matrix_t expected = {.m={{0,4,0,0},{-9,0,0,0},{0,0,16,0},{-5,28,54,1}}}; world(&g,g.target,&expected);
        eng_vector_t moved = {200,300,400}; eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.other_parent),&moved,-1,false);
        trigger(&g); world(&g,g.target,&expected); position(&g,source_entity,(eng_vector_t){204,305,406});
        eng_matrix_t changed = requested(); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,producer_matrix,changed.m,sizeof(changed.m))); trigger(&g);
        expected = expected_world(); world(&g,g.target,&expected); position(&g,source_entity,(eng_vector_t){201,302,403});
        position(&g,g.child,(eng_vector_t){13,24,50}); position(&g,g.grandchild,(eng_vector_t){19.75f,25,50+16.0f/3});
        ASSERT_EQ(3u,operations); ASSERT_EQ(3u,runs(&g.f,producer)); ASSERT_EQ(3u,runs(&g.f,g.block)); ASSERT_EQ(3u,runs(&g.f,g.out)); fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_local_matrix, composes_local_pose_with_scaled_parent_and_propagates_or_preserves_descendants);
REGISTER_TEST(engine_local_matrix, root_entities_copy_all_sixteen_values_including_shear_reflection_and_singular_axes);
REGISTER_TEST(engine_local_matrix, reapplying_a_matrix_is_absolute_and_kept_children_still_follow_later_parent_motion);
REGISTER_TEST(engine_local_matrix, reads_live_matrix_values_and_the_current_parent_even_when_target_parent_updates_are_disabled);
REGISTER_TEST(engine_local_matrix, children_that_ignore_parent_updates_keep_world_and_local_matrices_until_reenabled);
REGISTER_TEST(engine_local_matrix, disconnected_matrices_use_native_zero_initialization_and_hierarchy_defaults);
REGISTER_TEST(engine_local_matrix, missing_deleted_and_nonentity_targets_report_owner_errors_before_consuming_input);
REGISTER_TEST(engine_local_matrix, loaded_and_copied_graphs_keep_full_matrix_parameters_and_parent_bindings_independent);
REGISTER_TEST(engine_local_matrix, local_matrix_operations_read_the_preceding_block_pose_and_are_skipped_for_invalid_targets);
TEST_MAIN_END()
