#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_TRANSLATE NMO_GUID(0x000d000d,0x000d000d)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct translate_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, target, child, grandchild, reference, block, out, target_parameter, vector_parameter, reference_parameter;
} translate_fixture_t;

static void input(translate_fixture_t *g, eng_vector_t value)
{
    fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,0),&value,sizeof(value));
}

static void begin(translate_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Parent"); g->target = fixture_create(&g->f,NMO_CID_3DENTITY,"Moving Entity");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child"); g->grandchild = fixture_create(&g->f,NMO_CID_3DENTITY,"Grandchild");
    g->reference = fixture_create(&g->f,NMO_CID_3DENTITY,"Reference");
    nmo_3dentity_state_t *parent = fixture_state(&g->f,g->parent), *reference = fixture_state(&g->f,g->reference);
    nmo_3dentity_state_t *target = fixture_state(&g->f,g->target), *child = fixture_state(&g->f,g->child), *grand = fixture_state(&g->f,g->grandchild);
    ASSERT_NOT_NULL(parent); ASSERT_NOT_NULL(reference); ASSERT_NOT_NULL(target); ASSERT_NOT_NULL(child); ASSERT_NOT_NULL(grand);
    const eng_matrix_t p = {.m={{0,2,0,0},{-3,0,0,0},{0,0,4,0},{10,20,30,1}}}, r = {.m={{2,1,0,0},{.5f,3,0,0},{0,0,4,0},{100,200,300,1}}};
    memcpy(parent->world_matrix,p.m,sizeof(p.m)); memcpy(reference->world_matrix,r.m,sizeof(r.m));
    target->parent = nmo_ref_from_id(g->parent); target->has_entityndata_chunk = 1;
    target->world_matrix[0] = 2; target->world_matrix[5] = 3; target->world_matrix[10] = 4;
    target->world_matrix[12] = 3; target->world_matrix[13] = 4; target->world_matrix[14] = 5;
    child->parent = nmo_ref_from_id(g->target); child->has_entityndata_chunk = 1; child->world_matrix[12] = 5; child->world_matrix[13] = 7; child->world_matrix[14] = 9;
    grand->parent = nmo_ref_from_id(g->child); grand->has_entityndata_chunk = 1; grand->world_matrix[12] = 6; grand->world_matrix[13] = 9; grand->world_matrix[14] = 12;
    g->f.script_id = fixture_add_script(&g->f,"Translation Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_TRANSLATE,"Translate"); g->out = fixture_block(&g->f,GUID_NOP,"After Translation"); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->target); input(g,(eng_vector_t){1,2,3}); fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,1),0);
    g->target_parameter = nmo_parameterin_source_id(fixture_state(&g->f,fixture_target(&g->f,g->block)));
    g->vector_parameter = nmo_parameterin_source_id(fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,0)));
    g->reference_parameter = nmo_parameterin_source_id(fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,1)));
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void commit(translate_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_TRANSLATE));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void vector(eng_vector_t actual, eng_vector_t expected)
{
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z));
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,1e-4f);
}

static void position(translate_fixture_t *g, nmo_object_id_t id, eng_vector_t expected)
{
    vector(eng_entity_position(g->f.engine,eng_entity_index(g->f.engine,id),-1),expected);
}

static void basis(translate_fixture_t *g)
{
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g->f.engine,g->target,matrix));
    vector((eng_vector_t){matrix[0],matrix[1],matrix[2]},(eng_vector_t){2,0,0});
    vector((eng_vector_t){matrix[4],matrix[5],matrix[6]},(eng_vector_t){0,3,0}); vector((eng_vector_t){matrix[8],matrix[9],matrix[10]},(eng_vector_t){0,0,4});
}

static void trigger(translate_fixture_t *g, float delta)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,delta));
}

TEST(engine_translate, adds_world_displacements_without_changing_orientation_scale_or_scheduling_another_frame)
{
    const eng_vector_t values[] = {{1,2,3},{-2,0,4},{0,0,0}};
    for (size_t i = 0; i < COUNT(values); i++) {
        translate_fixture_t g; begin(&g); input(&g,values[i]); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        position(&g,g.target,(eng_vector_t){3+values[i].x,4+values[i].y,5+values[i].z}); basis(&g);
        ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(1u,runs(&g.f,g.block));
        ASSERT_FLOAT_EQ(3,((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[12],0); fixture_end(&g.f);
    }
}

TEST(engine_translate, hierarchy_preserves_or_moves_descendants_and_kept_children_still_follow_future_parent_motion)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        translate_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),hierarchy); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,(eng_vector_t){4,6,8});
        position(&g,g.child,hierarchy ? (eng_vector_t){6,9,12} : (eng_vector_t){5,7,9}); position(&g,g.grandchild,hierarchy ? (eng_vector_t){7,11,15} : (eng_vector_t){6,9,12});
        vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,g.target),eng_entity_index(g.f.engine,g.parent)),(eng_vector_t){-7,2,-5.5f});
        eng_vector_t moved = {110,20,30}; eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.parent),&moved,-1,false);
        position(&g,g.target,(eng_vector_t){104,6,8}); position(&g,g.child,hierarchy ? (eng_vector_t){106,9,12} : (eng_vector_t){105,7,9});
        position(&g,g.grandchild,hierarchy ? (eng_vector_t){107,11,15} : (eng_vector_t){106,9,12}); fixture_end(&g.f);
    }
}

TEST(engine_translate, reference_rotation_scale_and_shear_transform_displacement_without_adding_reference_position)
{
    const eng_matrix_t references[] = {
        {.m={{0,2,0,0},{-3,0,0,0},{0,0,4,0},{100,200,300,1}}},
        {.m={{2,1,0,0},{.5f,3,0,0},{0,0,4,0},{100,200,300,1}}},
        {.m={{-2,0,0,0},{0,3,0,0},{0,0,-4,0},{100,200,300,1}}}
    };
    const eng_vector_t moves[] = {{-6,2,12},{3,7,12},{-2,6,-12}};
    for (size_t i = 0; i < COUNT(references); i++) {
        translate_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),(int32_t)g.reference);
        memcpy(((nmo_3dentity_state_t *)fixture_state(&g.f,g.reference))->world_matrix,references[i].m,sizeof(references[i].m)); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,(eng_vector_t){3+moves[i].x,4+moves[i].y,5+moves[i].z});
        eng_vector_t far = {1e6f,-2e6f,3e6f}; eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.reference),&far,-1,false); trigger(&g,0);
        position(&g,g.target,(eng_vector_t){3+2*moves[i].x,4+2*moves[i].y,5+2*moves[i].z}); basis(&g); fixture_end(&g.f);
    }
}

TEST(engine_translate, self_parent_and_descendant_references_use_their_current_axes)
{
    const eng_vector_t expected[] = {{5,10,17},{-3,6,17},{4,6,8}};
    for (int kind = 0; kind < 3; kind++) {
        translate_fixture_t g; begin(&g); nmo_object_id_t reference = kind == 0 ? g.target : kind == 1 ? g.parent : g.child;
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),(int32_t)reference); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,expected[kind]); basis(&g); fixture_end(&g.f);
    }
}

TEST(engine_translate, reads_live_vectors_references_and_targets_without_scaling_by_delta_time)
{
    translate_fixture_t g; begin(&g); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,(eng_vector_t){4,6,8});
    eng_vector_t changed = {-2,0,4}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.vector_parameter,&changed,sizeof(changed))); trigger(&g,1000); position(&g,g.target,(eng_vector_t){2,6,12});
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.reference_parameter,&g.reference,sizeof(g.reference))); trigger(&g,1); position(&g,g.target,(eng_vector_t){-2,4,28});
    nmo_object_id_t zero = 0; changed = (eng_vector_t){1,0,0};
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.reference_parameter,&zero,sizeof(zero))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.vector_parameter,&changed,sizeof(changed)));
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_parameter,&g.child,sizeof(g.child))); trigger(&g,0);
    position(&g,g.target,(eng_vector_t){-2,4,28}); position(&g,g.child,(eng_vector_t){1,7,32}); position(&g,g.grandchild,(eng_vector_t){2,9,35});
    ASSERT_EQ(4u,runs(&g.f,g.out)); fixture_end(&g.f);
}

TEST(engine_translate, absent_deleted_and_nonentity_references_use_world_displacement)
{
    for (int kind = 0; kind < 3; kind++) {
        translate_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),kind == 1 ? (int32_t)g.reference : kind == 2 ? (int32_t)g.block : 0);
        commit(&g); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.reference);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,(eng_vector_t){4,6,8}); fixture_end(&g.f);
    }
}

TEST(engine_translate, disconnected_inputs_keep_native_zero_vector_world_reference_and_hierarchy_defaults)
{
    for (int missing_vector = 0; missing_vector < 2; missing_vector++) {
        translate_fixture_t g; begin(&g); size_t kept = 0;
        for (size_t i = 0; i < g.f.value_count; i++) if (g.f.values[i].param == g.target_parameter || (!missing_vector && g.f.values[i].param == g.vector_parameter)) g.f.values[kept++] = g.f.values[i];
        g.f.value_count = kept;
        for (int i = missing_vector ? 0 : 1; i < 3; i++) nmo_parameterin_set_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,i)),0);
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,missing_vector ? (eng_vector_t){3,4,5} : (eng_vector_t){4,6,8});
        position(&g,g.child,missing_vector ? (eng_vector_t){5,7,9} : (eng_vector_t){6,9,12}); fixture_end(&g.f);
    }
}

TEST(engine_translate, explicit_translation_ignores_target_parent_update_flags_but_respects_them_on_children)
{
    for (int target_disabled = 0; target_disabled < 2; target_disabled++) for (int child_disabled = 0; child_disabled < 2; child_disabled++) {
        translate_fixture_t g; begin(&g);
        if (target_disabled) ((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->moveable_flags |= VX_MOVEABLE_DONTUPDATEFROMPARENT;
        if (child_disabled) ((nmo_3dentity_state_t *)fixture_state(&g.f,g.child))->moveable_flags |= VX_MOVEABLE_DONTUPDATEFROMPARENT;
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,(eng_vector_t){4,6,8});
        position(&g,g.child,child_disabled ? (eng_vector_t){5,7,9} : (eng_vector_t){6,9,12}); position(&g,g.grandchild,child_disabled ? (eng_vector_t){6,9,12} : (eng_vector_t){7,11,15});
        ASSERT_EQ(target_disabled != 0,(g.f.engine->entities.data[eng_entity_index(g.f.engine,g.target)].moveable_flags & VX_MOVEABLE_DONTUPDATEFROMPARENT) != 0); fixture_end(&g.f);
    }
}

TEST(engine_translate, tiny_vectors_are_applied_without_an_angle_or_length_threshold)
{
    translate_fixture_t g; begin(&g); input(&g,(eng_vector_t){1e-8f,-2e-8f,3e-8f}); commit(&g);
    eng_vector_t zero = {0,0,0}; eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.target),&zero,-1,false);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); eng_vector_t actual = eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,g.target),-1);
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z));
    ASSERT_FLOAT_EQ(1e-8f,actual.x,1e-14f); ASSERT_FLOAT_EQ(-2e-8f,actual.y,1e-14f); ASSERT_FLOAT_EQ(3e-8f,actual.z,1e-14f); fixture_end(&g.f);
}

TEST(engine_translate, missing_deleted_and_nonentity_targets_report_owner_errors_before_consuming_input)
{
    for (int kind = 0; kind < 3; kind++) {
        translate_fixture_t g; begin(&g); if (kind != 1) fixture_input_int(&g.f,fixture_target(&g.f,g.block),kind == 0 ? 0 : (int32_t)g.block);
        commit(&g); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.target);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); fixture_end(&g.f);
    }
}

static nmo_object_id_t saved_input(translate_fixture_t *g, nmo_object_id_t pin, const char *text)
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

TEST(engine_translate, loaded_and_copied_graphs_keep_vectors_references_and_hierarchy_independent)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        translate_fixture_t g; begin(&g); nmo_object_id_t parameters[4]; char text[32];
        snprintf(text,sizeof(text),"%u",g.target); parameters[0] = saved_input(&g,fixture_target(&g.f,g.block),text);
        parameters[1] = saved_input(&g,BLOCK_PIN(&g.f,g.block,0),"1,2,3");
        snprintf(text,sizeof(text),"%u",g.reference); parameters[2] = saved_input(&g,BLOCK_PIN(&g.f,g.block,1),text);
        parameters[3] = saved_input(&g,BLOCK_PIN(&g.f,g.block,2),hierarchy ? "TRUE" : "FALSE");
        for (size_t i = 0; i < COUNT(parameters); i++) ASSERT_TRUE(parameters[i] != 0);
        fixture_commit_bare(&g.f,NULL); nmo_beobject_state_t *owner = fixture_state(&g.f,g.f.owner_id);
        owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
        script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        const char *path = NMO_TEST_SCRATCH_FILE("engine_translate.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(g.f.session,path,NULL,NULL));
        for (int started = 0; started < 2; started++) {
            engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host);
            nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&host,&options);
            if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
            nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
            int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"translate.cmo",true,&file));
            if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
            nmo_object_id_t block = nmo_engine_object_named(host.engine,"Translate",NMO_CID_BEHAVIOR); ASSERT_TRUE(block != 0);
            const eng_module_t *module = eng_module_of(host.engine,block,NULL);
            const nmo_object_id_t original[] = {g.f.owner_id,g.parent,g.target,g.child,g.grandchild,g.reference,g.f.script_id,g.block,g.out};
            nmo_object_id_t ids[13] = {0}, copies[13] = {0};
            for (size_t i = 0; i < COUNT(original); i++) { ids[i] = eng_module_id(module,original[i]); ASSERT_TRUE(ids[i] != 0); }
            for (size_t i = 0; i < COUNT(parameters); i++) ids[9+i] = eng_module_id(module,parameters[i]);
            ASSERT_TRUE(ids[2] != g.target); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
            translate_fixture_t view = {.f.engine=host.engine}; position(&view,ids[2],(eng_vector_t){6,11,17});
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,ids,COUNT(ids),false,copies));
            ASSERT_EQ(copies[1],nmo_engine_entity_parent(host.engine,copies[2])); ASSERT_EQ(copies[2],nmo_engine_entity_parent(host.engine,copies[3])); ASSERT_EQ(copies[3],nmo_engine_entity_parent(host.engine,copies[4]));
            eng_matrix_t reference = {.m={{0,2,0,0},{-3,0,0,0},{0,0,4,0},{-100,50,300,1}}};
            eng_entity_set_world_matrix(host.engine,eng_entity_index(host.engine,copies[5]),&reference,false);
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,ids[6],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[6],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,100));
            position(&view,ids[2],(eng_vector_t){9,18,29}); position(&view,copies[2],(eng_vector_t){0,13,29});
            position(&view,copies[3],hierarchy ? (eng_vector_t){2,16,33} : (eng_vector_t){5,7,9}); position(&view,copies[4],hierarchy ? (eng_vector_t){3,18,36} : (eng_vector_t){6,9,12});
            nmo_engine_destroy_object(host.engine,ids[2]); nmo_engine_destroy_object(host.engine,ids[5]); nmo_engine_destroy_object(host.engine,ids[1]);
            eng_vector_t changed = {0,-2,0}; int32_t yes = 1;
            ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[10],&changed,sizeof(changed))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[12],&yes,sizeof(yes)));
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[6],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); position(&view,copies[2],(eng_vector_t){6,13,29});
            position(&view,copies[3],hierarchy ? (eng_vector_t){8,16,33} : (eng_vector_t){11,7,9}); position(&view,copies[4],hierarchy ? (eng_vector_t){9,18,36} : (eng_vector_t){12,9,12});
            view.target = copies[2]; basis(&view); eng_vector_t original_value; read_value(&host,ids[10],&original_value,sizeof(original_value)); vector(original_value,(eng_vector_t){1,2,3});
            ASSERT_EQ(2u,runs(&host,ids[7])); ASSERT_EQ(2u,runs(&host,copies[7])); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,10)); ASSERT_EQ(2u,runs(&host,copies[7])); fixture_end(&host);
        }
        ASSERT_FLOAT_EQ(3,((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[12],0); fixture_end(&g.f); remove(path);
    }
}

TEST(engine_translate, rotate_around_updates_reference_axes_before_translation_in_the_same_frame)
{
    translate_fixture_t g; begin(&g); input(&g,(eng_vector_t){0,0,1}); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),(int32_t)g.reference);
    eng_matrix_t reference = {.m={{2,0,0,0},{0,3,0,0},{0,0,4,0},{100,200,300,1}}};
    memcpy(((nmo_3dentity_state_t *)fixture_state(&g.f,g.reference))->world_matrix,reference.m,sizeof(reference.m));
    const nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
    nmo_object_id_t first_link = nmo_behavior_ref_array_get_id(&script->sub_behavior_links,0);
    ASSERT_EQ(NMO_OK,nmo_script_edit_remove_behavior_link(g.f.tx,g.f.script_id,first_link));
    nmo_object_id_t rotate = fixture_block(&g.f,NMO_GUID(0xfdfd999e,0xdefeded8),"Rotate Reference"); ASSERT_TRUE(rotate != 0);
    fixture_input_int(&g.f,fixture_target(&g.f,rotate),(int32_t)g.reference); fixture_input_int(&g.f,BLOCK_PIN(&g.f,rotate,3),0);
    float quarter = 1.57079632679489661923f; fixture_input_value(&g.f,BLOCK_PIN(&g.f,rotate,1),&quarter,sizeof(quarter));
    fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,rotate,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,rotate,0),BLOCK_IN(&g.f,g.block,0),0); commit(&g);
    const eng_vector_t positions[] = {{-1,4,5},{-1,4,1},{3,4,1},{3,4,5}}; const float deltas[] = {0,10,1000,1};
    for (size_t i = 0; i < COUNT(positions); i++) {
        if (i == 0) ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,deltas[i])); else trigger(&g,deltas[i]);
        position(&g,g.target,positions[i]); position(&g,g.child,(eng_vector_t){positions[i].x+2,positions[i].y+3,positions[i].z+4});
        position(&g,g.grandchild,(eng_vector_t){positions[i].x+3,positions[i].y+5,positions[i].z+7}); basis(&g);
    }
    ASSERT_EQ(4u,runs(&g.f,rotate)); ASSERT_EQ(4u,runs(&g.f,g.block)); ASSERT_EQ(4u,runs(&g.f,g.out));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(4u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_translate, adds_world_displacements_without_changing_orientation_scale_or_scheduling_another_frame);
REGISTER_TEST(engine_translate, hierarchy_preserves_or_moves_descendants_and_kept_children_still_follow_future_parent_motion);
REGISTER_TEST(engine_translate, reference_rotation_scale_and_shear_transform_displacement_without_adding_reference_position);
REGISTER_TEST(engine_translate, self_parent_and_descendant_references_use_their_current_axes);
REGISTER_TEST(engine_translate, reads_live_vectors_references_and_targets_without_scaling_by_delta_time);
REGISTER_TEST(engine_translate, absent_deleted_and_nonentity_references_use_world_displacement);
REGISTER_TEST(engine_translate, disconnected_inputs_keep_native_zero_vector_world_reference_and_hierarchy_defaults);
REGISTER_TEST(engine_translate, explicit_translation_ignores_target_parent_update_flags_but_respects_them_on_children);
REGISTER_TEST(engine_translate, tiny_vectors_are_applied_without_an_angle_or_length_threshold);
REGISTER_TEST(engine_translate, missing_deleted_and_nonentity_targets_report_owner_errors_before_consuming_input);
REGISTER_TEST(engine_translate, loaded_and_copied_graphs_keep_vectors_references_and_hierarchy_independent);
REGISTER_TEST(engine_translate, rotate_around_updates_reference_axes_before_translation_in_the_same_frame);
TEST_MAIN_END()
