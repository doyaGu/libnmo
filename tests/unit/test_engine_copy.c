#include "engine_fixture.h"

#include "document/nmo_document_save.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_ref.h"

#include <math.h>
#include <stdio.h>

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_OBJECT_COPY NMO_GUID(0x3f6b0ac7, 0x47d20f78)
#define GUID_OBJECT_DELETE NMO_GUID(0x74120ded, 0x76524673)
#define GUID_OBJECT_CREATE NMO_GUID(0x271538e6, 0x2fae49ac)
#define GUID_DELETE_DYNAMIC_OBJECTS NMO_GUID(0x0cba3195, 0x53440e4c)
#define GUID_RESTORE_IC NMO_GUID(0x766e4e44, 0x4fac6d52)
#define GUID_TT_RESTORE_IC NMO_GUID(0x7e0f2f58, 0x232d3109)
#define GUID_TT_SAVE_IC NMO_GUID(0x30362f34, 0x1935316f)

#define DEPENDENCIES_CHUNK_CUSTOM 0x12248768u

/* The files the engine loads, from the scratch directory */
static const char *resolve_scratch(void *user_data, const char *name)
{
    static char path[512];
    (void)user_data;
    int length = snprintf(path, sizeof(path), "%s/%s", NMO_TEST_SCRATCH_DIR, name);
    return length > 0 && (size_t)length < sizeof(path) ? path : NULL;
}

/* An entity saved at `x` on the X axis with unit axes, under `parent` (0 for none) */
static void place_entity(engine_fixture_t *f, nmo_object_id_t id, float x, nmo_object_id_t parent)
{
    nmo_3dentity_state_t *state = (nmo_3dentity_state_t *)fixture_state(f, id);
    ASSERT_NOT_NULL(state);
    memset(state->world_matrix, 0, sizeof(state->world_matrix));
    state->world_matrix[0] = state->world_matrix[5] = state->world_matrix[10] = 1.0f;
    state->world_matrix[15] = 1.0f;
    state->world_matrix[12] = x;
    if (parent != 0) {
        state->parent = nmo_ref_from_id(parent);
    }
}

/*
 * Save "template.nmo": an entity "Template" at x 3, saved to start active
 * and with an initial value, with a script "Template Script" (one block) saved to start active; its
 * child "Child" at x 4; and a group "Members" holding the template.
 */
static void save_template_file(void)
{
    engine_fixture_t g;
    fixture_begin_document(&g);
    nmo_object_t *owner = nmo_object_repository_find_by_id(g.repo, g.owner_id);
    ASSERT_NOT_NULL(owner);
    ASSERT_EQ(NMO_OK, nmo_object_set_name(owner, "Template"));
    nmo_object_id_t child = fixture_create(&g, NMO_CID_3DENTITY, "Child");
    nmo_object_id_t group = fixture_create(&g, NMO_CID_GROUP, "Members");
    ASSERT_TRUE(child != 0 && group != 0);
    place_entity(&g, g.owner_id, 3.0f, 0);
    place_entity(&g, child, 4.0f, g.owner_id);
    nmo_group_state_t *members = (nmo_group_state_t *)fixture_state(&g, group);
    ASSERT_NOT_NULL(members);
    nmo_ref_t *refs = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(&members->object_ids, 1u, (void **)&refs));
    refs[0] = nmo_ref_from_id(g.owner_id);
    g.script_id = fixture_add_script(&g, "Template Script", &g.script_in);
    ASSERT_TRUE(g.script_id != 0);
    nmo_beobject_state_t *be = (nmo_beobject_state_t *)fixture_state(&g, g.owner_id);
    nmo_behavior_state_t *script = (nmo_behavior_state_t *)fixture_state(&g, g.script_id);
    ASSERT_TRUE(be != NULL && script != NULL);
    be->has_single_activity = 1;
    be->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_INTERNAL_IC;
    script->has_single_activity = true;
    script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
    fixture_begin_edit(&g);
    nmo_object_id_t nop = fixture_block(&g, GUID_NOP, "Nop");
    ASSERT_TRUE(nop != 0);
    fixture_link(&g, g.script_in, BLOCK_IN(&g, nop, 0), 0);
    fixture_commit(&g, NULL);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", NMO_TEST_SCRATCH_DIR, "template.nmo");
    ASSERT_EQ(NMO_OK, nmo_document_save_file(g.document, path, NULL));
    fixture_end(&g);
}

/* The loaded file's object named `name`; 0 when there is none. */
static nmo_object_id_t loaded_named(engine_fixture_t *f, int32_t file, const char *name)
{
    for (int32_t i = 0; i < nmo_engine_loaded_object_count(f->engine, file); i++) {
        nmo_object_id_t id = nmo_engine_loaded_object(f->engine, file, i);
        const char *object_name = nmo_engine_object_name(f->engine, id);
        if (object_name != NULL && strcmp(object_name, name) == 0) {
            return id;
        }
    }
    return 0;
}

/* The script named `name` other than `other`; 0 when there is none. */
static nmo_object_id_t script_named(engine_fixture_t *f, const char *name, nmo_object_id_t other)
{
    for (int32_t i = 0; i < nmo_engine_script_count(f->engine); i++) {
        nmo_object_id_t id = nmo_engine_script_id(f->engine, i);
        const char *script_name = nmo_engine_object_name(f->engine, id);
        if (id != other && script_name != NULL && strcmp(script_name, name) == 0) {
            return id;
        }
    }
    return 0;
}

/* The copy of the template's child: the copies take the IDs after the template's copy. */
static nmo_object_id_t copied_child(engine_fixture_t *f, nmo_object_id_t copy)
{
    for (nmo_object_id_t id = copy + 1u; id < copy + 16u; id++) {
        const char *name = nmo_engine_object_name(f->engine, id);
        if (name != NULL && strcmp(name, "Child") == 0) {
            return id;
        }
    }
    return 0;
}

/* The X of the entity's world position; NaN for no entity */
static float position_x(engine_fixture_t *f, nmo_object_id_t id)
{
    float m[16];
    return nmo_engine_entity_world_matrix(f->engine, id, m) ? m[12] : NAN;
}

TEST(engine_copy, copies_take_scripts_children_and_the_original_state)
{
    save_template_file();
    engine_fixture_t f;
    fixture_begin(&f);
    fixture_commit(&f, NULL);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);
    int32_t file = -1;
    ASSERT_EQ(NMO_OK, nmo_engine_load(f.engine, "template.nmo", true, &file));
    nmo_object_id_t original = loaded_named(&f, file, "Template");
    nmo_object_id_t child = loaded_named(&f, file, "Child");
    nmo_object_id_t group = loaded_named(&f, file, "Members");
    nmo_object_id_t script = script_named(&f, "Template Script", 0);
    ASSERT_TRUE(original != 0 && child != 0 && group != 0 && script != 0);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    /* Moved at run time: the copies are where the originals are now */
    float m[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, original, m));
    m[12] = 10.0f;
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, original, m, false));

    nmo_object_id_t ids[2] = {original, f.owner_id};
    nmo_object_id_t copies[2] = {0, 0};
    ASSERT_EQ(NMO_OK, nmo_engine_copy_objects(f.engine, ids, 2, false, copies));
    nmo_object_id_t copy = copies[0];
    ASSERT_TRUE(copy != 0 && copy != original);
    ASSERT_EQ(0u, copies[1]); /* the engine's own document copies nothing */
    ASSERT_STR_EQ("Template", nmo_engine_object_name(f.engine, copy));
    ASSERT_FLOAT_EQ(10.0f, position_x(&f, copy), 0.0001f);
    /* The child is copied under the copy (3D entity children), the original keeps its own */
    nmo_object_id_t child_copy = copied_child(&f, copy);
    ASSERT_TRUE(child_copy != 0 && child_copy != child);
    ASSERT_EQ(copy, nmo_engine_entity_parent(f.engine, child_copy));
    ASSERT_EQ(original, nmo_engine_entity_parent(f.engine, child));
    ASSERT_FLOAT_EQ(11.0f, position_x(&f, child_copy), 0.0001f);
    /* Not in the original's group: the default options leave groups out */
    ASSERT_EQ(1, nmo_engine_group_count(f.engine, group));

    /* The copied script is active as its original is (to run this frame), and runs as its own */
    nmo_object_id_t script_copy = script_named(&f, "Template Script", script);
    ASSERT_TRUE(script_copy != 0);
    ASSERT_TRUE(nmo_engine_object_is_active(f.engine, copy));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, script_copy));
    ASSERT_EQ(1u, runs(&f, script));
    fixture_end(&f);
}

/* Write CK's custom CKDependencies sub-chunk (class, mask pairs) as `param`'s saved value. */
static void save_dependencies(engine_fixture_t *f, nmo_object_id_t param, const uint32_t *pairs,
                              size_t count)
{
    nmo_parameter_state_t *state = (nmo_parameter_state_t *)fixture_state(f, param);
    ASSERT_NOT_NULL(state);
    nmo_chunk_t *chunk = nmo_chunk_create(nmo_session_get_arena(f->session));
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, DEPENDENCIES_CHUNK_CUSTOM));
    for (size_t i = 0; i < count * 2u; i++) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, pairs[i]));
    }
    nmo_chunk_close(chunk);
    state->subchunk = chunk;
}

TEST(engine_copy, object_copy_joins_groups_and_delete_leaves_them)
{
    save_template_file();
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t copier = fixture_block(&f, GUID_OBJECT_COPY, "Copy");
    nmo_object_id_t deleter = fixture_block(&f, GUID_OBJECT_DELETE, "Delete");
    ASSERT_TRUE(copier != 0 && deleter != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, copier, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, copier, 0), BLOCK_IN(&f, deleter, 0), 0);
    nmo_object_id_t options = 0, original_pin = 0, deleted_pin = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(f.tx, BLOCK_PIN(&f, copier, 0),
                                                                   &options));
    ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(
                          f.tx, BLOCK_PIN(&f, copier, 1), &original_pin));
    ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(
                          f.tx, BLOCK_PIN(&f, deleter, 0), &deleted_pin));
    fixture_commit(&f, NULL);
    /* Ballance's options: a behavioral object copies its groups too */
    const uint32_t pairs[] = {NMO_CID_BEOBJECT, 7u};
    save_dependencies(&f, options, pairs, 1u);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);
    int32_t file = -1;
    ASSERT_EQ(NMO_OK, nmo_engine_load(f.engine, "template.nmo", true, &file));
    nmo_object_id_t original = loaded_named(&f, file, "Template");
    nmo_object_id_t group = loaded_named(&f, file, "Members");
    ASSERT_TRUE(original != 0 && group != 0);
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, original_pin, &original, sizeof(original)));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, deleted_pin, &original, sizeof(original)));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    uint32_t copy = 0;
    read_value(&f, BLOCK_POUT(&f, copier, 0), &copy, sizeof(copy));
    ASSERT_TRUE(copy != 0 && copy != original);
    /* The copy joined the original's group; deleting the original took it out */
    ASSERT_EQ(1, nmo_engine_group_count(f.engine, group));
    ASSERT_EQ(copy, nmo_engine_group_object(f.engine, group, 0));
    ASSERT_TRUE(nmo_engine_object_is_active(f.engine, copy));
    fixture_end(&f);
}

TEST(engine_copy, unique_name_copies_take_the_next_number)
{
    save_template_file();
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t first = fixture_block(&f, GUID_OBJECT_COPY, "Copy");
    nmo_object_id_t second = fixture_block(&f, GUID_OBJECT_COPY, "Copy Again");
    ASSERT_TRUE(first != 0 && second != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, first, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, first, 0), BLOCK_IN(&f, second, 0), 0);
    nmo_object_id_t options[2] = {0}, original_pins[2] = {0};
    const nmo_object_id_t copiers[2] = {first, second};
    for (int i = 0; i < 2; i++) {
        ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(
                              f.tx, BLOCK_PIN(&f, copiers[i], 0), &options[i]));
        ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(
                              f.tx, BLOCK_PIN(&f, copiers[i], 1), &original_pins[i]));
    }
    fixture_commit(&f, NULL);
    /* CKObject's name with CK_DEPENDENCIES_COPY_OBJECT_UNIQUENAME, the children copied too */
    const uint32_t pairs[] = {NMO_CID_OBJECT, 3u, NMO_CID_3DENTITY, 7u};
    save_dependencies(&f, options[0], pairs, 2u);
    save_dependencies(&f, options[1], pairs, 2u);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);
    int32_t file = -1;
    ASSERT_EQ(NMO_OK, nmo_engine_load(f.engine, "template.nmo", true, &file));
    nmo_object_id_t original = loaded_named(&f, file, "Template");
    ASSERT_TRUE(original != 0);
    for (int i = 0; i < 2; i++) {
        ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, original_pins[i], &original,
                                                     sizeof(original)));
    }

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    uint32_t copy1 = 0, copy2 = 0;
    read_value(&f, BLOCK_POUT(&f, first, 0), &copy1, sizeof(copy1));
    read_value(&f, BLOCK_POUT(&f, second, 0), &copy2, sizeof(copy2));
    ASSERT_TRUE(copy1 != 0 && copy2 != 0 && copy1 != copy2);
    ASSERT_STR_EQ("Template", nmo_engine_object_name(f.engine, original));
    ASSERT_STR_EQ("Template001", nmo_engine_object_name(f.engine, copy1));
    ASSERT_STR_EQ("Template002", nmo_engine_object_name(f.engine, copy2));
    /* The scripts keep their names: CKBehavior's default options skip the check */
    ASSERT_TRUE(script_named(&f, "Template Script", 0) != 0);
    ASSERT_EQ(original, nmo_engine_object_named(f.engine, "Template", 0));
    fixture_end(&f);
}

/* The source of the block's target, made for it */
static nmo_object_id_t target_source(engine_fixture_t *f, nmo_object_id_t block)
{
    nmo_object_id_t source = 0;
    nmo_object_id_t target = fixture_target(f, block);
    if (target == 0 ||
        nmo_script_edit_ensure_input_parameter_source(f->tx, target, &source) != NMO_OK) {
        return 0;
    }
    return source;
}

TEST(engine_copy, object_delete_takes_what_its_options_delete)
{
    save_template_file();
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t deleter = fixture_block(&f, GUID_OBJECT_DELETE, "Delete");
    ASSERT_TRUE(deleter != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, deleter, 0), 0);
    nmo_object_id_t deleted_pin = 0, options = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(
                          f.tx, BLOCK_PIN(&f, deleter, 0), &deleted_pin));
    ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(f.tx, BLOCK_PIN(&f, deleter, 1),
                                                                   &options));
    fixture_commit(&f, NULL);
    /* Ballance's level unloading: a group takes its objects along */
    const uint32_t pairs[] = {NMO_CID_GROUP, 1u};
    save_dependencies(&f, options, pairs, 1u);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);
    int32_t file = -1;
    ASSERT_EQ(NMO_OK, nmo_engine_load(f.engine, "template.nmo", true, &file));
    nmo_object_id_t original = loaded_named(&f, file, "Template");
    nmo_object_id_t child = loaded_named(&f, file, "Child");
    nmo_object_id_t group = loaded_named(&f, file, "Members");
    nmo_object_id_t script = script_named(&f, "Template Script", 0);
    ASSERT_TRUE(original != 0 && child != 0 && group != 0 && script != 0);
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, deleted_pin, &group, sizeof(group)));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, script));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* The group's member went with its script (deleting, always); not the member's child, which
     * the default delete options leave */
    float m[16];
    ASSERT_FALSE(nmo_engine_entity_world_matrix(f.engine, original, m));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, script));
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, child, m));
    fixture_end(&f);
}

TEST(engine_copy, delete_dynamic_objects_takes_the_dynamic_ones)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t dynamic = fixture_block(&f, GUID_OBJECT_CREATE, "Create Dynamic");
    nmo_object_id_t kept = fixture_block(&f, GUID_OBJECT_CREATE, "Create Kept");
    nmo_object_id_t deleter = fixture_block(&f, GUID_DELETE_DYNAMIC_OBJECTS, "Delete");
    ASSERT_TRUE(dynamic != 0 && kept != 0 && deleter != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, dynamic, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, dynamic, 0), BLOCK_IN(&f, kept, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, kept, 0), BLOCK_IN(&f, deleter, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, dynamic, 0), NMO_CID_DATAARRAY);
    fixture_input_value(&f, BLOCK_PIN(&f, dynamic, 1), "Dynamic", 8u);
    fixture_input_int(&f, BLOCK_PIN(&f, kept, 0), NMO_CID_DATAARRAY);
    fixture_input_value(&f, BLOCK_PIN(&f, kept, 1), "Kept", 5u);
    fixture_commit(&f, NULL);
    /* Local 0 is the Dynamic setting: off for the second */
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(&f, kept);
    ASSERT_TRUE(state != NULL && state->local_parameters.count >= 1);
    const int32_t off = 0;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(
                          f.engine, nmo_behavior_ref_array_get_id(&state->local_parameters, 0),
                          &off, sizeof(off)));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_TRUE(nmo_engine_object_named(f.engine, "Dynamic", NMO_CID_DATAARRAY) != 0);
    ASSERT_TRUE(nmo_engine_object_named(f.engine, "Kept", NMO_CID_DATAARRAY) != 0);
    /* Deleted at the end of the frame Delete Dynamic Objects ran in */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, deleter));
    ASSERT_EQ(0u, nmo_engine_object_named(f.engine, "Dynamic", NMO_CID_DATAARRAY));
    ASSERT_TRUE(nmo_engine_object_named(f.engine, "Kept", NMO_CID_DATAARRAY) != 0);
    fixture_end(&f);
}

TEST(engine_copy, initial_conditions_are_restored)
{
    save_template_file();
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t array = fixture_create(&f, NMO_CID_DATAARRAY, "Rows");
    ASSERT_TRUE(array != 0);
    /* Frame 0: the template's own initial value back; frame 1: the template and its child, and
     * the array, saved as they are; frame 2: those back */
    nmo_object_id_t restore = fixture_block(&f, GUID_RESTORE_IC, "Restore");
    nmo_object_id_t save = fixture_block(&f, GUID_TT_SAVE_IC, "Save");
    nmo_object_id_t save_rows = fixture_block(&f, GUID_TT_SAVE_IC, "Save Rows");
    nmo_object_id_t restore_all = fixture_block(&f, GUID_TT_RESTORE_IC, "Restore All");
    nmo_object_id_t restore_rows = fixture_block(&f, GUID_RESTORE_IC, "Restore Rows");
    ASSERT_TRUE(restore != 0 && save != 0 && save_rows != 0 && restore_all != 0 && restore_rows != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, restore, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, save, 0), 1);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, save_rows, 0), 1);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, restore_all, 0), 2);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, restore_rows, 0), 2);
    fixture_input_int(&f, BLOCK_PIN(&f, save, 0), 1); /* Hierarchy */
    fixture_input_int(&f, BLOCK_PIN(&f, restore_all, 0), 1);
    nmo_object_id_t restore_target = target_source(&f, restore);
    nmo_object_id_t save_target = target_source(&f, save);
    nmo_object_id_t restore_all_target = target_source(&f, restore_all);
    ASSERT_TRUE(restore_target != 0 && save_target != 0 && restore_all_target != 0);
    fixture_input_int(&f, fixture_target(&f, save_rows), (int32_t)array);
    fixture_input_int(&f, fixture_target(&f, restore_rows), (int32_t)array);
    fixture_commit(&f, NULL);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);
    int32_t file = -1;
    ASSERT_EQ(NMO_OK, nmo_engine_load(f.engine, "template.nmo", true, &file));
    nmo_object_id_t original = loaded_named(&f, file, "Template");
    nmo_object_id_t child = loaded_named(&f, file, "Child");
    ASSERT_TRUE(original != 0 && child != 0);
    for (int i = 0; i < 3; i++) {
        nmo_object_id_t pin = i == 0 ? restore_target : i == 1 ? save_target : restore_all_target;
        ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, pin, &original, sizeof(original)));
    }

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    /* Moved with its child: Restore IC puts it back where it was loaded (it was saved with an
     * initial value), the child keeps its place (it has none) */
    float m[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, original, m));
    m[12] = 10.0f;
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, original, m, false));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_FLOAT_EQ(3.0f, position_x(&f, original), 0.0001f);
    ASSERT_FLOAT_EQ(11.0f, position_x(&f, child), 0.0001f);
    /* Saved at frame 1 with the hierarchy and an empty array; moved and filled after */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    m[12] = 20.0f;
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, original, m, false));
    ASSERT_FLOAT_EQ(28.0f, position_x(&f, child), 0.0001f);
    ASSERT_EQ(NMO_OK, nmo_engine_array_insert_column(f.engine, array, -1, CKARRAYTYPE_INT,
                                                     NMO_GUID(0u, 0u)));
    ASSERT_EQ(NMO_OK, nmo_engine_array_insert_row(f.engine, array, -1, NULL));
    ASSERT_EQ(1, nmo_engine_array_row_count(f.engine, array));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_FLOAT_EQ(3.0f, position_x(&f, original), 0.0001f);
    ASSERT_FLOAT_EQ(11.0f, position_x(&f, child), 0.0001f);
    ASSERT_EQ(0, nmo_engine_array_row_count(f.engine, array));
    ASSERT_EQ(0, nmo_engine_array_column_count(f.engine, array));
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_copy, copies_take_scripts_children_and_the_original_state);
REGISTER_TEST(engine_copy, object_copy_joins_groups_and_delete_leaves_them);
REGISTER_TEST(engine_copy, unique_name_copies_take_the_next_number);
REGISTER_TEST(engine_copy, object_delete_takes_what_its_options_delete);
REGISTER_TEST(engine_copy, delete_dynamic_objects_takes_the_dynamic_ones);
REGISTER_TEST(engine_copy, initial_conditions_are_restored);
TEST_MAIN_END()
