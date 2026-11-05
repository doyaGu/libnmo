#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"

#include <float.h>
#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define PI_F 3.14159265358979323846f
#define GUID_GET_EULER NMO_GUID(0x0c4977a8,0x6c645d14)
#define GUID_SET_EULER NMO_GUID(0x0c4966d8,0x6c0c6d14)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct euler_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, target, child, block, out, target_parameter;
} euler_fixture_t;

/* Compose independent row-vector axis rotations instead of the expanded Euler formula. */
static eng_matrix_t rotation(float x, float y, float z)
{
    eng_matrix_t result; eng_matrix_identity(&result);
    const float angles[] = {x,y,z};
    for (int axis = 0; axis < 3; axis++) {
        int p = (axis+1)%3, q = (p+1)%3;
        double s = sin(angles[axis]), c = cos(angles[axis]);
        for (int row = 0; row < 3; row++) {
            double a = result.m[row][p], b = result.m[row][q];
            result.m[row][p] = (float)(a*c-b*s); result.m[row][q] = (float)(a*s+b*c);
        }
    }
    return result;
}

static void begin(euler_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Parent");
    g->target = fixture_create(&g->f,NMO_CID_3DENTITY,"Euler Target");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child");
    nmo_3dentity_state_t *parent = fixture_state(&g->f,g->parent), *target = fixture_state(&g->f,g->target);
    nmo_3dentity_state_t *child = fixture_state(&g->f,g->child); ASSERT_NOT_NULL(parent); ASSERT_NOT_NULL(target); ASSERT_NOT_NULL(child);
    eng_matrix_t m = rotation(0,0,.6f); m.m[3][0] = 10;
    memcpy(parent->world_matrix,m.m,sizeof(m.m)); parent->has_entityndata_chunk = 1;
    m = rotation(.3f,-.4f,.7f); m.m[3][0] = 3; m.m[3][1] = 4; m.m[3][2] = 5;
    memcpy(target->world_matrix,m.m,sizeof(m.m)); target->has_entityndata_chunk = 1; target->parent = nmo_ref_from_id(g->parent);
    child->parent = nmo_ref_from_id(g->target); child->has_entityndata_chunk = 1;
    child->world_matrix[12] = 6; child->world_matrix[13] = 8; child->world_matrix[14] = 10;
    g->f.script_id = fixture_add_script(&g->f,"Euler Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_GET_EULER,"Get Euler Orientation"); g->out = fixture_block(&g->f,GUID_NOP,"After Euler");
    ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->target);
    g->target_parameter = nmo_parameterin_source_id(fixture_state(&g->f,fixture_target(&g->f,g->block)));
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void commit(euler_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_GET_EULER));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void trigger(euler_fixture_t *g)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,0));
}

static void angles(engine_fixture_t *f, nmo_object_id_t block, float x, float y, float z)
{
    const float expected[] = {x,y,z};
    nmo_object_t *object = eng_find_object(f->engine,block); ASSERT_NOT_NULL(object);
    const nmo_behavior_state_t *state = nmo_object_get_state(object); ASSERT_NOT_NULL(state);
    const eng_module_t *module = eng_module_of(f->engine,block,NULL); ASSERT_NOT_NULL(module);
    for (int i = 0; i < 3; i++) {
        nmo_object_id_t output = eng_module_id(module,nmo_behavior_ref_array_get_id(&state->out_parameters,(size_t)i));
        float actual = NAN; read_value(f,output,&actual,sizeof(actual));
        ASSERT_TRUE(isfinite(actual)); ASSERT_FLOAT_EQ(expected[i],actual,3e-7f);
    }
}

static void same_world(nmo_engine_t *engine, nmo_object_id_t id, const float *expected)
{
    float actual[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(engine,id,actual));
    ASSERT_EQ(0,memcmp(actual,expected,sizeof(actual)));
}

TEST(engine_get_euler, reads_world_angles_in_radians_without_changing_target_children_or_scheduling)
{
    euler_fixture_t g; begin(&g); commit(&g);
    for (int i = 0; i < 3; i++) {
        const nmo_parameterout_state_t *state = fixture_state(&g.f,BLOCK_POUT(&g.f,g.block,i)); ASSERT_NOT_NULL(state);
        ASSERT_TRUE(nmo_guid_equals(CKPGUID_ANGLE,state->base.type_guid));
    }
    angles(&g.f,g.block,0,0,0);
    float target[16], child[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.target,target));
    ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.child,child));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); angles(&g.f,g.block,.3f,-.4f,.7f);
    same_world(g.f.engine,g.target,target); same_world(g.f.engine,g.child,child);
    ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1000)); ASSERT_EQ(1u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST(engine_get_euler, signed_axis_rotations_and_mixed_angles_follow_the_live_world_matrix)
{
    euler_fixture_t g; begin(&g); commit(&g);
    const float xz[] = {-2.7f,-.8f,0,.3f,2.8f}, y[] = {-1.4f,-.4f,0,.6f,1.4f};
    for (size_t x = 0; x < COUNT(xz); x++) for (size_t p = 0; p < COUNT(y); p++) for (size_t z = 0; z < COUNT(xz); z++) {
        eng_matrix_t m = rotation(xz[x],y[p],xz[z]); m.m[3][0] = 300; m.m[3][1] = -400;
        eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.target),&m,true); trigger(&g);
        angles(&g.f,g.block,xz[x],y[p],xz[z]); same_world(g.f.engine,g.target,&m.m[0][0]);
    }
    ASSERT_EQ(125u,runs(&g.f,g.out)); fixture_end(&g.f);
}

TEST(engine_get_euler, scaled_and_sheared_rows_are_read_directly_without_rotation_normalization)
{
    euler_fixture_t g; begin(&g); commit(&g);
    const eng_matrix_t m = {.m={{3,4,-12,0},{.25f,5,6,0},{.5f,-2,8,0},{100,200,300,1}}};
    eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.target),&m,true); trigger(&g);
    angles(&g.f,g.block,.643501109f,1.176005207f,.927295218f); same_world(g.f.engine,g.target,&m.m[0][0]); fixture_end(&g.f);
}

TEST(engine_get_euler, exact_gimbal_threshold_uses_the_locked_branch_with_zero_z)
{
    euler_fixture_t g; begin(&g); commit(&g);
    float edge = ldexpf(1,-19); const float sizes[] = {0,nextafterf(edge,0),edge,nextafterf(edge,FLT_MAX)};
    for (int sign = -1; sign <= 1; sign += 2) for (size_t i = 0; i < COUNT(sizes); i++) {
        eng_matrix_t m = {.m={{0,sizes[i],(float)sign,0},{0,0,.6f,0},{0,-1,.8f,0},{0,0,0,1}}};
        eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.target),&m,true); trigger(&g);
        bool locked = i < 3;
        angles(&g.f,g.block,locked ? PI_F*.5f : .643501109f,(float)atan2(-sign,(double)sizes[i]),locked ? 0 : PI_F*.5f);
    }
    fixture_end(&g.f);
}

TEST(engine_get_euler, finite_extreme_axes_keep_the_native_wide_norm_and_tiny_axes_take_the_locked_branch)
{
    euler_fixture_t g; begin(&g); ((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->parent = nmo_ref_from_id(0); commit(&g);
    eng_matrix_t m = {.m={{FLT_MAX,FLT_MAX,-FLT_MAX,0},{0,0,1,0},{0,-1,1,0},{0,0,0,1}}};
    eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.target),&m,true); trigger(&g);
    angles(&g.f,g.block,PI_F*.25f,.615479709f,PI_F*.25f);
    float tiny = nextafterf(0,1); m.m[0][0] = m.m[0][1] = tiny; m.m[0][2] = -tiny;
    eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.target),&m,true); trigger(&g);
    angles(&g.f,g.block,PI_F*.5f,.615479709f,0); fixture_end(&g.f);
}

TEST(engine_get_euler, live_targets_can_switch_between_entities)
{
    euler_fixture_t g; begin(&g); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); angles(&g.f,g.block,.3f,-.4f,.7f);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_parameter,&g.parent,sizeof(g.parent))); trigger(&g);
    angles(&g.f,g.block,0,0,.6f);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_parameter,&g.f.owner_id,sizeof(g.f.owner_id))); trigger(&g);
    angles(&g.f,g.block,0,0,0); fixture_end(&g.f);
}

TEST(engine_get_euler, absent_deleted_and_nonentity_targets_preserve_outputs_and_leave_input_active)
{
    for (int kind = 0; kind < 3; kind++) {
        euler_fixture_t g; begin(&g); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.target);
        else {
            nmo_object_id_t bad = kind == 0 ? 0 : g.block;
            ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_parameter,&bad,sizeof(bad)));
        }
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true));
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
        ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); angles(&g.f,g.block,.3f,-.4f,.7f);
        ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
    }
}

static nmo_object_id_t saved_target(euler_fixture_t *g, nmo_object_id_t block, nmo_object_id_t target)
{
    nmo_object_id_t parameter = 0; char text[32]; snprintf(text,sizeof(text),"%u",target);
    if (nmo_script_edit_ensure_input_parameter_source(g->f.tx,fixture_target(&g->f,block),&parameter) != NMO_OK ||
        nmo_script_edit_set_parameter_value(g->f.tx,parameter,text) != NMO_OK) return 0;
    return parameter;
}

static nmo_object_id_t forward_angles(euler_fixture_t *g, nmo_object_id_t *target, nmo_object_id_t *child)
{
    *target = fixture_create(&g->f,NMO_CID_3DENTITY,"Driven Entity");
    *child = fixture_create(&g->f,NMO_CID_3DENTITY,"Driven Child");
    nmo_3dentity_state_t *state = fixture_state(&g->f,*target), *descendant = fixture_state(&g->f,*child);
    if (state == NULL || descendant == NULL) return 0;
    state->has_entityndata_chunk = descendant->has_entityndata_chunk = 1;
    state->world_matrix[12] = 20; state->world_matrix[13] = 30; state->world_matrix[14] = 40;
    descendant->parent = nmo_ref_from_id(*target);
    descendant->world_matrix[12] = 22; descendant->world_matrix[13] = 33; descendant->world_matrix[14] = 44;
    nmo_object_id_t setter = fixture_block(&g->f,GUID_SET_EULER,"Apply Euler");
    if (setter == 0) return 0;
    /* The native unmerged setting has three Angle pins before Hierarchy and Ref. */
    for (int i = 0; i < 3; i++) if (nmo_script_edit_remove_parameter(g->f.tx,BLOCK_PIN(&g->f,setter,0),true) != NMO_OK) return 0;
    const char *names[] = {"X","Y","Z","Hierarchy","Referential"};
    const nmo_guid_t types[] = {CKPGUID_ANGLE,CKPGUID_ANGLE,CKPGUID_ANGLE,CKPGUID_BOOL,CKPGUID_3DENTITY};
    for (size_t i = 0; i < COUNT(names); i++) {
        nmo_object_id_t pin = 0;
        if (nmo_script_edit_add_parameter(g->f.tx,setter,NMO_SCRIPT_EDIT_PARAM_IN,types[i],names[i],&pin) != NMO_OK) return 0;
        if (i < 3 && nmo_script_edit_connect_parameter(g->f.tx,BLOCK_POUT(&g->f,g->block,(int)i),pin) != NMO_OK) return 0;
        if (i == 3) {
            nmo_object_id_t source = 0;
            if (nmo_script_edit_ensure_input_parameter_source(g->f.tx,pin,&source) != NMO_OK ||
                nmo_script_edit_set_parameter_value(g->f.tx,source,"TRUE") != NMO_OK) return 0;
        }
    }
    const nmo_behavior_state_t *behavior = fixture_state(&g->f,setter);
    nmo_object_id_t setting = nmo_behavior_ref_array_get_id(&behavior->local_parameters,0);
    if (nmo_script_edit_set_parameter_value(g->f.tx,setting,"FALSE") != NMO_OK || saved_target(g,setter,*target) == 0) return 0;
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,setter,0),0);
    return setter;
}

static void oriented(nmo_engine_t *engine, nmo_object_id_t target, const eng_matrix_t *expected)
{
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(engine,target,matrix));
    for (int row = 0; row < 3; row++) for (int column = 0; column < 3; column++) {
        ASSERT_TRUE(isfinite(matrix[4*row+column]));
        ASSERT_FLOAT_EQ(expected->m[row][column],matrix[4*row+column],2e-6f);
    }
}

TEST(engine_get_euler, same_frame_rotation_reads_feed_unmerged_euler_inputs_and_move_children_through_completion)
{
    euler_fixture_t g; begin(&g);
    nmo_3dentity_state_t *source = fixture_state(&g.f,g.target);
    eng_matrix_t initial; eng_matrix_identity(&initial); initial.m[3][0] = 3; initial.m[3][1] = 4; initial.m[3][2] = 5;
    memcpy(source->world_matrix,initial.m,sizeof(initial.m));
    nmo_object_id_t destination = 0, child = 0, setter = forward_angles(&g,&destination,&child); ASSERT_TRUE(setter != 0);
    const nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
    nmo_object_id_t first_link = nmo_behavior_ref_array_get_id(&script->sub_behavior_links,0);
    ASSERT_EQ(NMO_OK,nmo_script_edit_remove_behavior_link(g.f.tx,g.f.script_id,first_link));
    nmo_object_id_t rotate = fixture_block(&g.f,NMO_GUID(0xffffffee,0xeeffffff),"Rotate Source"); ASSERT_TRUE(rotate != 0);
    fixture_input_int(&g.f,fixture_target(&g.f,rotate),(int32_t)g.target);
    const eng_vector_t axis = {0,0,1}; const float step = .8f;
    fixture_input_value(&g.f,BLOCK_PIN(&g.f,rotate,0),&axis,sizeof(axis));
    fixture_input_value(&g.f,BLOCK_PIN(&g.f,rotate,1),&step,sizeof(step));
    fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,rotate,0),0);
    fixture_link(&g.f,BLOCK_OUT(&g.f,rotate,0),BLOCK_IN(&g.f,g.block,0),0); commit(&g);
    for (int frame = 1; frame <= 3; frame++) {
        /* Native Rotate's positive Z-axis angle gives a negative Euler Z. */
        trigger(&g); float angle = -frame*step, c = cosf(angle), s = sinf(angle);
        angles(&g.f,g.block,0,0,angle); eng_matrix_t expected = rotation(0,0,angle); oriented(g.f.engine,destination,&expected);
        float m[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,child,m));
        ASSERT_FLOAT_EQ(20+2*c-3*s,m[12],2e-5f); ASSERT_FLOAT_EQ(30+2*s+3*c,m[13],2e-5f); ASSERT_FLOAT_EQ(44,m[14],1e-5f);
    }
    ASSERT_EQ(3u,runs(&g.f,setter)); ASSERT_EQ(3u,runs(&g.f,g.block)); ASSERT_EQ(3u,runs(&g.f,g.out));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(3u,runs(&g.f,setter)); fixture_end(&g.f);
}

static const char *resolve_file(void *data, const char *name)
{
    (void)name; return data;
}

static nmo_object_id_t loaded_target_source(nmo_engine_t *engine, nmo_object_id_t block)
{
    nmo_object_t *object = eng_find_object(engine,block);
    const nmo_behavior_state_t *behavior = object != NULL ? nmo_object_get_state(object) : NULL;
    const eng_module_t *module = eng_module_of(engine,block,NULL);
    if (behavior == NULL || module == NULL) return 0;
    object = eng_find_object(engine,eng_module_id(module,nmo_behavior_target_parameter_id(behavior)));
    const nmo_parameterin_state_t *input = object != NULL ? nmo_object_get_state(object) : NULL;
    return input != NULL ? eng_module_id(module,nmo_parameterin_source_id(input)) : 0;
}

TEST(engine_get_euler, loaded_and_copied_graphs_remap_live_targets_and_all_three_output_connections)
{
    euler_fixture_t g; begin(&g); nmo_object_id_t destination = 0, child = 0;
    nmo_object_id_t setter = forward_angles(&g,&destination,&child); ASSERT_TRUE(setter != 0);
    nmo_object_id_t reader_target = saved_target(&g,g.block,g.target), writer_target = saved_target(&g,setter,destination);
    ASSERT_TRUE(reader_target != 0 && writer_target != 0); fixture_commit_bare(&g.f,NULL);
    nmo_beobject_state_t *owner = fixture_state(&g.f,g.f.owner_id);
    owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
    nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
    script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
    const char *path = NMO_TEST_SCRATCH_FILE("engine_get_euler.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(g.f.session,path,NULL,NULL));
    for (int started = 0; started < 2; started++) {
        engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host);
        nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&host,&options);
        if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
        nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
        int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"get-euler.cmo",true,&file));
        if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
        nmo_object_id_t reader = nmo_engine_object_named(host.engine,"Get Euler Orientation",NMO_CID_BEHAVIOR); ASSERT_TRUE(reader != 0);
        const eng_module_t *module = eng_module_of(host.engine,reader,NULL);
        /* Saving compacts IDs after replacing the setter's merged input pins. */
        const char *names[] = {"Owner","Parent","Euler Target","Child","Driven Entity","Driven Child","Euler Script","Get Euler Orientation","Apply Euler","After Euler"};
        nmo_object_id_t ids[12] = {0}, copies[12] = {0};
        for (size_t i = 0; i < COUNT(names); i++) {
            nmo_object_t *object = nmo_object_repository_find_by_name(module->repo,names[i]); ASSERT_NOT_NULL(object);
            ids[i] = eng_module_id(module,nmo_object_get_id(object)); ASSERT_TRUE(ids[i] != 0);
        }
        ids[10] = loaded_target_source(host.engine,ids[7]); ids[11] = loaded_target_source(host.engine,ids[8]);
        ASSERT_TRUE(ids[10] != 0 && ids[11] != 0);
        ASSERT_TRUE(ids[2] != g.target); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
        angles(&host,ids[7],.3f,-.4f,.7f); eng_matrix_t expected = rotation(.3f,-.4f,.7f); oriented(host.engine,ids[4],&expected);
        ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,ids,COUNT(ids),false,copies));
        for (size_t i = 0; i < COUNT(copies); i++) ASSERT_TRUE(copies[i] != 0);
        ASSERT_EQ(copies[4],nmo_engine_entity_parent(host.engine,copies[5]));
        eng_matrix_t changed = rotation(-.6f,.2f,-1.1f); changed.m[3][0] = 50; changed.m[3][1] = 60;
        eng_entity_set_world_matrix(host.engine,eng_entity_index(host.engine,copies[2]),&changed,false);
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,ids[6],true,true));
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[6],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
        angles(&host,ids[7],.3f,-.4f,.7f); angles(&host,copies[7],-.6f,.2f,-1.1f);
        oriented(host.engine,ids[4],&expected); oriented(host.engine,copies[4],&changed);
        nmo_object_id_t reference = 0; read_value(&host,copies[10],&reference,sizeof(reference)); ASSERT_EQ(copies[2],reference);
        read_value(&host,copies[11],&reference,sizeof(reference)); ASSERT_EQ(copies[4],reference);
        nmo_engine_destroy_object(host.engine,ids[2]); nmo_engine_destroy_object(host.engine,ids[4]); nmo_engine_destroy_object(host.engine,ids[0]);
        changed = rotation(.2f,.5f,-.8f); eng_entity_set_world_matrix(host.engine,eng_entity_index(host.engine,copies[2]),&changed,false);
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[6],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
        angles(&host,copies[7],.2f,.5f,-.8f); oriented(host.engine,copies[4],&changed);
        ASSERT_EQ(2u,runs(&host,copies[8])); ASSERT_EQ(2u,runs(&host,copies[9]));
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,100)); ASSERT_EQ(2u,runs(&host,copies[7])); fixture_end(&host);
    }
    eng_matrix_t saved = rotation(.3f,-.4f,.7f);
    const nmo_3dentity_state_t *state = fixture_state(&g.f,g.target);
    for (int row = 0; row < 3; row++) for (int col = 0; col < 3; col++) ASSERT_FLOAT_EQ(saved.m[row][col],state->world_matrix[row*4+col],0);
    fixture_end(&g.f); remove(path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_get_euler, reads_world_angles_in_radians_without_changing_target_children_or_scheduling);
REGISTER_TEST(engine_get_euler, signed_axis_rotations_and_mixed_angles_follow_the_live_world_matrix);
REGISTER_TEST(engine_get_euler, scaled_and_sheared_rows_are_read_directly_without_rotation_normalization);
REGISTER_TEST(engine_get_euler, exact_gimbal_threshold_uses_the_locked_branch_with_zero_z);
REGISTER_TEST(engine_get_euler, finite_extreme_axes_keep_the_native_wide_norm_and_tiny_axes_take_the_locked_branch);
REGISTER_TEST(engine_get_euler, live_targets_can_switch_between_entities);
REGISTER_TEST(engine_get_euler, absent_deleted_and_nonentity_targets_preserve_outputs_and_leave_input_active);
REGISTER_TEST(engine_get_euler, same_frame_rotation_reads_feed_unmerged_euler_inputs_and_move_children_through_completion);
REGISTER_TEST(engine_get_euler, loaded_and_copied_graphs_remap_live_targets_and_all_three_output_connections);
TEST_MAIN_END()
