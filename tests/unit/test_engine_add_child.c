#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "type/nmo_object_guids.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_ADD_CHILD NMO_GUID(0x45986587,0x12654556)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct child_fixture {
    engine_fixture_t f;
    nmo_object_id_t old_parent, parent, child, grandchild, sibling, old_sibling, block, out, target_parameter, child_parameter;
} child_fixture_t;

static void begin(child_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->old_parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Old Parent"); g->parent = fixture_create(&g->f,NMO_CID_3DENTITY,"New Parent");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child"); g->grandchild = fixture_create(&g->f,NMO_CID_3DENTITY,"Grandchild");
    g->sibling = fixture_create(&g->f,NMO_CID_3DENTITY,"New Sibling"); g->old_sibling = fixture_create(&g->f,NMO_CID_3DENTITY,"Old Sibling");
    nmo_3dentity_state_t *old = fixture_state(&g->f,g->old_parent), *parent = fixture_state(&g->f,g->parent);
    nmo_3dentity_state_t *child = fixture_state(&g->f,g->child), *grand = fixture_state(&g->f,g->grandchild);
    nmo_3dentity_state_t *sibling = fixture_state(&g->f,g->sibling), *old_sibling = fixture_state(&g->f,g->old_sibling);
    ASSERT_NOT_NULL(old); ASSERT_NOT_NULL(parent); ASSERT_NOT_NULL(child); ASSERT_NOT_NULL(grand); ASSERT_NOT_NULL(sibling); ASSERT_NOT_NULL(old_sibling);
    old->world_matrix[12] = -10; old->world_matrix[13] = -20; old->world_matrix[14] = -30;
    const eng_matrix_t p = {.m={{0,2,0,0},{-3,0,0,0},{0,0,4,0},{10,20,30,1}}}; memcpy(parent->world_matrix,p.m,sizeof(p.m));
    child->parent = nmo_ref_from_id(g->old_parent); child->has_entityndata_chunk = 1;
    child->world_matrix[0] = 2; child->world_matrix[5] = 3; child->world_matrix[10] = 4;
    child->world_matrix[12] = 4; child->world_matrix[13] = 22; child->world_matrix[14] = 42;
    grand->parent = nmo_ref_from_id(g->child); grand->has_entityndata_chunk = 1; grand->world_matrix[12] = 6; grand->world_matrix[13] = 25; grand->world_matrix[14] = 46;
    sibling->parent = nmo_ref_from_id(g->parent); sibling->has_entityndata_chunk = 1; sibling->world_matrix[12] = 0; sibling->world_matrix[13] = 1; sibling->world_matrix[14] = 2;
    old_sibling->parent = nmo_ref_from_id(g->old_parent); old_sibling->has_entityndata_chunk = 1; old_sibling->world_matrix[12] = -5; old_sibling->world_matrix[13] = 6; old_sibling->world_matrix[14] = 7;
    g->f.script_id = fixture_add_script(&g->f,"Adoption Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_ADD_CHILD,"Add Child"); g->out = fixture_block(&g->f,GUID_NOP,"After Adoption"); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->parent); fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,0),(int32_t)g->child);
    g->target_parameter = nmo_parameterin_source_id(fixture_state(&g->f,fixture_target(&g->f,g->block)));
    g->child_parameter = nmo_parameterin_source_id(fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,0)));
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void commit(child_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_ADD_CHILD));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void same_matrix(const eng_matrix_t *actual, const eng_matrix_t *expected)
{
    for (int row = 0; row < 4; row++) for (int col = 0; col < 4; col++) {
        ASSERT_TRUE(isfinite(actual->m[row][col])); ASSERT_FLOAT_EQ(expected->m[row][col],actual->m[row][col],1e-4f);
    }
}

static eng_matrix_t world(child_fixture_t *g, nmo_object_id_t id)
{
    return g->f.engine->entities.data[eng_entity_index(g->f.engine,id)].world;
}

static void position(child_fixture_t *g, nmo_object_id_t id, eng_vector_t expected)
{
    eng_vector_t actual = eng_entity_position(g->f.engine,eng_entity_index(g->f.engine,id),-1);
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z));
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,1e-4f);
}

static void children(child_fixture_t *g, nmo_object_id_t id, const nmo_object_id_t *expected, size_t count)
{
    int32_t parent = eng_entity_index(g->f.engine,id), current = g->f.engine->entities.data[parent].first_child, previous = -1;
    for (size_t i = 0; i < count; i++) {
        ASSERT_TRUE(current >= 0); const eng_entity_t *child = &g->f.engine->entities.data[current];
        ASSERT_EQ(expected[i],child->id); ASSERT_EQ(parent,child->parent); ASSERT_EQ(previous,child->prev_sibling);
        previous = current; current = child->next_sibling;
    }
    ASSERT_EQ(-1,current); ASSERT_EQ(previous,g->f.engine->entities.data[parent].last_child);
}

static void trigger(child_fixture_t *g)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,0));
}

static void set_parent_position(child_fixture_t *g, nmo_object_id_t id, eng_vector_t value)
{
    eng_entity_set_position(g->f.engine,eng_entity_index(g->f.engine,id),&value,-1,false);
}

TEST(engine_add_child, reparents_without_changing_world_matrices_and_then_follows_the_new_parent)
{
    child_fixture_t g; begin(&g); commit(&g); eng_matrix_t child = world(&g,g.child), grand = world(&g,g.grandchild), parent = world(&g,g.parent);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.parent,nmo_engine_entity_parent(g.f.engine,g.child));
    eng_matrix_t actual = world(&g,g.child); same_matrix(&actual,&child); actual = world(&g,g.grandchild); same_matrix(&actual,&grand); actual = world(&g,g.parent); same_matrix(&actual,&parent);
    const eng_matrix_t local = {.m={{0,-2.0f/3,0,0},{1.5f,0,0,0},{0,0,1,0},{1,2,3,1}}};
    same_matrix(&g.f.engine->entities.data[eng_entity_index(g.f.engine,g.child)].local,&local);
    children(&g,g.old_parent,&g.old_sibling,1); nmo_object_id_t expected[] = {g.sibling,g.child}; children(&g,g.parent,expected,COUNT(expected));
    set_parent_position(&g,g.parent,(eng_vector_t){110,20,30}); position(&g,g.child,(eng_vector_t){104,22,42}); position(&g,g.grandchild,(eng_vector_t){106,25,46});
    set_parent_position(&g,g.old_parent,(eng_vector_t){190,-20,-30}); position(&g,g.child,(eng_vector_t){104,22,42});
    ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(1u,runs(&g.f,g.block));
    ASSERT_EQ(g.old_parent,nmo_ref_runtime_id(&((nmo_3dentity_state_t *)fixture_state(&g.f,g.child))->parent)); fixture_end(&g.f);
}

TEST(engine_add_child, repeated_adoption_preserves_sibling_order_and_reattachment_appends_once)
{
    child_fixture_t g; begin(&g); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    nmo_object_id_t expected[] = {g.sibling,g.child}; children(&g,g.parent,expected,COUNT(expected)); trigger(&g); children(&g,g.parent,expected,COUNT(expected));
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.child_parameter,&g.old_sibling,sizeof(g.old_sibling))); trigger(&g);
    nmo_object_id_t all[] = {g.sibling,g.child,g.old_sibling}; children(&g,g.parent,all,COUNT(all)); children(&g,g.old_parent,NULL,0);
    ASSERT_TRUE(eng_entity_set_parent(g.f.engine,eng_entity_index(g.f.engine,g.child),eng_entity_index(g.f.engine,g.old_parent),true));
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.child_parameter,&g.child,sizeof(g.child))); trigger(&g);
    nmo_object_id_t reordered[] = {g.sibling,g.old_sibling,g.child}; children(&g,g.parent,reordered,COUNT(reordered));
    ASSERT_EQ(4u,runs(&g.f,g.out)); fixture_end(&g.f);
}

TEST(engine_add_child, resamples_the_parent_target_and_child_reference_on_each_trigger)
{
    child_fixture_t g; begin(&g); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_parameter,&g.old_parent,sizeof(g.old_parent))); trigger(&g);
    ASSERT_EQ(g.old_parent,nmo_engine_entity_parent(g.f.engine,g.child)); children(&g,g.parent,&g.sibling,1);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.child_parameter,&g.sibling,sizeof(g.sibling))); trigger(&g);
    ASSERT_EQ(g.old_parent,nmo_engine_entity_parent(g.f.engine,g.sibling)); children(&g,g.parent,NULL,0);
    position(&g,g.child,(eng_vector_t){4,22,42}); position(&g,g.sibling,(eng_vector_t){0,1,2}); fixture_end(&g.f);
}

TEST(engine_add_child, missing_deleted_and_nonentity_children_are_noops_that_still_activate_out)
{
    for (int kind = 0; kind < 4; kind++) {
        child_fixture_t g; begin(&g);
        if (kind == 0 || kind == 2) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),kind == 0 ? 0 : (int32_t)g.block);
        if (kind == 3) { g.f.value_count--; nmo_parameterin_set_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,0)),0); }
        commit(&g); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.child);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); children(&g,g.parent,&g.sibling,1);
        if (kind != 1) { ASSERT_EQ(g.old_parent,nmo_engine_entity_parent(g.f.engine,g.child)); position(&g,g.child,(eng_vector_t){4,22,42}); }
        ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); fixture_end(&g.f);
    }
}

TEST(engine_add_child, self_and_descendant_attachments_leave_the_existing_tree_unchanged)
{
    for (int kind = 0; kind < 3; kind++) {
        child_fixture_t g; begin(&g);
        fixture_input_int(&g.f,fixture_target(&g.f,g.block),(int32_t)(kind == 0 ? g.parent : kind == 1 ? g.grandchild : g.child));
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),(int32_t)(kind == 0 ? g.parent : kind == 1 ? g.child : g.old_parent)); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.old_parent,nmo_engine_entity_parent(g.f.engine,g.child)); ASSERT_EQ(g.child,nmo_engine_entity_parent(g.f.engine,g.grandchild));
        ASSERT_EQ(0u,nmo_engine_entity_parent(g.f.engine,g.old_parent)); children(&g,g.parent,&g.sibling,1); ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
    }
}

TEST(engine_add_child, places_cannot_be_parented_to_other_places_and_still_finish_normally)
{
    child_fixture_t g; begin(&g);
    nmo_object_id_t parent = fixture_create(&g.f,NMO_CID_PLACE,"Place Parent"), child = fixture_create(&g.f,NMO_CID_PLACE,"Place Child");
    nmo_3dentity_state_t *state = fixture_state(&g.f,child); ASSERT_NOT_NULL(state); state->parent = nmo_ref_from_id(g.old_parent); state->has_entityndata_chunk = 1;
    fixture_input_int(&g.f,fixture_target(&g.f,g.block),(int32_t)parent); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),(int32_t)child); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.old_parent,nmo_engine_entity_parent(g.f.engine,child)); children(&g,parent,NULL,0);
    ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
}

TEST(engine_add_child, preserves_the_child_parent_update_flag_and_recomputes_local_pose_even_when_inheritance_is_disabled)
{
    child_fixture_t g; begin(&g); ((nmo_3dentity_state_t *)fixture_state(&g.f,g.child))->moveable_flags |= VX_MOVEABLE_DONTUPDATEFROMPARENT; commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); int32_t child = eng_entity_index(g.f.engine,g.child);
    ASSERT_TRUE((g.f.engine->entities.data[child].moveable_flags & VX_MOVEABLE_DONTUPDATEFROMPARENT) != 0);
    set_parent_position(&g,g.parent,(eng_vector_t){110,20,30}); position(&g,g.child,(eng_vector_t){4,22,42}); position(&g,g.grandchild,(eng_vector_t){6,25,46});
    g.f.engine->entities.data[child].moveable_flags &= ~VX_MOVEABLE_DONTUPDATEFROMPARENT;
    set_parent_position(&g,g.parent,(eng_vector_t){210,20,30}); position(&g,g.child,(eng_vector_t){204,22,42}); position(&g,g.grandchild,(eng_vector_t){206,25,46}); fixture_end(&g.f);
}

TEST(engine_add_child, missing_deleted_and_nonentity_targets_report_owner_errors_before_consuming_input)
{
    for (int kind = 0; kind < 3; kind++) {
        child_fixture_t g; begin(&g); if (kind != 1) fixture_input_int(&g.f,fixture_target(&g.f,g.block),kind == 0 ? 0 : (int32_t)g.block);
        commit(&g); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.parent);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0)));
        ASSERT_EQ(g.old_parent,nmo_engine_entity_parent(g.f.engine,g.child)); fixture_end(&g.f);
    }
}

static nmo_object_id_t saved_input(child_fixture_t *g, nmo_object_id_t pin, const char *text)
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

TEST(engine_add_child, loaded_and_copied_graphs_keep_parent_child_bindings_and_inheritance_flags_independent)
{
    for (int disabled = 0; disabled < 2; disabled++) {
        child_fixture_t g; begin(&g); if (disabled) ((nmo_3dentity_state_t *)fixture_state(&g.f,g.child))->moveable_flags |= VX_MOVEABLE_DONTUPDATEFROMPARENT;
        char text[32]; snprintf(text,sizeof(text),"%u",g.parent); nmo_object_id_t parameters[2];
        parameters[0] = saved_input(&g,fixture_target(&g.f,g.block),text);
        snprintf(text,sizeof(text),"%u",g.child); parameters[1] = saved_input(&g,BLOCK_PIN(&g.f,g.block,0),text);
        ASSERT_TRUE(parameters[0] != 0 && parameters[1] != 0); fixture_commit_bare(&g.f,NULL);
        nmo_beobject_state_t *owner = fixture_state(&g.f,g.f.owner_id);
        owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
        script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        const char *path = NMO_TEST_SCRATCH_FILE("engine_add_child.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(g.f.session,path,NULL,NULL));
        for (int started = 0; started < 2; started++) {
            engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host);
            nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&host,&options);
            if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
            nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
            int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"child.cmo",true,&file));
            if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
            nmo_object_id_t block = nmo_engine_object_named(host.engine,"Add Child",NMO_CID_BEHAVIOR); ASSERT_TRUE(block != 0);
            const eng_module_t *module = eng_module_of(host.engine,block,NULL);
            const nmo_object_id_t original[] = {g.f.owner_id,g.old_parent,g.parent,g.child,g.grandchild,g.sibling,g.old_sibling,g.f.script_id,g.block,g.out};
            nmo_object_id_t ids[12] = {0}, copies[12] = {0};
            for (size_t i = 0; i < COUNT(original); i++) { ids[i] = eng_module_id(module,original[i]); ASSERT_TRUE(ids[i] != 0); }
            for (size_t i = 0; i < COUNT(parameters); i++) ids[10+i] = eng_module_id(module,parameters[i]);
            ASSERT_TRUE(ids[3] != g.child); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); ASSERT_EQ(ids[2],nmo_engine_entity_parent(host.engine,ids[3]));
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,ids,COUNT(ids),false,copies));
            ASSERT_EQ(copies[2],nmo_engine_entity_parent(host.engine,copies[3])); ASSERT_EQ(copies[3],nmo_engine_entity_parent(host.engine,copies[4]));
            child_fixture_t view = {.f.engine=host.engine}; set_parent_position(&view,copies[2],(eng_vector_t){210,20,30});
            position(&view,copies[3],(eng_vector_t){disabled ? 4 : 204,22,42}); position(&view,ids[3],(eng_vector_t){4,22,42});
            ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[10],&copies[1],sizeof(copies[1])));
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,ids[7],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[7],true,true));
            ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); ASSERT_EQ(ids[2],nmo_engine_entity_parent(host.engine,ids[3])); ASSERT_EQ(copies[1],nmo_engine_entity_parent(host.engine,copies[3]));
            children(&view,copies[2],&copies[5],1); nmo_object_id_t siblings[] = {copies[6],copies[3]}; children(&view,copies[1],siblings,COUNT(siblings));
            nmo_engine_destroy_object(host.engine,ids[3]); nmo_engine_destroy_object(host.engine,ids[2]); nmo_engine_destroy_object(host.engine,ids[1]);
            int32_t child = eng_entity_index(host.engine,copies[3]); ASSERT_EQ(disabled != 0,(host.engine->entities.data[child].moveable_flags & VX_MOVEABLE_DONTUPDATEFROMPARENT) != 0);
            host.engine->entities.data[child].moveable_flags &= ~VX_MOVEABLE_DONTUPDATEFROMPARENT;
            set_parent_position(&view,copies[1],(eng_vector_t){90,-20,-30});
            position(&view,copies[3],(eng_vector_t){disabled ? 104 : 304,22,42}); position(&view,copies[4],(eng_vector_t){disabled ? 106 : 306,25,46});
            ASSERT_EQ(2u,runs(&host,ids[8])); ASSERT_EQ(1u,runs(&host,copies[8])); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,10)); ASSERT_EQ(1u,runs(&host,copies[8])); fixture_end(&host);
        }
        ASSERT_EQ(g.old_parent,nmo_ref_runtime_id(&((nmo_3dentity_state_t *)fixture_state(&g.f,g.child))->parent)); fixture_end(&g.f); remove(path);
    }
}

static void count_operations(const nmo_engine_event_t *event, void *data)
{
    if (event->kind == NMO_ENGINE_EVENT_OPERATION) (*(size_t *)data)++;
}

TEST(engine_add_child, local_matrix_and_parent_query_blocks_observe_each_new_attachment_in_the_same_frame)
{
    child_fixture_t g; begin(&g);
    nmo_object_id_t local = fixture_block(&g.f,NMO_GUID(0x21f5f30d,0x08d5a1db),"Set Adopted Local Matrix"); ASSERT_TRUE(local != 0);
    ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,g.child_parameter,fixture_target(&g.f,local)));
    nmo_object_id_t matrix_parameter = 0; ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(g.f.tx,BLOCK_PIN(&g.f,local,0),&matrix_parameter));
    eng_matrix_t matrix; eng_matrix_identity(&matrix); matrix.m[3][0] = 1; matrix.m[3][1] = 2; matrix.m[3][2] = 3;
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_bytes(g.f.tx,matrix_parameter,(const uint8_t *)matrix.m,sizeof(matrix.m)));
    nmo_object_id_t operation = 0, parent_result = 0;
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g.f.tx,g.f.script_id,NMO_SCRIPT_EDIT_PARAM_LOCAL,CKPGUID_3DENTITY,"Queried Parent",&parent_result));
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_operation(g.f.tx,g.f.script_id,NMO_GUID(0x5bf9718f,0x030b698c),g.child_parameter,0,parent_result,&operation));
    const nmo_parameteroperation_state_t *op = fixture_state(&g.f,operation); ASSERT_NOT_NULL(op);
    /* Select CK3dEntityGetParent3dEntity explicitly; the catalogue also lists Basic Object. */
    ((nmo_parameterin_state_t *)fixture_state(&g.f,nmo_parameteroperation_in1_id(op)))->type_guid = CKPGUID_3DENTITY;
    nmo_object_id_t move = fixture_block(&g.f,NMO_GUID(0xe456e78a,0x456789aa),"Move Queried Parent"); ASSERT_TRUE(move != 0);
    ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,nmo_parameteroperation_out_id(op),fixture_target(&g.f,move)));
    const eng_vector_t position_value = {110,20,30}; fixture_input_value(&g.f,BLOCK_PIN(&g.f,move,0),&position_value,sizeof(position_value));
    fixture_link(&g.f,BLOCK_OUT(&g.f,g.block,0),BLOCK_IN(&g.f,local,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,local,0),BLOCK_IN(&g.f,move,0),0); commit(&g);
    size_t operations = 0; nmo_engine_set_trace(g.f.engine,count_operations,&operations); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    ASSERT_EQ(g.parent,nmo_engine_entity_parent(g.f.engine,g.child)); position(&g,g.parent,position_value); position(&g,g.old_parent,(eng_vector_t){-10,-20,-30});
    position(&g,g.child,(eng_vector_t){104,22,42}); position(&g,g.grandchild,(eng_vector_t){101,24,46});
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.child_parameter,&g.old_sibling,sizeof(g.old_sibling))); trigger(&g);
    ASSERT_EQ(g.parent,nmo_engine_entity_parent(g.f.engine,g.old_sibling)); position(&g,g.old_sibling,(eng_vector_t){104,22,42});
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_parameter,&g.old_parent,sizeof(g.old_parent))); trigger(&g);
    ASSERT_EQ(g.old_parent,nmo_engine_entity_parent(g.f.engine,g.old_sibling)); position(&g,g.old_parent,position_value); position(&g,g.old_sibling,(eng_vector_t){111,22,33});
    position(&g,g.child,(eng_vector_t){104,22,42}); position(&g,g.grandchild,(eng_vector_t){101,24,46});
    children(&g,g.old_parent,&g.old_sibling,1); nmo_object_id_t siblings[] = {g.sibling,g.child}; children(&g,g.parent,siblings,COUNT(siblings));
    ASSERT_EQ(3u,operations); ASSERT_EQ(3u,runs(&g.f,g.block)); ASSERT_EQ(3u,runs(&g.f,local)); ASSERT_EQ(3u,runs(&g.f,move)); ASSERT_EQ(3u,runs(&g.f,g.out));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(3u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_add_child, reparents_without_changing_world_matrices_and_then_follows_the_new_parent);
REGISTER_TEST(engine_add_child, repeated_adoption_preserves_sibling_order_and_reattachment_appends_once);
REGISTER_TEST(engine_add_child, resamples_the_parent_target_and_child_reference_on_each_trigger);
REGISTER_TEST(engine_add_child, missing_deleted_and_nonentity_children_are_noops_that_still_activate_out);
REGISTER_TEST(engine_add_child, self_and_descendant_attachments_leave_the_existing_tree_unchanged);
REGISTER_TEST(engine_add_child, places_cannot_be_parented_to_other_places_and_still_finish_normally);
REGISTER_TEST(engine_add_child, preserves_the_child_parent_update_flag_and_recomputes_local_pose_even_when_inheritance_is_disabled);
REGISTER_TEST(engine_add_child, missing_deleted_and_nonentity_targets_report_owner_errors_before_consuming_input);
REGISTER_TEST(engine_add_child, loaded_and_copied_graphs_keep_parent_child_bindings_and_inheritance_flags_independent);
REGISTER_TEST(engine_add_child, local_matrix_and_parent_query_blocks_observe_each_new_attachment_in_the_same_frame);
TEST_MAIN_END()
