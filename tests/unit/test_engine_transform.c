#include "engine_fixture.h"
#include "../../src/engine/engine_array.h"
#include "../../src/engine/engine_module.h"

#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/nmo_ref.h"
#include "type/nmo_object_guids.h"

#include <math.h>
#include <stdio.h>

#define GUID_SET_POSITION NMO_GUID(0xe456e78a, 0x456789aa)
#define GUID_SET_PARENT NMO_GUID(0x9d9d9d98, 0x7e7a7f75)
#define GUID_ROTATE NMO_GUID(0xffffffee, 0xeeffffff)
#define GUID_SCALE NMO_GUID(0x41236987, 0xa54a87a6)
#define GUID_SET_EULER_ORIENTATION NMO_GUID(0x0c4966d8, 0x6c0c6d14)
#define GUID_LOOK_AT NMO_GUID(0x26843971, 0x86249317)
#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_NEAREST_IN_GROUP NMO_GUID(0x085207eb, 0x584950d8)
#define GUID_SCALEABLE_PROXIMITY NMO_GUID(0x2a2a63ca, 0x00826175)
#define GUID_SET_DYNAMIC_POSITION NMO_GUID(0x0fd4755f, 0x7de22dc8)
#define GUID_SPEEDOMETER NMO_GUID(0x51bd5521, 0x672c67bf)
#define GUID_TT_EXTRA NMO_GUID(0x36106bd9, 0x51813906)
#define GUID_RAY_INTERSECTION NMO_GUID(0x671e4a87, 0x383b2912)
#define GUID_BOX_BOX_INTERSECTION NMO_GUID(0x64154401, 0x76cf37af)
#define GUID_PROXIMITY_VOLUME NMO_GUID(0x38571b61, 0x64cd2174)
#define GUID_TEXTURE_SINE NMO_GUID(0x009c1208, 0x3a8d779e)
#define GUID_CHARACTER_NEAREST NMO_GUID(0x4321abcd, 0x0eeaabdd)

#define PI_F 3.14159265f

/* An entity saved at `x, y, z` with unit axes, under `parent` (0 for none) */
static nmo_object_id_t place_entity(engine_fixture_t *f, nmo_object_id_t id, float x, float y,
                                    float z, nmo_object_id_t parent)
{
    nmo_3dentity_state_t *state = (nmo_3dentity_state_t *)fixture_state(f, id);
    if (state == NULL) {
        return 0;
    }
    memset(state->world_matrix, 0, sizeof(state->world_matrix));
    state->world_matrix[0] = state->world_matrix[5] = state->world_matrix[10] = 1.0f;
    state->world_matrix[15] = 1.0f;
    state->world_matrix[12] = x;
    state->world_matrix[13] = y;
    state->world_matrix[14] = z;
    if (parent != 0) {
        state->parent = nmo_ref_from_id(parent);
    }
    return id;
}

static void world_matrix(engine_fixture_t *f, nmo_object_id_t id, float m[16])
{
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f->engine, id, m));
}

#define ASSERT_POSITION(f, id, x, y, z)                                                            \
    do {                                                                                           \
        float m_[16];                                                                              \
        world_matrix((f), (id), m_);                                                               \
        ASSERT_FLOAT_EQ((x), m_[12], 0.0001f);                                                     \
        ASSERT_FLOAT_EQ((y), m_[13], 0.0001f);                                                     \
        ASSERT_FLOAT_EQ((z), m_[14], 0.0001f);                                                     \
    } while (0)

/* A block targeting `target`, linked after `after` */
static nmo_object_id_t target_block(engine_fixture_t *f, nmo_guid_t guid, const char *name,
                                    nmo_object_id_t target, nmo_object_id_t after)
{
    nmo_object_id_t block = fixture_block(f, guid, name);
    if (block == 0 || fixture_target(f, block) == 0) {
        return 0;
    }
    fixture_input_int(f, fixture_target(f, block), (int32_t)target);
    fixture_link(f, after, BLOCK_IN(f, block, 0), 0);
    return block;
}

static nmo_object_id_t object_group(engine_fixture_t *f, const char *name,
    const nmo_object_id_t *ids, size_t count)
{
    nmo_object_id_t group = fixture_create(f,NMO_CID_GROUP,name);
    nmo_group_state_t *state = fixture_state(f,group);
    nmo_ref_t *members = NULL;
    if (state == NULL || nmo_array_extend(&state->object_ids,count,(void **)&members) != NMO_OK) return 0;
    for (size_t i = 0; i < count; i++) members[i] = nmo_ref_from_id(ids[i]);
    return group;
}

static void query_nearest(engine_fixture_t *f, nmo_object_id_t block, nmo_object_id_t expected)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(f->engine,f->script_id,true,true));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f->engine,0));
    nmo_object_id_t result = 0;
    read_value(f,BLOCK_POUT(f,block,0),&result,sizeof(result));
    ASSERT_EQ(expected,result);
}

static void commit_nearest(engine_fixture_t *f)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(f,&options);
    ASSERT_TRUE(nmo_engine_has_block(f->engine,GUID_CHARACTER_NEAREST));
    ASSERT_EQ(NMO_OK,nmo_engine_start(f->engine,NMO_ENGINE_START_ALL));
}

TEST(engine_transform, character_nearest_uses_world_distance_group_order_and_live_members)
{
    engine_fixture_t f; fixture_begin_document(&f);
    nmo_object_id_t parent = fixture_create(&f,NMO_CID_3DENTITY,"Parent");
    nmo_object_id_t actor = fixture_create(&f,NMO_CID_CHARACTER,"Character");
    nmo_object_id_t root = fixture_create(&f,NMO_CID_BODYPART,"Root");
    nmo_object_id_t far = fixture_create(&f,NMO_CID_3DENTITY,"Far");
    nmo_object_id_t camera = fixture_create(&f,NMO_CID_CAMERA,"First at five");
    nmo_object_id_t tie = fixture_create(&f,NMO_CID_LIGHT,"Second at five");
    nmo_object_id_t projected = fixture_create(&f,NMO_CID_3DENTITY,"Far above");
    nmo_object_id_t material = fixture_create(&f,NMO_CID_MATERIAL,"Nonentity");
    ASSERT_TRUE(place_entity(&f,parent,1000,2000,3000,0) != 0);
    ASSERT_TRUE(place_entity(&f,actor,10,20,30,parent) != 0);
    ASSERT_TRUE(place_entity(&f,root,10,25,30,actor) != 0);
    ((nmo_character_state_t *)fixture_state(&f,actor))->root_body_part = nmo_ref_from_id(root);
    ASSERT_TRUE(place_entity(&f,far,10,26,30,0) != 0);
    ASSERT_TRUE(place_entity(&f,camera,13,20,34,parent) != 0);
    ASSERT_TRUE(place_entity(&f,tie,10,25,30,0) != 0);
    ASSERT_TRUE(place_entity(&f,projected,10,100,30,0) != 0);
    nmo_object_id_t nested = object_group(&f,"Nested",&root,1);
    nmo_object_id_t ids[] = {material,projected,nested,far,camera,tie};
    nmo_object_id_t group = object_group(&f,"Candidates",ids,sizeof(ids)/sizeof(ids[0])); ASSERT_TRUE(group != 0);
    f.script_id = fixture_add_script(&f,"Nearest script",&f.script_in); fixture_begin_edit(&f);
    nmo_object_id_t block = target_block(&f,GUID_CHARACTER_NEAREST,"Character nearest",actor,f.script_in);
    nmo_object_id_t after = fixture_block(&f,GUID_NOP,"After nearest"); ASSERT_TRUE(block != 0 && after != 0);
    fixture_input_int(&f,BLOCK_PIN(&f,block,0),(int32_t)group);
    fixture_link(&f,BLOCK_OUT(&f,block,0),BLOCK_IN(&f,after,0),0);
    commit_nearest(&f);
    query_nearest(&f,block,camera);
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine,actor,matrix));
    matrix[13] = 25; ASSERT_EQ(NMO_OK,nmo_engine_set_entity_world_matrix(f.engine,actor,matrix,false));
    query_nearest(&f,block,tie);
    nmo_engine_destroy_object(f.engine,tie); query_nearest(&f,block,far);
    ASSERT_TRUE(eng_group_add(f.engine,group,actor)); query_nearest(&f,block,actor);
    ASSERT_EQ(4u,runs(&f,block)); ASSERT_EQ(4u,runs(&f,after));
    fixture_end(&f);
}

TEST(engine_transform, character_nearest_keeps_native_range_and_clears_absent_or_deleted_groups)
{
    engine_fixture_t f; fixture_begin_document(&f);
    nmo_object_id_t actor = fixture_create(&f,NMO_CID_CHARACTER,"Character");
    nmo_object_id_t boundary = fixture_create(&f,NMO_CID_3DENTITY,"At limit");
    nmo_object_id_t beyond = fixture_create(&f,NMO_CID_3DENTITY,"Beyond limit");
    nmo_object_id_t inside = fixture_create(&f,NMO_CID_3DENTITY,"Inside limit");
    ASSERT_TRUE(place_entity(&f,boundary,100000000.0f,0,0,0) != 0);
    ASSERT_TRUE(place_entity(&f,beyond,nextafterf(100000000.0f,INFINITY),0,0,0) != 0);
    ASSERT_TRUE(place_entity(&f,inside,nextafterf(100000000.0f,0),0,0,0) != 0);
    nmo_object_id_t ids[] = {boundary,beyond}, group = object_group(&f,"Candidates",ids,2);
    nmo_object_id_t empty = object_group(&f,"Empty",NULL,0); ASSERT_TRUE(group != 0 && empty != 0);
    f.script_id = fixture_add_script(&f,"Nearest script",&f.script_in); fixture_begin_edit(&f);
    nmo_object_id_t block = target_block(&f,GUID_CHARACTER_NEAREST,"Character nearest",actor,f.script_in); ASSERT_TRUE(block != 0);
    fixture_input_int(&f,BLOCK_PIN(&f,block,0),(int32_t)group);
    nmo_object_id_t input = nmo_parameterin_source_id(fixture_state(&f,BLOCK_PIN(&f,block,0)));
    commit_nearest(&f);
    query_nearest(&f,block,0);
    ASSERT_TRUE(eng_group_add(f.engine,group,inside)); query_nearest(&f,block,inside);
    nmo_object_id_t inputs[] = {0,empty,actor,group};
    for (size_t i = 0; i < sizeof(inputs)/sizeof(inputs[0]); i++) {
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(f.engine,input,&inputs[i],sizeof(inputs[i])));
        query_nearest(&f,block,inputs[i] == group ? inside : 0);
    }
    nmo_engine_destroy_object(f.engine,group); query_nearest(&f,block,0);
    fixture_end(&f);
}

TEST(engine_transform, character_nearest_reports_missing_owners_without_replacing_the_last_result)
{
    for (int deleted = 0; deleted < 2; deleted++) {
        engine_fixture_t f; fixture_begin_document(&f);
        nmo_object_id_t actor = fixture_create(&f,NMO_CID_CHARACTER,"Character");
        nmo_object_id_t group = object_group(&f,"Candidates",&f.owner_id,1);
        f.script_id = fixture_add_script(&f,"Nearest script",&f.script_in); fixture_begin_edit(&f);
        nmo_object_id_t block = target_block(&f,GUID_CHARACTER_NEAREST,"Character nearest",actor,f.script_in); ASSERT_TRUE(block != 0);
        fixture_input_int(&f,BLOCK_PIN(&f,block,0),(int32_t)group);
        nmo_object_id_t input = nmo_parameterin_source_id(fixture_state(&f,fixture_target(&f,block)));
        commit_nearest(&f); query_nearest(&f,block,f.owner_id);
        if (deleted) nmo_engine_destroy_object(f.engine,actor);
        else {
            nmo_object_id_t none = 0; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(f.engine,input,&none,sizeof(none)));
        }
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(f.engine,f.script_id,true,true));
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(f.engine,0));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(f.engine,&failure));
        ASSERT_EQ(block,failure.behavior_id); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
        nmo_object_id_t result = 0; read_value(&f,BLOCK_POUT(&f,block,0),&result,sizeof(result)); ASSERT_EQ(f.owner_id,result);
        fixture_end(&f);
    }
}

static nmo_object_id_t saved_nearest_object(engine_fixture_t *f, nmo_object_id_t input, nmo_object_id_t id)
{
    nmo_object_id_t source = 0;
    char text[32]; snprintf(text,sizeof(text),"%u",id);
    if (nmo_script_edit_ensure_input_parameter_source(f->tx,input,&source) != NMO_OK ||
        nmo_script_edit_set_parameter_value(f->tx,source,text) != NMO_OK) return 0;
    return source;
}

static const char *resolve_nearest_file(void *data, const char *name)
{
    (void)name; return data;
}

static nmo_object_id_t nearest_output(nmo_engine_t *engine, nmo_object_id_t block)
{
    nmo_object_t *object = eng_find_object(engine,block);
    const nmo_behavior_state_t *state = object != NULL ? nmo_object_get_state(object) : NULL;
    nmo_object_id_t local = state != NULL ? nmo_behavior_ref_array_get_id(&state->out_parameters,0) : 0;
    nmo_object_id_t result = 0;
    nmo_object_id_t parameter = eng_module_id(eng_module_of(engine,block,NULL),local);
    const void *data = NULL; size_t size = 0;
    if (nmo_engine_param_value(engine,parameter,&data,&size,NULL) == NMO_OK && size >= sizeof(result)) memcpy(&result,data,sizeof(result));
    return result;
}

TEST(engine_transform, loaded_character_nearest_outputs_feed_transforms_and_remap_on_copy)
{
    engine_fixture_t source; fixture_begin_document(&source);
    nmo_object_id_t actor = fixture_create(&source,NMO_CID_CHARACTER,"Nearest actor");
    nmo_object_id_t near = fixture_create(&source,NMO_CID_3DENTITY,"Near candidate");
    nmo_object_id_t far = fixture_create(&source,NMO_CID_3DENTITY,"Far candidate");
    nmo_object_id_t material = fixture_create(&source,NMO_CID_MATERIAL,"Ignored candidate");
    ASSERT_TRUE(place_entity(&source,actor,5,0,0,0) != 0);
    ASSERT_TRUE(place_entity(&source,near,6,0,0,0) != 0);
    ASSERT_TRUE(place_entity(&source,far,7,0,0,0) != 0);
    nmo_object_id_t members[] = {material,near,far}, group = object_group(&source,"Nearest candidates",members,3); ASSERT_TRUE(group != 0);
    source.script_id = fixture_add_script(&source,"Nearest saved script",&source.script_in); fixture_begin_edit(&source);
    nmo_object_id_t query = fixture_block(&source,GUID_CHARACTER_NEAREST,"Saved nearest query");
    nmo_object_id_t move = fixture_block(&source,GUID_SET_POSITION,"Move nearest result"); ASSERT_TRUE(query != 0 && move != 0);
    nmo_object_id_t actor_parameter = saved_nearest_object(&source,fixture_target(&source,query),actor);
    nmo_object_id_t group_parameter = saved_nearest_object(&source,BLOCK_PIN(&source,query,0),group);
    ASSERT_TRUE(actor_parameter != 0 && group_parameter != 0);
    ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(source.tx,BLOCK_POUT(&source,query,0),fixture_target(&source,move)));
    nmo_object_id_t position = 0; float vector[] = {100,0,0};
    ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(source.tx,BLOCK_PIN(&source,move,0),&position));
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_bytes(source.tx,position,(const uint8_t *)vector,sizeof(vector)));
    fixture_link(&source,source.script_in,BLOCK_IN(&source,query,0),0);
    fixture_link(&source,BLOCK_OUT(&source,query,0),BLOCK_IN(&source,move,0),0);
    fixture_commit_bare(&source,NULL);
    nmo_beobject_state_t *owner = fixture_state(&source,source.owner_id);
    owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
    nmo_behavior_state_t *script = fixture_state(&source,source.script_id);
    script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
    const char *path = NMO_TEST_SCRATCH_FILE("engine_character_nearest.cmo");
    ASSERT_EQ(NMO_OK,nmo_session_save_file(source.session,path,NULL,NULL));
    for (int started = 0; started < 2; started++) {
        engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host);
        nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
        fixture_commit(&host,&options);
        if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
        nmo_engine_set_file_resolver(host.engine,resolve_nearest_file,(void *)path);
        int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"nearest.cmo",true,&file)); ASSERT_TRUE(file > 0);
        if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
        nmo_object_id_t loaded_actor = nmo_engine_object_named(host.engine,"Nearest actor",NMO_CID_CHARACTER);
        nmo_object_id_t loaded_near = nmo_engine_object_named(host.engine,"Near candidate",NMO_CID_3DENTITY);
        nmo_object_id_t loaded_far = nmo_engine_object_named(host.engine,"Far candidate",NMO_CID_3DENTITY);
        nmo_object_id_t loaded_group = nmo_engine_object_named(host.engine,"Nearest candidates",NMO_CID_GROUP);
        nmo_object_id_t loaded_query = nmo_engine_object_named(host.engine,"Saved nearest query",NMO_CID_BEHAVIOR);
        nmo_object_id_t loaded_move = nmo_engine_object_named(host.engine,"Move nearest result",NMO_CID_BEHAVIOR);
        nmo_object_id_t loaded_script = nmo_engine_object_named(host.engine,"Nearest saved script",NMO_CID_BEHAVIOR);
        ASSERT_TRUE(loaded_actor != 0 && loaded_actor != actor && loaded_near != near && loaded_query != query);
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
        ASSERT_EQ(loaded_near,nearest_output(host.engine,loaded_query));
        ASSERT_POSITION(&host,loaded_near,100,0,0); ASSERT_POSITION(&host,loaded_far,7,0,0);
        ASSERT_EQ(1u,runs(&host,loaded_query)); ASSERT_EQ(1u,runs(&host,loaded_move));
        nmo_object_id_t loaded_owner = 0;
        for (int32_t i = 0; i < nmo_engine_loaded_object_count(host.engine,file); i++) {
            nmo_object_id_t id = nmo_engine_loaded_object(host.engine,file,i);
            const char *name = nmo_engine_object_name(host.engine,id);
            if (nmo_engine_object_class(host.engine,id) == NMO_CID_3DENTITY && name != NULL && strcmp(name,"Owner") == 0) loaded_owner = id;
        }
        ASSERT_TRUE(loaded_owner != 0 && loaded_owner != host.owner_id);
        /* External CKParameter sources stay shared unless copied explicitly. */
        const eng_module_t *module = eng_module_of(host.engine,loaded_query,NULL);
        nmo_object_id_t ids[] = {loaded_owner,loaded_actor,loaded_near,loaded_far,loaded_group,loaded_script,loaded_query,loaded_move,
            eng_module_id(module,actor_parameter),eng_module_id(module,group_parameter),eng_module_id(module,position)}, copies[11] = {0};
        ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,ids,11,false,copies));
        ASSERT_EQ(copies[2],nmo_engine_group_object(host.engine,copies[4],1));
        ASSERT_EQ(copies[3],nmo_engine_group_object(host.engine,copies[4],2));
        const nmo_behavior_state_t *copied_state = nmo_object_get_state(eng_find_object(host.engine,copies[6]));
        nmo_object_id_t pin = eng_module_id(eng_module_of(host.engine,copies[6],NULL),nmo_behavior_ref_array_get_id(&copied_state->in_parameters,0));
        nmo_object_id_t actual_group = 0; read_value(&host,pin,&actual_group,sizeof(actual_group));
        ASSERT_EQ(copies[4],actual_group);
        ASSERT_EQ(copies[2],nearest_output(host.engine,copies[6]));
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
        ASSERT_EQ(copies[3],nearest_output(host.engine,copies[6]));
        ASSERT_POSITION(&host,copies[3],100,0,0); ASSERT_POSITION(&host,loaded_far,7,0,0);
        nmo_engine_destroy_object(host.engine,loaded_near);
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,loaded_script,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
        ASSERT_EQ(loaded_far,nearest_output(host.engine,loaded_query)); ASSERT_POSITION(&host,loaded_far,100,0,0);
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
        ASSERT_EQ(copies[2],nearest_output(host.engine,copies[6]));
        fixture_end(&host);
    }
    fixture_end(&source); remove(path);
}

TEST(engine_transform, loaded_children_keep_their_place_under_the_parent)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t parent = fixture_create(&f, NMO_CID_3DENTITY, "Parent");
    nmo_object_id_t child = fixture_create(&f, NMO_CID_3DENTITY, "Child");
    ASSERT_TRUE(place_entity(&f, parent, 10.0f, 0.0f, 0.0f, 0) != 0);
    ASSERT_TRUE(place_entity(&f, child, 12.0f, 1.0f, 0.0f, parent) != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);

    ASSERT_EQ(parent, nmo_engine_entity_parent(f.engine, child));
    ASSERT_EQ(0u, nmo_engine_entity_parent(f.engine, parent));
    float local[16];
    ASSERT_TRUE(nmo_engine_entity_local_matrix(f.engine, child, local));
    ASSERT_FLOAT_EQ(2.0f, local[12], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, local[13], 0.0001f);
    /* The host moves the parent: the child moves along, unless kept */
    float m[16];
    world_matrix(&f, parent, m);
    m[12] = 20.0f;
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, parent, m, false));
    ASSERT_POSITION(&f, child, 22.0f, 1.0f, 0.0f);
    m[12] = 0.0f;
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, parent, m, true));
    ASSERT_POSITION(&f, child, 22.0f, 1.0f, 0.0f);
    ASSERT_TRUE(nmo_engine_entity_local_matrix(f.engine, child, local));
    ASSERT_FLOAT_EQ(22.0f, local[12], 0.0001f);
    ASSERT_FALSE(nmo_engine_entity_world_matrix(f.engine, f.script_id, m));
    fixture_end(&f);
}

TEST(engine_transform, set_position_and_set_parent)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t parent = fixture_create(&f, NMO_CID_3DENTITY, "Parent");
    nmo_object_id_t child = fixture_create(&f, NMO_CID_3DENTITY, "Child");
    nmo_object_id_t loose = fixture_create(&f, NMO_CID_3DENTITY, "Loose");
    ASSERT_TRUE(place_entity(&f, parent, 0.0f, 0.0f, 0.0f, 0) != 0);
    ASSERT_TRUE(place_entity(&f, child, 1.0f, 0.0f, 0.0f, parent) != 0);
    ASSERT_TRUE(place_entity(&f, loose, 0.0f, 0.0f, 5.0f, 0) != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    /* Moved with its child, then without, then the loose one into the parent's space */
    nmo_object_id_t along = target_block(&f, GUID_SET_POSITION, "Along", parent, f.script_in);
    ASSERT_TRUE(along != 0);
    nmo_object_id_t alone = target_block(&f, GUID_SET_POSITION, "Alone", parent, BLOCK_OUT(&f, along, 0));
    ASSERT_TRUE(alone != 0);
    nmo_object_id_t adopt = target_block(&f, GUID_SET_PARENT, "Adopt", loose, BLOCK_OUT(&f, alone, 0));
    ASSERT_TRUE(adopt != 0);
    nmo_object_id_t inside = target_block(&f, GUID_SET_POSITION, "Inside", loose, BLOCK_OUT(&f, adopt, 0));
    ASSERT_TRUE(inside != 0);
    const float ten[3] = {10.0f, 0.0f, 0.0f}, twenty[3] = {20.0f, 0.0f, 0.0f};
    const float one_up[3] = {0.0f, 1.0f, 0.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, along, 0), ten, sizeof(ten));
    fixture_input_value(&f, BLOCK_PIN(&f, alone, 0), twenty, sizeof(twenty));
    fixture_input_int(&f, BLOCK_PIN(&f, alone, 2), 0); /* Hierarchy: no */
    fixture_input_int(&f, BLOCK_PIN(&f, adopt, 0), (int32_t)parent);
    fixture_input_value(&f, BLOCK_PIN(&f, inside, 0), one_up, sizeof(one_up));
    fixture_input_int(&f, BLOCK_PIN(&f, inside, 1), (int32_t)parent); /* Referential */
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_POSITION(&f, parent, 20.0f, 0.0f, 0.0f);
    ASSERT_POSITION(&f, child, 11.0f, 0.0f, 0.0f); /* followed to 10, then kept */
    /* Set Parent keeps the world position: the local one follows */
    ASSERT_EQ(parent, nmo_engine_entity_parent(f.engine, loose));
    ASSERT_POSITION(&f, loose, 20.0f, 1.0f, 0.0f);
    float local[16];
    ASSERT_TRUE(nmo_engine_entity_local_matrix(f.engine, loose, local));
    ASSERT_FLOAT_EQ(1.0f, local[13], 0.0001f);
    fixture_end(&f);
}

TEST(engine_transform, rotate_scale_and_orientation)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t turned = fixture_create(&f, NMO_CID_3DENTITY, "Turned");
    nmo_object_id_t scaled = fixture_create(&f, NMO_CID_3DENTITY, "Scaled");
    nmo_object_id_t oriented = fixture_create(&f, NMO_CID_3DENTITY, "Oriented");
    nmo_object_id_t looking = fixture_create(&f, NMO_CID_3DENTITY, "Looking");
    ASSERT_TRUE(place_entity(&f, turned, 1.0f, 2.0f, 3.0f, 0) != 0);
    ASSERT_TRUE(place_entity(&f, scaled, 0.0f, 0.0f, 0.0f, 0) != 0);
    ASSERT_TRUE(place_entity(&f, oriented, 0.0f, 0.0f, 0.0f, 0) != 0);
    ASSERT_TRUE(place_entity(&f, looking, 0.0f, 0.0f, 0.0f, 0) != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t rotate = target_block(&f, GUID_ROTATE, "Rotate", turned, f.script_in);
    ASSERT_TRUE(rotate != 0);
    nmo_object_id_t twice = target_block(&f, GUID_SCALE, "Twice", scaled, BLOCK_OUT(&f, rotate, 0));
    ASSERT_TRUE(twice != 0);
    nmo_object_id_t again = target_block(&f, GUID_SCALE, "Again", scaled, BLOCK_OUT(&f, twice, 0));
    ASSERT_TRUE(again != 0);
    nmo_object_id_t euler =
        target_block(&f, GUID_SET_EULER_ORIENTATION, "Euler", oriented, BLOCK_OUT(&f, again, 0));
    ASSERT_TRUE(euler != 0);
    nmo_object_id_t look = target_block(&f, GUID_LOOK_AT, "Look", looking, BLOCK_OUT(&f, euler, 0));
    ASSERT_TRUE(look != 0);
    const float axis[3] = {0.0f, 1.0f, 0.0f};
    const float quarter = PI_F / 2.0f;
    const float double_x[3] = {2.0f, 1.0f, 1.0f};
    const float yaw[3] = {0.0f, PI_F / 2.0f, 0.0f};
    const float target[3] = {-5.0f, 0.0f, 0.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, rotate, 0), axis, sizeof(axis));
    fixture_input_value(&f, BLOCK_PIN(&f, rotate, 1), &quarter, sizeof(quarter));
    fixture_input_value(&f, BLOCK_PIN(&f, twice, 0), double_x, sizeof(double_x));
    fixture_input_value(&f, BLOCK_PIN(&f, again, 0), double_x, sizeof(double_x));
    fixture_input_value(&f, BLOCK_PIN(&f, euler, 0), yaw, sizeof(yaw));
    fixture_input_value(&f, BLOCK_PIN(&f, look, 0), target, sizeof(target));
    fixture_input_value(&f, BLOCK_PIN(&f, look, 2), &(float){1.0f}, sizeof(float)); /* at once */
    fixture_commit(&f, NULL);
    /* Settings: Merge Angles Params for the euler block, not Time Based for Look At */
    const nmo_behavior_state_t *euler_state = (const nmo_behavior_state_t *)fixture_state(&f, euler);
    const nmo_behavior_state_t *look_state = (const nmo_behavior_state_t *)fixture_state(&f, look);
    ASSERT_TRUE(euler_state != NULL && look_state != NULL);
    const int32_t yes = 1, no = 0, z_axis = 5;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(
                          f.engine, nmo_behavior_ref_array_get_id(&euler_state->local_parameters, 0),
                          &yes, sizeof(yes)));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(
                          f.engine, nmo_behavior_ref_array_get_id(&look_state->local_parameters, 0),
                          &no, sizeof(no)));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(
                          f.engine, nmo_behavior_ref_array_get_id(&look_state->local_parameters, 1),
                          &z_axis, sizeof(z_axis)));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    float m[16];
    /* A quarter turn about Y (Vx3DMatrixFromRotation) keeps the position: X goes to Z, Z to -X */
    world_matrix(&f, turned, m);
    ASSERT_FLOAT_EQ(1.0f, m[2], 0.0001f);
    ASSERT_FLOAT_EQ(-1.0f, m[8], 0.0001f);
    ASSERT_POSITION(&f, turned, 1.0f, 2.0f, 3.0f);
    /* Twice times two along X */
    world_matrix(&f, scaled, m);
    ASSERT_FLOAT_EQ(4.0f, m[0], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, m[5], 0.0001f);
    /* The angles' axes: Z turned to X */
    world_matrix(&f, oriented, m);
    ASSERT_FLOAT_EQ(1.0f, m[8], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, m[5], 0.0001f);
    /* Looking toward -X: the Z axis points there */
    world_matrix(&f, looking, m);
    ASSERT_FLOAT_EQ(-1.0f, m[8], 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, m[9], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, m[5], 0.0001f);
    fixture_end(&f);
}

/* The `pos`th local parameter (or setting) of a block, set in the engine to `value` */
static void set_local_int(engine_fixture_t *f, nmo_object_id_t block, size_t pos, int32_t value)
{
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(f, block);
    ASSERT_TRUE(state != NULL && pos < state->local_parameters.count);
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(
                          f->engine, nmo_behavior_ref_array_get_id(&state->local_parameters, pos),
                          &value, sizeof(value)));
}

static void move_to(engine_fixture_t *f, nmo_object_id_t id, float x)
{
    float m[16];
    world_matrix(f, id, m);
    m[12] = x;
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f->engine, id, m, false));
}

TEST(engine_transform, nearest_in_group_and_proximity)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t entities[3];
    const float xs[3] = {1.0f, 5.0f, 9.0f};
    for (int i = 0; i < 3; i++) {
        entities[i] = fixture_create(&f, NMO_CID_3DENTITY, "Member");
        ASSERT_TRUE(place_entity(&f, entities[i], xs[i], 0.0f, 0.0f, 0) != 0);
    }
    nmo_object_id_t group = fixture_create(&f, NMO_CID_GROUP, "Group");
    nmo_group_state_t *state = (nmo_group_state_t *)fixture_state(&f, group);
    ASSERT_NOT_NULL(state);
    nmo_ref_t *members = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(&state->object_ids, 3u, (void **)&members));
    for (int i = 0; i < 3; i++) {
        members[i] = nmo_ref_from_id(entities[i]);
    }
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t nearest = fixture_block(&f, GUID_NEAREST_IN_GROUP, "Nearest");
    nmo_object_id_t proximity = fixture_block(&f, GUID_SCALEABLE_PROXIMITY, "Near");
    ASSERT_TRUE(nearest != 0 && proximity != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, nearest, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, proximity, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, nearest, 0), (int32_t)group);
    const float point[3] = {6.0f, 0.0f, 0.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, nearest, 1), point, sizeof(point));
    /* Within 3 of each other, checked every frame */
    const float three = 3.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, proximity, 0), &three, sizeof(three));
    fixture_input_int(&f, BLOCK_PIN(&f, proximity, 1), (int32_t)entities[0]);
    fixture_input_int(&f, BLOCK_PIN(&f, proximity, 2), (int32_t)entities[1]);
    fixture_input_int(&f, BLOCK_PIN(&f, proximity, 6), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, proximity, 7), 1);
    /* In Range, Out Range, Enter Range, Exit Range */
    nmo_object_id_t outputs[4];
    for (int i = 0; i < 4; i++) {
        outputs[i] = fixture_block(&f, GUID_NOP, "Output");
        ASSERT_TRUE(outputs[i] != 0);
        fixture_link(&f, BLOCK_OUT(&f, proximity, (size_t)i), BLOCK_IN(&f, outputs[i], 0), 0);
    }
    fixture_commit(&f, NULL);
    set_local_int(&f, proximity, 2, 15); /* every output */
    set_local_int(&f, proximity, 3, 7);  /* X, Y and Z */
    set_local_int(&f, proximity, 4, 1);  /* squared */

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    uint32_t found = 0;
    float distance = 0.0f;
    read_value(&f, BLOCK_POUT(&f, nearest, 0), &found, sizeof(found));
    read_value(&f, BLOCK_POUT(&f, nearest, 1), &distance, sizeof(distance));
    ASSERT_EQ(entities[1], found);
    ASSERT_FLOAT_EQ(1.0f, distance, 0.0001f);
    /* 4 apart: the first check exits (In starts neither in nor out), then stays out */
    ASSERT_EQ(1u, runs(&f, outputs[3]));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, outputs[1]));
    move_to(&f, entities[1], 2.0f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, outputs[2]));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, outputs[0]));
    float squared = 0.0f;
    read_value(&f, BLOCK_POUT(&f, proximity, 0), &squared, sizeof(squared));
    ASSERT_FLOAT_EQ(1.0f, squared, 0.0001f);
    fixture_end(&f);
}

TEST(engine_transform, set_dynamic_position_follows_the_object)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t follower = fixture_create(&f, NMO_CID_3DENTITY, "Follower");
    nmo_object_id_t leader = fixture_create(&f, NMO_CID_3DENTITY, "Leader");
    ASSERT_TRUE(place_entity(&f, follower, 0.0f, 0.0f, 0.0f, 0) != 0);
    ASSERT_TRUE(place_entity(&f, leader, 10.0f, 0.0f, 0.0f, 0) != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t follow =
        target_block(&f, GUID_SET_DYNAMIC_POSITION, "Follow", follower, f.script_in);
    ASSERT_TRUE(follow != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, follow, 0), (int32_t)leader);
    const float half = 0.5f;
    for (int axis = 1; axis <= 3; axis++) {
        fixture_input_value(&f, BLOCK_PIN(&f, follow, (size_t)axis), &half, sizeof(half));
    }
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f)); /* On: where it starts */
    ASSERT_POSITION(&f, follower, 0.0f, 0.0f, 0.0f);
    /* Then half the way each second (the force times the seconds) */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f));
    ASSERT_POSITION(&f, follower, 5.0f, 0.0f, 0.0f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f));
    ASSERT_POSITION(&f, follower, 7.5f, 0.0f, 0.0f);
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, follow));
    fixture_end(&f);
}

/* A TT SpeedOMeter on `target` run each frame; with `relative`, its speed range pins and output */
static nmo_object_id_t speedometer(engine_fixture_t *f, nmo_object_id_t target, const char *name,
                                   bool relative, float min, float max)
{
    nmo_object_id_t block = target_block(f, GUID_SPEEDOMETER, name, target, f->script_in);
    if (block == 0) {
        return 0;
    }
    fixture_link(f, BLOCK_OUT(f, block, 0), BLOCK_IN(f, block, 0), 1);
    if (relative) {
        nmo_object_id_t pins[2] = {0, 0}, pout = 0;
        if (nmo_script_edit_add_parameter(f->tx, block, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_FLOAT,
                                          "Minimum Speed", &pins[0]) != NMO_OK ||
            nmo_script_edit_add_parameter(f->tx, block, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_FLOAT,
                                          "Maximum Speed", &pins[1]) != NMO_OK ||
            nmo_script_edit_add_parameter(f->tx, block, NMO_SCRIPT_EDIT_PARAM_OUT, CKPGUID_FLOAT,
                                          "Relative Speed", &pout) != NMO_OK) {
            return 0;
        }
        fixture_input_value(f, pins[0], &min, sizeof(min));
        fixture_input_value(f, pins[1], &max, sizeof(max));
    }
    return block;
}

static float output_float(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    float value = -1.0f;
    read_value(f, BLOCK_POUT(f, block, pos), &value, sizeof(value));
    return value;
}

TEST(engine_transform, speedometer_measures_the_target_speed)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t ball = fixture_create(&f, NMO_CID_3DENTITY, "Ball");
    ASSERT_TRUE(place_entity(&f, ball, 0.0f, 0.0f, 0.0f, 0) != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t absolute = speedometer(&f, ball, "Absolute", false, 0.0f, 0.0f);
    nmo_object_id_t relative = speedometer(&f, ball, "Relative", true, 0.0f, 10.0f);
    nmo_object_id_t flat = speedometer(&f, ball, "No Range", true, 0.0f, 0.0f);
    ASSERT_TRUE(absolute != 0 && relative != 0 && flat != 0);
    fixture_commit(&f, NULL);
    set_local_int(&f, relative, 1, 1);
    set_local_int(&f, relative, 2, 2); /* the speed over two frames' time */
    set_local_int(&f, flat, 1, 1);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f));
    ASSERT_FLOAT_EQ(0.0f, output_float(&f, absolute, 0), 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, output_float(&f, relative, 1), 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, output_float(&f, flat, 1), 0.0001f); /* 0 / 0: NaN reads as 1 */
    move_to(&f, ball, 5.0f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f));
    ASSERT_FLOAT_EQ(5.0f, output_float(&f, absolute, 0), 0.0001f);
    ASSERT_FLOAT_EQ(2.5f, output_float(&f, relative, 0), 0.0001f);
    ASSERT_FLOAT_EQ(0.25f, output_float(&f, relative, 1), 0.0001f);
    ASSERT_EQ(2u, runs(&f, absolute));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f));
    ASSERT_FLOAT_EQ(0.0f, output_float(&f, absolute, 0), 0.0001f); /* not moved since */
    fixture_end(&f);
}

/* A child entity of `parent` at `x, y, z` with a script of its own */
static nmo_object_id_t scripted_child(engine_fixture_t *f, const char *name, nmo_object_id_t parent,
                                      float x, float y, float z)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_3DENTITY, name);
    if (id == 0 || place_entity(f, id, x, y, z, parent) == 0) {
        return 0;
    }
    nmo_object_id_t owner = f->owner_id, input = 0;
    f->owner_id = id;
    nmo_object_id_t script = fixture_add_script(f, name, &input);
    f->owner_id = owner;
    return script != 0 ? id : 0;
}

TEST(engine_transform, tt_extra_circles_flies_away_and_homes_in)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t extra = fixture_create(&f, NMO_CID_3DENTITY, "P_Extra_Point_01");
    nmo_object_id_t ball = fixture_create(&f, NMO_CID_3DENTITY, "Ball");
    ASSERT_TRUE(place_entity(&f, extra, 0.0f, 0.0f, 0.0f, 0) != 0);
    ASSERT_TRUE(place_entity(&f, ball, 100.0f, 0.0f, 0.0f, 0) != 0);
    /* Its parts: named from the 14th character, the small balls from the 18th */
    nmo_object_id_t floor = fixture_create(&f, NMO_CID_3DENTITY, "P_Extra_Point_Floor");
    ASSERT_TRUE(floor != 0 && place_entity(&f, floor, 0.0f, 0.0f, 0.0f, extra) != 0);
    nmo_object_id_t center = fixture_create(&f, NMO_CID_3DENTITY, "P_Extra_Point_Ball0");
    ASSERT_TRUE(center != 0 && place_entity(&f, center, 0.0f, 0.0f, 0.0f, extra) != 0);
    ASSERT_TRUE(scripted_child(&f, "Center Frame", center, 0.0f, 0.0f, 0.0f) != 0);
    nmo_object_id_t balls[2] = {fixture_create(&f, NMO_CID_3DENTITY, "P_Extra_Point_Ball1"),
                                fixture_create(&f, NMO_CID_3DENTITY, "P_Extra_Point_Ball2")};
    ASSERT_TRUE(balls[0] != 0 && place_entity(&f, balls[0], 2.0f, 0.0f, 0.0f, extra) != 0);
    ASSERT_TRUE(balls[1] != 0 && place_entity(&f, balls[1], 0.0f, 2.0f, 0.0f, extra) != 0);
    ASSERT_TRUE(scripted_child(&f, "Frame 1", balls[0], 2.0f, 0.0f, 0.0f) != 0);
    ASSERT_TRUE(scripted_child(&f, "Frame 2", balls[1], 0.0f, 2.0f, 0.0f) != 0);
    /* Where the hits show */
    nmo_object_id_t hits[2] = {fixture_create(&f, NMO_CID_3DENTITY, "Hit 1"),
                               fixture_create(&f, NMO_CID_3DENTITY, "Hit 2")};
    ASSERT_TRUE(place_entity(&f, hits[0], 50.0f, 0.0f, 0.0f, 0) != 0);
    ASSERT_TRUE(place_entity(&f, hits[1], 60.0f, 0.0f, 0.0f, 0) != 0);
    nmo_object_id_t group = fixture_create(&f, NMO_CID_GROUP, "Hit Frames");
    nmo_group_state_t *members = (nmo_group_state_t *)fixture_state(&f, group);
    ASSERT_NOT_NULL(members);
    nmo_ref_t *refs = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(&members->object_ids, 2u, (void **)&refs));
    refs[0] = nmo_ref_from_id(hits[0]);
    refs[1] = nmo_ref_from_id(hits[1]);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t block = target_block(&f, GUID_TT_EXTRA, "Extra", extra, f.script_in);
    ASSERT_TRUE(block != 0);
    /* Activated, Hit, Ready */
    nmo_object_id_t outputs[3];
    for (int i = 0; i < 3; i++) {
        outputs[i] = fixture_block(&f, GUID_NOP, "Output");
        ASSERT_TRUE(outputs[i] != 0);
        fixture_link(&f, BLOCK_OUT(&f, block, (size_t)i), BLOCK_IN(&f, outputs[i], 0), 0);
    }
    fixture_input_int(&f, BLOCK_PIN(&f, block, 0), (int32_t)ball);
    fixture_input_int(&f, BLOCK_PIN(&f, block, 1), 2);
    const float quarter = PI_F / 2.0f, collision = 1000.0f, away_time = 1500.0f, still = 0.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, block, 3), &collision, sizeof(collision));
    fixture_input_value(&f, BLOCK_PIN(&f, block, 5), &still, sizeof(still)); /* no AwayForce */
    fixture_input_value(&f, BLOCK_PIN(&f, block, 4), &quarter, sizeof(quarter));
    fixture_input_value(&f, BLOCK_PIN(&f, block, 10), &away_time, sizeof(away_time));
    fixture_input_int(&f, BLOCK_PIN(&f, block, 11), (int32_t)group);
    fixture_commit(&f, NULL);

    /* Idle (the frame's time is 1 s): a quarter turn each about its axis, 2 from the center */
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f));
    ASSERT_TRUE(nmo_engine_object_is_visible(f.engine, center));
    ASSERT_TRUE(nmo_engine_object_is_visible(f.engine, floor));
    ASSERT_TRUE(nmo_engine_object_is_visible(f.engine, balls[0]));
    float m[16];
    world_matrix(&f, balls[0], m);
    ASSERT_FLOAT_EQ(0.0f, m[12], 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, fabsf(m[14]), 0.0001f); /* about Y */
    world_matrix(&f, balls[1], m);
    ASSERT_FLOAT_EQ(2.0f, fabsf(m[12]), 0.0001f); /* about Z */
    ASSERT_FLOAT_EQ(0.0f, m[13], 0.0001f);
    ASSERT_EQ(0u, runs(&f, outputs[0]));

    /* The ball comes near: Activated, then FlyAwayTime away, then they home in */
    move_to(&f, ball, 0.0f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f));
    ASSERT_EQ(1u, runs(&f, outputs[0]));
    ASSERT_FALSE(nmo_engine_object_is_visible(f.engine, center));
    ASSERT_FALSE(nmo_engine_object_is_visible(f.engine, floor));
    int32_t current = 0;
    read_value(&f, BLOCK_POUT(&f, block, 0), &current, sizeof(current));
    ASSERT_EQ(1, current);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f)); /* away, 500 ms left */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f)); /* time up */
    ASSERT_EQ(0u, runs(&f, outputs[1]));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f)); /* both within the collision */
    ASSERT_EQ(1u, runs(&f, outputs[1]));
    ASSERT_FALSE(nmo_engine_object_is_visible(f.engine, balls[0]));
    ASSERT_FALSE(nmo_engine_object_is_visible(f.engine, balls[1]));
    read_value(&f, BLOCK_POUT(&f, block, 0), &current, sizeof(current));
    ASSERT_EQ(2, current);
    float hit[16];
    world_matrix(&f, balls[0], m);
    world_matrix(&f, hits[0], hit);
    ASSERT_FLOAT_EQ(m[12], hit[12], 0.0001f);
    ASSERT_FLOAT_EQ(m[13], hit[13], 0.0001f);
    ASSERT_FLOAT_EQ(m[14], hit[14], 0.0001f);
    ASSERT_EQ(0u, runs(&f, outputs[2]));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f));
    ASSERT_EQ(1u, runs(&f, outputs[2]));
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, block));

    /* Its script's deactivation takes it down; restarted, it shows and is collected again */
    ASSERT_EQ(NMO_OK, nmo_engine_activate_script(f.engine, f.script_id, false, false));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_activate_script(f.engine, f.script_id, true, true));
    move_to(&f, ball, 100.0f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f));
    ASSERT_TRUE(nmo_engine_object_is_visible(f.engine, center));
    ASSERT_TRUE(nmo_engine_object_is_visible(f.engine, balls[0]));
    ASSERT_TRUE(nmo_engine_object_is_visible(f.engine, balls[1]));
    move_to(&f, ball, 0.0f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f));
    ASSERT_EQ(2u, runs(&f, outputs[0]));
    fixture_end(&f);
}

/*
 * A 3D object at the origin whose mesh is a 10 x 10 floor on y = 0: faces
 * (-5,-5) (-5,5) (5,5) and (-5,-5) (5,5) (5,-5) in x, z, facing up (the
 * face normals are left for the engine to build, as files leave them).
 */
static nmo_object_id_t floor_object(engine_fixture_t *f)
{
    nmo_object_id_t object = fixture_create(f, NMO_CID_3DOBJECT, "Floor");
    nmo_object_id_t mesh = fixture_create(f, NMO_CID_MESH, "Floor Mesh");
    nmo_3dentity_state_t *entity = (nmo_3dentity_state_t *)fixture_state(f, object);
    nmo_mesh_state_t *state = (nmo_mesh_state_t *)fixture_state(f, mesh);
    if (place_entity(f, object, 0.0f, 0.0f, 0.0f, 0) == 0 || entity == NULL || state == NULL) {
        return 0;
    }
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    state->vertices = (nmo_vertex_t *)nmo_arena_alloc(arena, 4u * sizeof(nmo_vertex_t), _Alignof(nmo_vertex_t));
    state->faces = (nmo_face_t *)nmo_arena_alloc(arena, 2u * sizeof(nmo_face_t), _Alignof(nmo_face_t));
    state->face_vertex_indices = (uint16_t *)nmo_arena_alloc(arena, 6u * sizeof(uint16_t), _Alignof(uint16_t));
    if (state->vertices == NULL || state->faces == NULL || state->face_vertex_indices == NULL) {
        return 0;
    }
    const float corners[4][2] = {{-5.0f, -5.0f}, {-5.0f, 5.0f}, {5.0f, 5.0f}, {5.0f, -5.0f}};
    for (int i = 0; i < 4; i++) {
        memset(&state->vertices[i], 0, sizeof(state->vertices[i]));
        state->vertices[i].position = (nmo_vector_t){corners[i][0], 0.0f, corners[i][1]};
        state->vertices[i].normal = (nmo_vector_t){0.0f, 1.0f, 0.0f};
    }
    memset(state->faces, 0, 2u * sizeof(nmo_face_t));
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    memcpy(state->face_vertex_indices, indices, sizeof(indices));
    state->vertex_count = 4;
    state->face_count = 2;
    state->bary_center = (nmo_vector_t){0.0f, 0.0f, 0.0f};
    state->radius = sqrtf(50.0f);
    entity->current_mesh = nmo_ref_from_id(mesh);
    return object;
}

/* A Ray Intersection of the owner from `origin` along `dir` within `depth`, run from the script's start */
static nmo_object_id_t ray_block(engine_fixture_t *f, const char *name, const float origin[3],
                                 const float dir[3], float depth)
{
    nmo_object_id_t block = target_block(f, GUID_RAY_INTERSECTION, name, f->owner_id, f->script_in);
    if (block == 0) {
        return 0;
    }
    fixture_input_value(f, BLOCK_PIN(f, block, 0), origin, 3u * sizeof(float));
    fixture_input_value(f, BLOCK_PIN(f, block, 1), dir, 3u * sizeof(float));
    fixture_input_value(f, BLOCK_PIN(f, block, 3), &depth, sizeof(depth));
    return block;
}

TEST(engine_transform, ray_intersection_finds_the_nearest_face)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t floor = floor_object(&f);
    ASSERT_TRUE(floor != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    const float above[3] = {2.0f, 2.0f, 1.0f}, below[3] = {2.0f, -2.0f, 1.0f};
    const float down[3] = {0.0f, -1.0f, 0.0f}, up[3] = {0.0f, 1.0f, 0.0f};
    nmo_object_id_t hit = ray_block(&f, "Hit", above, down, 3.0f);
    nmo_object_id_t short_ray = ray_block(&f, "Short", above, down, 1.5f);
    nmo_object_id_t behind = ray_block(&f, "Behind", below, up, 3.0f); /* the face's back */
    ASSERT_TRUE(hit != 0 && short_ray != 0 && behind != 0);
    /* True, False of each */
    nmo_object_id_t outputs[3][2];
    nmo_object_id_t blocks[3] = {hit, short_ray, behind};
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < 2; k++) {
            outputs[i][k] = fixture_block(&f, GUID_NOP, "Output");
            ASSERT_TRUE(outputs[i][k] != 0);
            fixture_link(&f, BLOCK_OUT(&f, blocks[i], (size_t)k), BLOCK_IN(&f, outputs[i][k], 0), 0);
        }
    }
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, outputs[0][0]));
    ASSERT_EQ(1u, runs(&f, outputs[1][1]));
    ASSERT_EQ(1u, runs(&f, outputs[2][1]));
    uint32_t object = 0;
    int32_t face = -1, vertex = -1;
    float point[3], normal[3], distance = 0.0f;
    read_value(&f, BLOCK_POUT(&f, hit, 0), &object, sizeof(object));
    read_value(&f, BLOCK_POUT(&f, hit, 1), &face, sizeof(face));
    read_value(&f, BLOCK_POUT(&f, hit, 2), &vertex, sizeof(vertex));
    read_value(&f, BLOCK_POUT(&f, hit, 3), point, sizeof(point));
    read_value(&f, BLOCK_POUT(&f, hit, 4), normal, sizeof(normal));
    read_value(&f, BLOCK_POUT(&f, hit, 5), &distance, sizeof(distance));
    ASSERT_EQ(floor, object);
    ASSERT_EQ(1, face);   /* (2, 1) is in the second face */
    ASSERT_EQ(2, vertex); /* (5, 5): the face's corner nearest the hit */
    ASSERT_FLOAT_EQ(2.0f, point[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, point[1], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, point[2], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, normal[1], 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, distance, 0.0001f);
    fixture_end(&f);
}

/* A 3D object at (x, 0, 0), turned `degrees` about Y, whose mesh is a cube of half size 0.5 */
static nmo_object_id_t cube_object(engine_fixture_t *f, const char *name, float x, float degrees)
{
    nmo_object_id_t object = fixture_create(f, NMO_CID_3DOBJECT, name);
    nmo_object_id_t mesh = fixture_create(f, NMO_CID_MESH, name);
    nmo_3dentity_state_t *entity = (nmo_3dentity_state_t *)fixture_state(f, object);
    nmo_mesh_state_t *state = (nmo_mesh_state_t *)fixture_state(f, mesh);
    if (place_entity(f, object, x, 0.0f, 0.0f, 0) == 0 || entity == NULL || state == NULL) {
        return 0;
    }
    float angle = degrees * PI_F / 180.0f;
    entity->world_matrix[0] = cosf(angle);
    entity->world_matrix[2] = -sinf(angle);
    entity->world_matrix[8] = sinf(angle);
    entity->world_matrix[10] = cosf(angle);
    state->vertices = (nmo_vertex_t *)nmo_arena_alloc(nmo_session_get_arena(f->session),
                                                      8u * sizeof(nmo_vertex_t), _Alignof(nmo_vertex_t));
    if (state->vertices == NULL) {
        return 0;
    }
    memset(state->vertices, 0, 8u * sizeof(nmo_vertex_t));
    for (int i = 0; i < 8; i++) {
        state->vertices[i].position =
            (nmo_vector_t){(i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f};
    }
    state->vertex_count = 8;
    entity->current_mesh = nmo_ref_from_id(mesh);
    return object;
}

TEST(engine_transform, box_box_intersection_tests_oriented_boxes)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t a = cube_object(&f, "A", 0.0f, 0.0f);
    nmo_object_id_t apart = cube_object(&f, "Apart", 1.1f, 0.0f);   /* 0.1 between them */
    nmo_object_id_t turned = cube_object(&f, "Turned", 1.1f, 45.0f); /* its corner reaches in */
    ASSERT_TRUE(a != 0 && apart != 0 && turned != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t others[2] = {apart, turned};
    nmo_object_id_t outputs[2][2];
    for (int i = 0; i < 2; i++) {
        nmo_object_id_t test = fixture_block(&f, GUID_BOX_BOX_INTERSECTION, "Test");
        ASSERT_TRUE(test != 0);
        fixture_link(&f, f.script_in, BLOCK_IN(&f, test, 0), 0);
        fixture_input_int(&f, BLOCK_PIN(&f, test, 0), (int32_t)a);
        fixture_input_int(&f, BLOCK_PIN(&f, test, 1), (int32_t)others[i]);
        for (int k = 0; k < 2; k++) {
            outputs[i][k] = fixture_block(&f, GUID_NOP, "Output");
            ASSERT_TRUE(outputs[i][k] != 0);
            fixture_link(&f, BLOCK_OUT(&f, test, (size_t)k), BLOCK_IN(&f, outputs[i][k], 0), 0);
        }
    }
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, outputs[0][1])); /* False */
    ASSERT_EQ(1u, runs(&f, outputs[1][0])); /* True */
    fixture_end(&f);
}

/* A TT ProximityVolumeControl on `sound` between `a` and `b`, on at frame 0 and off at frame 2 */
static nmo_object_id_t proximity_volume(engine_fixture_t *f, const char *name, nmo_object_id_t a,
                                        nmo_object_id_t b, nmo_object_id_t sound,
                                        nmo_object_id_t outs[2])
{
    nmo_object_id_t block = fixture_block(f, GUID_PROXIMITY_VOLUME, name);
    outs[0] = fixture_block(f, GUID_NOP, "On");
    outs[1] = fixture_block(f, GUID_NOP, "Off");
    if (block == 0 || outs[0] == 0 || outs[1] == 0) {
        return 0;
    }
    fixture_link(f, f->script_in, BLOCK_IN(f, block, 0), 0);
    fixture_link(f, f->script_in, BLOCK_IN(f, block, 1), 2);
    fixture_link(f, BLOCK_OUT(f, block, 0), BLOCK_IN(f, outs[0], 0), 0);
    fixture_link(f, BLOCK_OUT(f, block, 1), BLOCK_IN(f, outs[1], 0), 0);
    fixture_input_int(f, BLOCK_PIN(f, block, 0), (int32_t)a);
    fixture_input_int(f, BLOCK_PIN(f, block, 1), (int32_t)b);
    fixture_input_int(f, BLOCK_PIN(f, block, 4), (int32_t)sound);
    return block;
}

TEST(engine_transform, proximity_volume_control_runs_until_off)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t a = fixture_create(&f, NMO_CID_3DENTITY, "A");
    nmo_object_id_t b = fixture_create(&f, NMO_CID_3DENTITY, "B");
    nmo_object_id_t sound = fixture_create(&f, NMO_CID_WAVESOUND, "Hum");
    ASSERT_TRUE(place_entity(&f, a, 0.0f, 0.0f, 0.0f, 0) != 0 &&
                place_entity(&f, b, 5.0f, 0.0f, 0.0f, 0) != 0 && sound != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t outs[2], silent_outs[2];
    nmo_object_id_t control = proximity_volume(&f, "Control", a, b, sound, outs);
    nmo_object_id_t silent = proximity_volume(&f, "No Sound", a, b, 0, silent_outs);
    ASSERT_TRUE(control != 0 && silent != 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    /* On out, then it runs each frame */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, outs[0]));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, control));
    ASSERT_EQ(2u, runs(&f, control));
    /* Off: Off out, done */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, outs[1]));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, control));
    /* Without a sound: On out, then a parameter error ends it; Off errors the same way, before
     * Off out */
    ASSERT_EQ(1u, runs(&f, silent_outs[0]));
    ASSERT_EQ(2u, runs(&f, silent));
    ASSERT_EQ(0u, runs(&f, silent_outs[1]));
    fixture_end(&f);
}

TEST(engine_transform, texture_sine_runs_its_clock_and_checks_the_channel)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t mesh = fixture_create(&f, NMO_CID_MESH, "Water");
    ASSERT_TRUE(mesh != 0);
    nmo_mesh_state_t *mesh_state = (nmo_mesh_state_t *)fixture_state(&f, mesh);
    ASSERT_NOT_NULL(mesh_state);
    mesh_state->vertices = (nmo_vertex_t *)nmo_arena_alloc(nmo_session_get_arena(f.session),
                                                           2u * sizeof(nmo_vertex_t),
                                                           _Alignof(nmo_vertex_t));
    ASSERT_NOT_NULL(mesh_state->vertices);
    memset(mesh_state->vertices, 0, 2u * sizeof(nmo_vertex_t));
    mesh_state->vertex_count = 2;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    /* Base UVs (channel -1) every frame; channel 0, which the mesh lacks, once */
    nmo_object_id_t sine = target_block(&f, GUID_TEXTURE_SINE, "Sine", mesh, f.script_in);
    nmo_object_id_t missing = target_block(&f, GUID_TEXTURE_SINE, "Missing", mesh, f.script_in);
    nmo_object_id_t again = fixture_block(&f, GUID_NOP, "Again");
    nmo_object_id_t after_missing = fixture_block(&f, GUID_NOP, "After Missing");
    ASSERT_TRUE(sine != 0 && missing != 0 && again != 0 && after_missing != 0);
    fixture_link(&f, BLOCK_OUT(&f, sine, 0), BLOCK_IN(&f, sine, 0), 1);
    fixture_link(&f, BLOCK_OUT(&f, missing, 0), BLOCK_IN(&f, after_missing, 0), 0);
    const float velocity = 2.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, sine, 2), &velocity, sizeof(velocity));
    fixture_input_int(&f, BLOCK_PIN(&f, sine, 3), -1);
    fixture_input_int(&f, BLOCK_PIN(&f, missing, 3), 0);
    const float amplitude = 0.1f;
    fixture_input_value(&f, BLOCK_PIN(&f, sine, 0), &amplitude, sizeof(amplitude));
    fixture_input_value(&f, BLOCK_PIN(&f, sine, 1), &amplitude, sizeof(amplitude));
    fixture_commit(&f, NULL);
    /* The UVs saved when the block was attached (local 0): (0, 0) and (1, 1) */
    const nmo_behavior_state_t *sine_state = (const nmo_behavior_state_t *)fixture_state(&f, sine);
    ASSERT_TRUE(sine_state != NULL && sine_state->local_parameters.count > 1);
    const float saved[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(
                          f.engine, nmo_behavior_ref_array_get_id(&sine_state->local_parameters, 0),
                          saved, sizeof(saved)));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    /* 1 s frames at velocity 2: the clock wraps once time * velocity passes 2 pi */
    const float expected[4] = {1.0f, 2.0f, 3.0f, 4.0f - 3.14159265f};
    for (int frame = 0; frame < 4; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f));
        if (frame == 0) {
            /* At time 0, each saved UV swayed toward 0.5 by the wave at its place: -2, then 0 */
            nmo_engine_mesh_t desc;
            ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &desc));
            ASSERT_FLOAT_EQ(cosf(-2.0f) * 0.5f * 0.1f, desc.vertices[0].uv.x, 0.0001f);
            ASSERT_FLOAT_EQ(sinf(-2.0f) * 0.5f * 0.1f, desc.vertices[0].uv.y, 0.0001f);
            ASSERT_FLOAT_EQ(0.95f, desc.vertices[1].uv.x, 0.0001f);
            ASSERT_FLOAT_EQ(1.0f, desc.vertices[1].uv.y, 0.0001f);
        }
        float time = -1.0f;
        const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(&f, sine);
        ASSERT_TRUE(state != NULL && state->local_parameters.count > 1);
        read_value(&f, nmo_behavior_ref_array_get_id(&state->local_parameters, 1), &time, sizeof(time));
        ASSERT_FLOAT_EQ(expected[frame], time, 0.0001f);
    }
    ASSERT_EQ(4u, runs(&f, sine));
    /* Out even for a missing channel */
    ASSERT_EQ(1u, runs(&f, after_missing));
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_transform, loaded_children_keep_their_place_under_the_parent);
REGISTER_TEST(engine_transform, set_position_and_set_parent);
REGISTER_TEST(engine_transform, rotate_scale_and_orientation);
REGISTER_TEST(engine_transform, nearest_in_group_and_proximity);
REGISTER_TEST(engine_transform, character_nearest_uses_world_distance_group_order_and_live_members);
REGISTER_TEST(engine_transform, character_nearest_keeps_native_range_and_clears_absent_or_deleted_groups);
REGISTER_TEST(engine_transform, character_nearest_reports_missing_owners_without_replacing_the_last_result);
REGISTER_TEST(engine_transform, loaded_character_nearest_outputs_feed_transforms_and_remap_on_copy);
REGISTER_TEST(engine_transform, set_dynamic_position_follows_the_object);
REGISTER_TEST(engine_transform, speedometer_measures_the_target_speed);
REGISTER_TEST(engine_transform, tt_extra_circles_flies_away_and_homes_in);
REGISTER_TEST(engine_transform, ray_intersection_finds_the_nearest_face);
REGISTER_TEST(engine_transform, box_box_intersection_tests_oriented_boxes);
REGISTER_TEST(engine_transform, proximity_volume_control_runs_until_off);
REGISTER_TEST(engine_transform, texture_sine_runs_its_clock_and_checks_the_channel);
TEST_MAIN_END()
