/**
 * @file copy.c
 * @brief CKContext::CopyObjects for the engine: copies of objects, and of
 *        what CK's dependency options copy with them.
 *
 * As CKDependenciesContext::Copy does: the objects, and their dependencies
 * (PrepareDependencies of each class, as the dependency options say), are
 * copied; the copies refer to each other where the originals did, and to
 * the originals' other objects (a shared mesh) as those did. A copy takes
 * its original's state at the moment of the copy: the engine's matrices,
 * parameter values, attributes, array cells, group members and behavior
 * flags. Copies join the level scene their originals are in, active as the
 * copy asks or as their originals are.
 *
 * The copies are made in their originals' session (libnmo's runtime copy,
 * which clones the states and remaps the references between the copies).
 * Copies of a loaded file's objects become a module of their own over that
 * session; copies of created objects stay in the created objects' module.
 * A borrowed document gets a private session before its objects are copied.
 */

#include "../engine_internal.h"

#include "format/nmo_chunk_api.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_2dentity_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_sprite3d_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"
#include "../../runtime/runtime_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CK_DEPENDENCIES_FLAGS */
#define DEPENDENCIES_CUSTOM 0u
#define DEPENDENCIES_NONE 1u
#define DEPENDENCIES_FULL 2u

/* The identifiers of a saved CKDependencies (CKDependenciesSaver) */
#define DEPENDENCIES_CHUNK_NONE 0x12248766u
#define DEPENDENCIES_CHUNK_FULL 0x12248767u
#define DEPENDENCIES_CHUNK_CUSTOM 0x12248768u

/* CK_ATTRIBUT_DONOTCOPY */
#define ATTRIBUTE_DO_NOT_COPY 0x40u

/*
 * The classes' default copy dependencies, as CK2.dll and CK2_3D.dll register
 * them (CKClassRegisterDefaultDependencies, mode CK_DEPENDENCIES_COPY); a
 * class without one has none. A class's own entry is the one read.
 */
static const struct {
    uint32_t class_id;
    uint32_t mask;
} default_copy_dependencies[] = {
    {NMO_CID_OBJECT, 1u},       {NMO_CID_SCENEOBJECT, 1u},  {NMO_CID_BEOBJECT, 3u},
    {NMO_CID_DATAARRAY, 2u},    {NMO_CID_3DENTITY, 6u},     {NMO_CID_CHARACTER, 1u},
    {NMO_CID_GRID, 1u},         {NMO_CID_LAYER, 1u},        {NMO_CID_SPRITE3D, 1u},
    {NMO_CID_TARGETCAMERA, 1u}, {NMO_CID_TARGETLIGHT, 1u},  {NMO_CID_2DENTITY, 3u},
};

void eng_default_copy_dependencies(eng_dependencies_t *deps)
{
    memset(deps, 0, sizeof(*deps));
    deps->flags = DEPENDENCIES_CUSTOM;
    for (size_t i = 0; i < sizeof(default_copy_dependencies) / sizeof(default_copy_dependencies[0]);
         i++) {
        deps->classes[default_copy_dependencies[i].class_id] = default_copy_dependencies[i].mask;
    }
}

/* The default delete dependencies (mode CK_DEPENDENCIES_DELETE): a 3D entity's object animations */
static void default_delete_dependencies(eng_dependencies_t *deps)
{
    memset(deps, 0, sizeof(*deps));
    deps->flags = DEPENDENCIES_CUSTOM;
    deps->classes[NMO_CID_3DENTITY] = 4u;
}

/*
 * A Copy or Delete Dependencies parameter (CKDependenciesSaver): None, Full,
 * or the classes whose dependencies differ from the mode's defaults. The
 * defaults when the parameter saved none.
 */
void eng_dependencies_from_param(nmo_engine_t *engine, nmo_object_id_t param_id, bool deleting,
                                 eng_dependencies_t *deps)
{
    if (deleting) {
        default_delete_dependencies(deps);
    } else {
        eng_default_copy_dependencies(deps);
    }
    nmo_object_t *object = eng_find_object(engine, param_id);
    const nmo_parameter_state_t *state =
        object != NULL ? (const nmo_parameter_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                             engine->types, object, CKPGUID_PARAMETER)
                       : NULL;
    nmo_chunk_t *chunk = state != NULL ? state->subchunk : NULL;
    if (chunk == NULL || nmo_chunk_start_read(chunk) != NMO_OK) {
        return;
    }
    size_t dwords = 0;
    if (nmo_chunk_seek_identifier(chunk, DEPENDENCIES_CHUNK_NONE) == NMO_OK) {
        deps->flags = DEPENDENCIES_NONE;
    } else if (nmo_chunk_seek_identifier(chunk, DEPENDENCIES_CHUNK_FULL) == NMO_OK) {
        deps->flags = DEPENDENCIES_FULL;
    } else if (nmo_chunk_seek_identifier_with_size(chunk, DEPENDENCIES_CHUNK_CUSTOM, &dwords) ==
               NMO_OK) {
        for (size_t i = 0; i + 1 < dwords; i += 2) {
            uint32_t class_id = 0, mask = 0;
            if (nmo_chunk_read_dword(chunk, &class_id) != NMO_OK ||
                nmo_chunk_read_dword(chunk, &mask) != NMO_OK) {
                break;
            }
            if (class_id < ENG_DEPENDENCY_CLASSES) {
                deps->classes[class_id] = mask;
            }
        }
    }
    (void)nmo_chunk_start_read(chunk);
}

/* CKDependenciesContext::GetClassDependencies */
static uint32_t class_dependencies(const eng_dependencies_t *deps, uint32_t class_id)
{
    if (deps->flags == DEPENDENCIES_NONE) {
        return 0u;
    }
    if (deps->flags == DEPENDENCIES_FULL) {
        return 0xFFFFu;
    }
    return class_id < ENG_DEPENDENCY_CLASSES ? deps->classes[class_id] : 0u;
}

/* ============================================================================
 * The objects to copy (PrepareDependencies)
 * ============================================================================ */

typedef struct copy_set {
    nmo_engine_t *engine;
    const eng_dependencies_t *deps;
    bool deleting;              /* CK_DEPENDENCIES_DELETE rather than COPY */
    const eng_module_t *module; /* the originals' module: the session copied in (copying) */
    const eng_module_t *current; /* the module of the object being prepared */
    ENG_VEC(nmo_object_id_t) ids; /* engine IDs, in the order they were added */
    eng_map_t index;
} copy_set_t;

static bool derives(const nmo_engine_t *engine, uint32_t class_id, uint32_t base)
{
    return nmo_type_registry_is_class_derived_from(engine->types, class_id, base);
}

static const void *ancestor_state(const nmo_engine_t *engine, nmo_object_t *object,
                                  nmo_guid_t guid)
{
    return nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, guid);
}

static void prepare(copy_set_t *set, nmo_object_id_t id);

/* An object a reference of `owner_local`'s state names, as an engine ID */
static void prepare_ref(copy_set_t *set, const nmo_ref_t *ref)
{
    nmo_object_id_t local = nmo_ref_runtime_id(ref);
    if (local != 0) {
        prepare(set, eng_module_id(set->current, local));
    }
}

static void prepare_behavior_array(copy_set_t *set, const nmo_array_t *array)
{
    for (size_t i = 0; array != NULL && i < array->count; i++) {
        nmo_object_id_t local = nmo_behavior_ref_array_get_id(array, i);
        if (local != 0) {
            prepare(set, eng_module_id(set->current, local));
        }
    }
}

/*
 * CKBeObject::PrepareDependencies: its scripts (copying, those not locked)
 * and its attributes' parameters; deleting, both always.
 */
static void prepare_beobject(copy_set_t *set, nmo_object_t *object)
{
    nmo_engine_t *engine = set->engine;
    const nmo_beobject_state_t *be = (const nmo_beobject_state_t *)ancestor_state(
        engine, object, CKPGUID_BEOBJECT);
    if (be == NULL) {
        return;
    }
    uint32_t mask = class_dependencies(set->deps, NMO_CID_BEOBJECT);
    if ((mask & 1u) || set->deleting) {
        for (size_t i = 0; i < be->scripts.count; i++) {
            nmo_object_id_t local = nmo_beobject_script_array_get_id(&be->scripts, i);
            nmo_object_t *script = local != 0 ? nmo_object_repository_find_by_id(set->current->repo, local)
                                              : NULL;
            const nmo_behavior_state_t *state =
                script != NULL && nmo_object_get_class_id(script) == NMO_CID_BEHAVIOR
                    ? (const nmo_behavior_state_t *)nmo_object_get_state(script)
                    : NULL;
            if (state != NULL && (set->deleting || (state->flags & CKBEHAVIOR_LOCKED) == 0)) {
                prepare(set, eng_module_id(set->current, local));
            }
        }
    }
    if ((mask & 2u) || set->deleting) {
        for (size_t a = 0; a < be->attributes.count; a++) {
            const nmo_beobject_attribute_t *attribute =
                &((const nmo_beobject_attribute_t *)be->attributes.data)[a];
            int32_t type = eng_module_attribute_type(set->current, (int32_t)attribute->type_id);
            uint32_t flags = type >= 0 && (size_t)type < engine->attributes.types.count
                                 ? engine->attributes.types.data[type].flags
                                 : 0u;
            if ((flags & ATTRIBUTE_DO_NOT_COPY) == 0) {
                prepare_ref(set, &attribute->parameter);
            }
        }
    }
}

/* CKBehavior::PrepareDependencies: its IOs, links, sub-behaviors, operations and parameters */
static void prepare_behavior(copy_set_t *set, nmo_object_t *object)
{
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)nmo_object_get_state(object);
    if (state == NULL) {
        return;
    }
    nmo_object_id_t target = nmo_behavior_target_parameter_id(state);
    if (target != 0) {
        prepare(set, eng_module_id(set->current, target));
    }
    prepare_behavior_array(set, &state->inputs);
    prepare_behavior_array(set, &state->outputs);
    prepare_behavior_array(set, &state->sub_behavior_links);
    prepare_behavior_array(set, &state->sub_behaviors);
    prepare_behavior_array(set, &state->operations);
    prepare_behavior_array(set, &state->in_parameters);
    prepare_behavior_array(set, &state->out_parameters);
    prepare_behavior_array(set, &state->local_parameters);
}

/* CKParameterOperation::PrepareDependencies: its two inputs and its output */
static void prepare_operation(copy_set_t *set, nmo_object_t *object)
{
    const nmo_parameteroperation_state_t *state =
        (const nmo_parameteroperation_state_t *)nmo_object_get_state(object);
    nmo_object_id_t ids[3] = {nmo_parameteroperation_in1_id(state),
                              nmo_parameteroperation_in2_id(state),
                              nmo_parameteroperation_out_id(state)};
    for (int i = 0; state != NULL && i < 3; i++) {
        if (ids[i] != 0) {
            prepare(set, eng_module_id(set->current, ids[i]));
        }
    }
}

/* RCK3dEntity::PrepareDependencies: meshes, children (the engine's), object animations */
static void prepare_3dentity(copy_set_t *set, nmo_object_t *object, nmo_object_id_t id)
{
    nmo_engine_t *engine = set->engine;
    const nmo_3dentity_state_t *state = (const nmo_3dentity_state_t *)ancestor_state(
        engine, object, CKPGUID_3DENTITY);
    uint32_t mask = class_dependencies(set->deps, NMO_CID_3DENTITY);
    if (state != NULL && (mask & 1u)) {
        for (uint32_t i = 0; i < state->mesh_count; i++) {
            prepare_ref(set, &state->mesh_ids[i]);
        }
    }
    int32_t entity = eng_entity_index(engine, id);
    if (entity >= 0 && (mask & 2u)) {
        for (int32_t c = engine->entities.data[entity].first_child; c >= 0;
             c = engine->entities.data[c].next_sibling) {
            prepare(set, engine->entities.data[c].id);
        }
    }
    if (state != NULL && (mask & 4u)) {
        for (uint32_t i = 0; i < state->animation_count; i++) {
            prepare_ref(set, &state->animation_ids[i]);
        }
    }
}

/* RCKMesh::PrepareDependencies: its materials */
static void prepare_mesh(copy_set_t *set, nmo_object_t *object)
{
    const nmo_mesh_state_t *state = (const nmo_mesh_state_t *)ancestor_state(
        set->engine, object, CKPGUID_MESH);
    if (state == NULL || (class_dependencies(set->deps, NMO_CID_MESH) & 1u) == 0) {
        return;
    }
    for (uint32_t i = 0; i < state->material_group_count; i++) {
        prepare_ref(set, &state->material_groups[i].material);
    }
    for (uint32_t i = 0; i < state->material_channel_count; i++) {
        prepare_ref(set, &state->material_channels[i].material);
    }
}

/* RCKMaterial::PrepareDependencies: its textures, and its effect parameter always */
static void prepare_material(copy_set_t *set, nmo_object_t *object)
{
    const nmo_material_state_t *state = (const nmo_material_state_t *)ancestor_state(
        set->engine, object, CKPGUID_MATERIAL);
    if (state == NULL) {
        return;
    }
    if (class_dependencies(set->deps, NMO_CID_MATERIAL) & 1u) {
        for (int i = 0; i < 4; i++) {
            prepare_ref(set, &state->textures[i]);
        }
    }
    prepare_ref(set, &state->effect_parameter);
}

/* CKGroup::PrepareDependencies: its objects (the engine's); deleting, not a level or scene */
static void prepare_group(copy_set_t *set, nmo_object_id_t id)
{
    if ((class_dependencies(set->deps, NMO_CID_GROUP) & 1u) == 0) {
        return;
    }
    int32_t count = nmo_engine_group_count(set->engine, id);
    for (int32_t i = 0; i < count; i++) {
        nmo_object_id_t member = nmo_engine_group_object(set->engine, id, i);
        uint32_t class_id = member != 0 ? (uint32_t)nmo_engine_object_class(set->engine, member) : 0u;
        if (set->deleting && (derives(set->engine, class_id, NMO_CID_LEVEL) ||
                              derives(set->engine, class_id, NMO_CID_SCENE))) {
            continue;
        }
        prepare(set, member);
    }
}

/* CKDataArray::PrepareDependencies: the objects of its object cells (not a level or scene) */
static void prepare_array(copy_set_t *set, nmo_object_id_t id)
{
    nmo_engine_t *engine = set->engine;
    uint32_t mask = class_dependencies(set->deps, NMO_CID_DATAARRAY);
    if ((mask & 2u) == 0 || (mask & 1u) == 0) {
        return;
    }
    int32_t rows = nmo_engine_array_row_count(engine, id);
    int32_t columns = nmo_engine_array_column_count(engine, id);
    for (int32_t c = 0; c < columns; c++) {
        if (nmo_engine_array_column_type(engine, id, c) != CKARRAYTYPE_OBJECT) {
            continue;
        }
        for (int32_t r = 0; r < rows; r++) {
            nmo_engine_cell_t cell;
            if (nmo_engine_array_get_cell(engine, id, r, c, &cell) != NMO_OK || cell.value == 0) {
                continue;
            }
            uint32_t class_id = (uint32_t)nmo_engine_object_class(engine, cell.value);
            if (class_id != 0 && !derives(engine, class_id, NMO_CID_LEVEL) &&
                !derives(engine, class_id, NMO_CID_SCENE)) {
                prepare(set, cell.value);
            }
        }
    }
}

static void prepare_object(copy_set_t *set, nmo_object_t *object, nmo_object_id_t id);

/*
 * PrepareDependencies of an object: itself once, then what its classes
 * bring along. Only objects of the originals' session can be copied;
 * deleting, any object the engine has not destroyed.
 */
static void prepare(copy_set_t *set, nmo_object_id_t id)
{
    nmo_engine_t *engine = set->engine;
    nmo_object_id_t local = 0;
    const eng_module_t *module = eng_module_of(engine, id, &local);
    if (module == NULL || eng_map_get(&set->index, id) >= 0) {
        return;
    }
    if (set->deleting ? eng_map_get(&engine->destroyed, id) >= 0
                      : module->repo != set->module->repo) {
        return;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(module->repo, local);
    nmo_object_id_t *slot = NULL;
    if (object == NULL || !ENG_VEC_PUSH(set->ids, slot) ||
        !eng_map_put(&set->index, id, (int32_t)(set->ids.count - 1u))) {
        return;
    }
    *slot = id;
    const eng_module_t *outer = set->current;
    set->current = module;
    prepare_object(set, object, id);
    set->current = outer;
}

static void prepare_object(copy_set_t *set, nmo_object_t *object, nmo_object_id_t id)
{
    nmo_engine_t *engine = set->engine;
    uint32_t class_id = (uint32_t)nmo_object_get_class_id(object);
    if (class_id == NMO_CID_BEHAVIOR) {
        prepare_behavior(set, object);
        return;
    }
    if (class_id == NMO_CID_PARAMETEROPERATION) {
        prepare_operation(set, object);
        return;
    }
    if (!derives(engine, class_id, NMO_CID_BEOBJECT)) {
        return;
    }
    prepare_beobject(set, object);
    if (derives(engine, class_id, NMO_CID_3DENTITY)) {
        prepare_3dentity(set, object, id);
        const nmo_sprite3d_state_t *sprite = derives(engine, class_id, NMO_CID_SPRITE3D)
            ? (const nmo_sprite3d_state_t *)ancestor_state(engine, object, CKPGUID_SPRITE3D)
            : NULL;
        if (sprite != NULL && (class_dependencies(set->deps, NMO_CID_SPRITE3D) & 1u)) {
            prepare_ref(set, &sprite->material);
        }
    } else if (derives(engine, class_id, NMO_CID_2DENTITY)) {
        const nmo_2dentity_state_t *entity =
            (const nmo_2dentity_state_t *)ancestor_state(engine, object, CKPGUID_2DENTITY);
        if (entity != NULL && entity->has_material &&
            (class_dependencies(set->deps, NMO_CID_2DENTITY) & 1u)) {
            prepare_ref(set, &entity->material);
        }
    } else if (derives(engine, class_id, NMO_CID_MESH)) {
        prepare_mesh(set, object);
    } else if (derives(engine, class_id, NMO_CID_MATERIAL)) {
        prepare_material(set, object);
    } else if (derives(engine, class_id, NMO_CID_GROUP)) {
        prepare_group(set, id);
    } else if (derives(engine, class_id, NMO_CID_DATAARRAY)) {
        prepare_array(set, id);
    }
}

/* ============================================================================
 * Copying
 * ============================================================================ */

/* The copy of an original of the set: its engine ID; 0 for none. */
static nmo_object_id_t copy_of(const copy_set_t *set, const nmo_object_id_t *copies,
                               nmo_object_id_t original)
{
    int32_t i = original != 0 ? eng_map_get(&set->index, original) : -1;
    return i >= 0 ? copies[i] : 0;
}

/* The model of a module, built again after its session changed. */
static nmo_status_t rebuild_model(eng_module_t *module)
{
    nmo_script_model_t *model = NULL;
    nmo_status_t status = nmo_script_model_build(module->workspace, &model);
    if (status != NMO_OK) {
        return status;
    }
    nmo_script_model_destroy(module->model);
    module->model = model;
    return NMO_OK;
}

static int32_t *duplicate_types(const int32_t *types, uint32_t count)
{
    int32_t *copy = count > 0 ? (int32_t *)malloc(count * sizeof(int32_t)) : NULL;
    if (copy != NULL) {
        memcpy(copy, types, count * sizeof(int32_t));
    }
    return copy;
}

/*
 * The module the copies are in: the created objects' own (their IDs are in
 * its window), or a new one over the originals' session, for the IDs
 * [first, last]. Its index; -1 on failure.
 */
static int32_t copies_module(nmo_engine_t *engine, int32_t source, nmo_object_id_t first,
                             nmo_object_id_t last)
{
    eng_module_t *origin = &engine->modules.data[source];
    if (eng_module_has(origin, first) && eng_module_has(origin, last)) {
        return rebuild_model(origin) == NMO_OK ? source : -1;
    }
    eng_module_t module;
    memset(&module, 0, sizeof(module));
    module.engine = engine;
    module.workspace = origin->workspace;
    module.repo = origin->repo;
    module.offset = eng_next_engine_id(engine) - first;
    module.limit = module.offset + last;
    module.first_local = first;
    module.last_local = last;
    module.message_types = duplicate_types(origin->message_types, origin->message_type_count);
    module.message_type_count = module.message_types != NULL ? origin->message_type_count : 0u;
    module.attribute_types = duplicate_types(origin->attribute_types, origin->attribute_type_count);
    module.attribute_type_count =
        module.attribute_types != NULL ? origin->attribute_type_count : 0u;
    eng_module_t *slot = NULL;
    if (nmo_script_model_build(module.workspace, &module.model) != NMO_OK ||
        !ENG_VEC_PUSH(engine->modules, slot)) {
        nmo_script_model_destroy(module.model);
        free(module.message_types);
        free(module.attribute_types);
        return -1;
    }
    *slot = module;
    return (int32_t)engine->modules.count - 1;
}

/* CKBehavior::Copy and friends: the runtime state of the behaviors, IOs and links. */
static void copy_behavior_state(nmo_engine_t *engine, nmo_object_id_t original, nmo_object_id_t copy)
{
    int32_t from = eng_map_get(&engine->node_map, original), to = eng_map_get(&engine->node_map, copy);
    if (from >= 0 && to >= 0) {
        engine->nodes.data[to].flags =
            engine->nodes.data[from].flags &
            ~(uint32_t)(CKBEHAVIOR_RESERVED0 | CKBEHAVIOR_EXECUTEDLASTFRAME);
        return;
    }
    from = eng_map_get(&engine->io_map, original);
    to = eng_map_get(&engine->io_map, copy);
    if (from >= 0 && to >= 0) {
        engine->ios.data[to].active = engine->ios.data[from].active;
    }
}

/* A parameter's value at the copy, its object pointed at the copy when there is one. */
static void copy_param_value(nmo_engine_t *engine, const copy_set_t *set,
                             const nmo_object_id_t *copies, nmo_object_id_t original,
                             nmo_object_id_t copy)
{
    int32_t from = eng_map_get(&engine->param_map, original), to = eng_map_get(&engine->param_map, copy);
    if (from < 0 || to < 0 || engine->params.data[from].data == NULL) {
        return;
    }
    eng_param_t *source = &engine->params.data[from];
    if (eng_param_write(engine, &engine->params.data[to], source->data, source->size) != NMO_OK) {
        return;
    }
    eng_param_t *dest = &engine->params.data[to];
    if (dest->type_desc != NULL && dest->type_desc->object_class_id > 0 &&
        dest->size >= sizeof(uint32_t)) {
        uint32_t id = 0;
        memcpy(&id, dest->data, sizeof(id));
        nmo_object_id_t mapped = copy_of(set, copies, id);
        if (mapped != 0) {
            id = (uint32_t)mapped;
            memcpy(dest->data, &id, sizeof(id));
        }
    }
}

/* RCK3dEntity::Copy: the original's matrices, under its parent's copy (or its parent). */
static void copy_entity(nmo_engine_t *engine, const copy_set_t *set, const nmo_object_id_t *copies,
                        nmo_object_id_t original, nmo_object_id_t copy)
{
    int32_t from = eng_entity_index(engine, original), to = eng_entity_index(engine, copy);
    if (from < 0 || to < 0) {
        return;
    }
    int32_t parent = engine->entities.data[from].parent;
    nmo_object_id_t parent_id = parent >= 0 ? engine->entities.data[parent].id : 0;
    nmo_object_id_t parent_copy = copy_of(set, copies, parent_id);
    int32_t new_parent = eng_entity_index(engine, parent_copy != 0 ? parent_copy : parent_id);
    eng_entity_set_parent(engine, to, new_parent, true);
    eng_matrix_t world = engine->entities.data[from].world;
    eng_entity_set_world_matrix(engine, to, &world, true);
}

/*
 * CKBehaviorLink::Copy: each copied link's delay as its original's, waiting
 * in its graph's delayed list when the original was.
 */
static void copy_links(nmo_engine_t *engine, const copy_set_t *set, const nmo_object_id_t *copies,
                       size_t first_link)
{
    eng_map_t originals;
    memset(&originals, 0, sizeof(originals));
    for (size_t l = 0; l < first_link; l++) {
        if (copy_of(set, copies, engine->links.data[l].id) != 0 &&
            !eng_map_put(&originals, engine->links.data[l].id, (int32_t)l)) {
            break;
        }
    }
    for (size_t l = 0; l < first_link; l++) {
        const eng_link_t *original = &engine->links.data[l];
        nmo_object_id_t copy_id = copy_of(set, copies, original->id);
        if (copy_id == 0) {
            continue;
        }
        for (size_t c = first_link; c < engine->links.count; c++) {
            eng_link_t *copy = &engine->links.data[c];
            if (copy->id != copy_id) {
                continue;
            }
            copy->delay = original->delay;
            if (original->in_delayed_list && copy->graph >= 0) {
                eng_node_t *graph = &engine->nodes.data[copy->graph];
                if (graph->delayed_count < graph->link_count) {
                    copy->in_delayed_list = true;
                    engine->indexes.data[graph->first_delayed + graph->delayed_count++] = (int32_t)c;
                }
            }
            break;
        }
    }
    free(originals.slots);
}

/* Whether a script is active in the scene: running, or to run from the next frame. */
static bool script_active(const nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t node = eng_map_get(&engine->node_map, id);
    if (node < 0) {
        return false;
    }
    uint32_t flags = engine->nodes.data[node].flags;
    return (flags & CKBEHAVIOR_DEACTIVATENEXTFRAME) == 0 &&
           (flags & (CKBEHAVIOR_ACTIVE | CKBEHAVIOR_ACTIVATENEXTFRAME)) != 0;
}

static int compare_ids(const void *a, const void *b)
{
    nmo_object_id_t x = *(const nmo_object_id_t *)a, y = *(const nmo_object_id_t *)b;
    return x < y ? -1 : x > y;
}

/* The repository's object IDs, ascending, as a heap array of `*out_count` */
static nmo_object_id_t *repository_ids(nmo_object_repository_t *repo, size_t *out_count)
{
    size_t count = nmo_object_repository_get_count(repo);
    *out_count = count;
    nmo_object_id_t *ids = count > 0 ? (nmo_object_id_t *)malloc(count * sizeof(*ids)) : NULL;
    if (ids == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        ids[i] = nmo_object_get_id(nmo_object_repository_get_by_index(repo, i));
    }
    qsort(ids, count, sizeof(*ids), compare_ids);
    return ids;
}

/* Whether a class's own default options (CKClassRegisterDefaultOptions) have
   CK_GENERALOPTIONS_NODUPLICATENAMECHECK or _AUTOMATICUSECURRENT: its copies keep their names */
static bool class_keeps_copy_names(uint32_t class_id)
{
    switch (class_id) {
    case NMO_CID_PARAMETERIN: case NMO_CID_PARAMETEROUT: case NMO_CID_PARAMETEROPERATION:
    case NMO_CID_BEHAVIORLINK: case NMO_CID_BEHAVIOR: case NMO_CID_BEHAVIORIO:
    case NMO_CID_KINEMATICCHAIN: case NMO_CID_OBJECTANIMATION: case NMO_CID_LEVEL:
    case NMO_CID_PARAMETERLOCAL: case NMO_CID_PARAMETER: case NMO_CID_INTERFACEOBJECTMANAGER:
    case NMO_CID_LAYER:
        return true;
    default:
        return false;
    }
}

/*
 * CKContext::GetObjectSecureName: the name without three trailing digits,
 * then the next number (three digits at least) after the highest one the
 * objects of exactly that class carry after it, or the name as it is when
 * none starts with it. A heap string; NULL out of memory.
 */
static char *secure_name(const nmo_engine_t *engine, const char *name, uint32_t class_id)
{
    size_t length = strlen(name);
    int highest = -1;
    if (length > 3 && isdigit((unsigned char)name[length - 3]) &&
        isdigit((unsigned char)name[length - 2]) && isdigit((unsigned char)name[length - 1])) {
        highest = atoi(name + length - 3);
        length -= 3;
    }
    for (size_t m = 0; m < engine->modules.count; m++) {
        const eng_module_t *module = &engine->modules.data[m];
        size_t count = 0;
        nmo_object_t **objects = nmo_object_repository_get_all(module->repo, &count);
        for (size_t i = 0; objects != NULL && i < count; i++) {
            const char *other = nmo_object_get_name(objects[i]);
            nmo_object_id_t local = nmo_object_get_id(objects[i]);
            if (other == NULL || (uint32_t)nmo_object_get_class_id(objects[i]) != class_id ||
                !eng_module_has(module, local) ||
                eng_map_get(&engine->destroyed, eng_module_id(module, local)) >= 0 ||
                strncmp(other, name, length) != 0) {
                continue;
            }
            int number = other[length] != '\0' ? atoi(other + length) : 0;
            highest = number > highest ? number : highest;
        }
    }
    int size = highest < 0 ? (int)length : snprintf(NULL, 0, "%.*s%03d", (int)length, name, highest + 1);
    char *secure = size >= 0 ? (char *)malloc((size_t)size + 1u) : NULL;
    if (secure != NULL && highest < 0) {
        memcpy(secure, name, length);
        secure[length] = '\0';
    } else if (secure != NULL) {
        snprintf(secure, (size_t)size + 1u, "%.*s%03d", (int)length, name, highest + 1);
    }
    return secure;
}

/*
 * CKObject::Copy's name: the original's with CK_DEPENDENCIES_COPY_OBJECT_NAME
 * (made unique with CK_DEPENDENCIES_COPY_OBJECT_UNIQUENAME, unless the class
 * keeps names), else none but for shared names and parameter ins and outs.
 * The dependencies read are CKObject's, whatever the copy's class.
 */
static nmo_status_t name_copy(nmo_engine_t *engine, nmo_object_repository_t *repo,
                              nmo_object_id_t local, const eng_dependencies_t *deps)
{
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, local);
    const char *name = object != NULL ? nmo_object_get_name(object) : NULL;
    if (name == NULL) {
        return NMO_OK;
    }
    uint32_t class_id = (uint32_t)nmo_object_get_class_id(object);
    uint32_t dependencies = class_dependencies(deps, NMO_CID_OBJECT);
    if ((dependencies & 1u) == 0) {
        if ((nmo_object_get_flags(object) & CK_OBJECT_NAMESHARED) != 0 ||
            class_id == NMO_CID_PARAMETEROUT || class_id == NMO_CID_PARAMETERIN) {
            return NMO_OK;
        }
        return nmo_object_repository_rename(repo, local, NULL);
    }
    if (class_keeps_copy_names(class_id) || (dependencies & 2u) == 0) {
        return NMO_OK;
    }
    char *secure = secure_name(engine, name, class_id);
    if (secure == NULL) {
        return NMO_ERR_NOMEM;
    }
    nmo_status_t status = nmo_object_repository_rename(repo, local, secure);
    free(secure);
    return status;
}

/*
 * Copy the originals of one session with what their dependencies bring;
 * `out_copies[i]` gets the copy of `ids[i]` for each of them in the session.
 */
static nmo_status_t copy_session(nmo_engine_t *engine, int32_t source, const nmo_object_id_t *ids,
                                 int32_t count, const eng_dependencies_t *deps, bool activate,
                                 bool dynamic, nmo_object_id_t *out_copies)
{
    copy_set_t set;
    memset(&set, 0, sizeof(set));
    set.engine = engine;
    set.deps = deps;
    set.module = &engine->modules.data[source];
    set.current = set.module;
    for (int32_t i = 0; i < count; i++) {
        const eng_module_t *module = eng_module_of(engine, ids[i], NULL);
        if (module != NULL && module->repo == set.module->repo) {
            prepare(&set, ids[i]);
        }
    }
    nmo_status_t status = NMO_OK;
    size_t n = set.ids.count;
    nmo_object_id_t *locals = n > 0 ? (nmo_object_id_t *)malloc(n * sizeof(nmo_object_id_t)) : NULL;
    nmo_object_id_t *copies = n > 0 ? (nmo_object_id_t *)calloc(n, sizeof(nmo_object_id_t)) : NULL;
    if (n == 0 || locals == NULL || copies == NULL) {
        status = n == 0 ? NMO_OK : NMO_ERR_NOMEM;
        goto done;
    }
    for (size_t i = 0; i < n; i++) {
        eng_module_of(engine, set.ids.data[i], &locals[i]);
    }

    /* The copies take increasing IDs in their originals' order: they are the IDs new after the copy */
    nmo_object_repository_t *repo = set.module->repo;
    nmo_session_t *session = set.module->session;
    size_t before = 0;
    nmo_object_id_t *existing = repository_ids(repo, &before);
    if (existing == NULL && before > 0) {
        status = NMO_ERR_NOMEM;
        goto done;
    }
    status = nmo_session_copy_objects(session, locals, n, 0u, NULL);
    size_t after = 0;
    nmo_object_id_t *now = status == NMO_OK ? repository_ids(repo, &after) : NULL;
    if (status == NMO_OK && after != before + n) {
        status = now == NULL ? NMO_ERR_NOMEM : NMO_ERR_INVALID_STATE;
    }
    size_t added = 0;
    for (size_t i = 0; status == NMO_OK && i < after; i++) {
        if (bsearch(&now[i], existing, before, sizeof(*existing), compare_ids) == NULL && added < n) {
            locals[added++] = now[i];
        }
    }
    free(existing);
    free(now);
    if (status != NMO_OK) {
        goto done;
    }
    nmo_object_id_t first = locals[0], last = locals[n - 1];
    size_t first_node = engine->nodes.count, first_object = engine->objects.count;
    size_t first_link = engine->links.count;
    int32_t module = copies_module(engine, source, first, last);
    if (module < 0) {
        status = NMO_ERR_NOMEM;
        goto done;
    }
    status = eng_load_range(engine, module, first, last);
    if (status != NMO_OK) {
        goto done;
    }
    for (size_t i = 0; status == NMO_OK && i < n; i++) {
        status = name_copy(engine, engine->modules.data[module].repo, locals[i], deps);
    }
    if (status != NMO_OK) {
        goto done;
    }
    for (size_t i = 0; i < n; i++) {
        copies[i] = eng_module_id(&engine->modules.data[module], locals[i]);
        if (dynamic) {
            eng_mark_dynamic(engine, copies[i]);
        }
    }
    /* Copies are in no scene until added to one, as after a load */
    for (size_t o = first_object; o < engine->objects.count; o++) {
        engine->objects.data[o].active = false;
    }
    eng_bind_blocks(engine, first_node);

    /* Each copy takes its original's state (Copy, then RemapDependencies) */
    for (size_t i = 0; i < n; i++) {
        nmo_object_id_t original = set.ids.data[i], copy = copies[i];
        copy_behavior_state(engine, original, copy);
        copy_param_value(engine, &set, copies, original, copy);
        eng_copy_attributes(engine, original, copy);
        eng_copy_array(engine, original, copy);
        eng_copy_group(engine, original, copy, set.ids.data, copies, n);
        eng_copy_animation(engine, original, copy, set.ids.data, copies, n);
    }
    if ((class_dependencies(deps, NMO_CID_BEOBJECT) & 4u) != 0) {
        for (size_t i = 0; i < n; i++) {
            if (derives(engine, (uint32_t)nmo_engine_object_class(engine, copies[i]), NMO_CID_BEOBJECT)) {
                eng_group_join(engine, set.ids.data[i], copies[i]);
            }
        }
    }
    for (size_t i = 0; i < n; i++) {
        copy_entity(engine, &set, copies, set.ids.data[i], copies[i]);
    }
    copy_links(engine, &set, copies, first_link);

    /* The copies join the level scene with their originals, active as asked or as they were */
    for (size_t i = 0; i < n; i++) {
        nmo_object_id_t original = set.ids.data[i], copy = copies[i];
        if (!eng_in_scene(engine, original)) {
            continue;
        }
        eng_add_to_level_scene(engine, copy);
        uint32_t class_id = (uint32_t)nmo_engine_object_class(engine, copy);
        if (class_id == NMO_CID_BEHAVIOR) {
            if (script_active(engine, original)) {
                nmo_engine_activate_script(engine, copy, true, true);
            }
        } else if (derives(engine, class_id, NMO_CID_BEOBJECT) &&
                   (activate || nmo_engine_object_is_active(engine, original))) {
            nmo_engine_activate_object(engine, copy, true);
        }
    }
    for (int32_t i = 0; i < count; i++) {
        nmo_object_id_t copy = copy_of(&set, copies, ids[i]);
        if (copy != 0) {
            out_copies[i] = copy;
        }
    }

done:
    free(locals);
    free(copies);
    free(set.ids.data);
    free(set.index.slots);
    return status;
}

nmo_status_t eng_copy_objects(nmo_engine_t *engine, const nmo_object_id_t *ids, int32_t count,
                              const eng_dependencies_t *deps, bool activate, bool dynamic,
                              nmo_object_id_t *out_copies)
{
    for (int32_t i = 0; i < count; i++) {
        out_copies[i] = 0;
    }
    /* Each session the originals are in, once, in the order the originals come */
    for (int32_t i = 0; i < count; i++) {
        const eng_module_t *module = eng_module_of(engine, ids[i], NULL);
        if (module == NULL || out_copies[i] != 0) {
            continue;
        }
        int32_t source = (int32_t)(module - engine->modules.data);
        if (module->session == NULL) {
            /* A copies window shares the session its original module owns. */
            for (size_t m = 0; m < engine->modules.count; m++) {
                if (engine->modules.data[m].repo == module->repo &&
                    engine->modules.data[m].session != NULL) {
                    source = (int32_t)m;
                    break;
                }
            }
            if (engine->modules.data[source].session == NULL) {
                nmo_status_t status = eng_own_module(engine, source);
                if (status != NMO_OK) return status;
            }
            module = &engine->modules.data[source];
        }
        bool done = false;
        for (int32_t k = 0; k < i && !done; k++) {
            const eng_module_t *earlier = eng_module_of(engine, ids[k], NULL);
            done = earlier != NULL && earlier->repo == module->repo;
        }
        if (done) {
            continue;
        }
        /* The session's owner: the loaded file's or the created objects' module */
        nmo_status_t status =
            copy_session(engine, source, ids, count, deps, activate, dynamic, out_copies);
        if (status != NMO_OK) {
            return status;
        }
    }
    return NMO_OK;
}

/*
 * CKContext::DestroyObject with dependencies: the objects, and what the
 * options delete with them (PrepareDependencies in CK_DEPENDENCIES_DELETE
 * mode), destroyed.
 */
void eng_destroy_objects(nmo_engine_t *engine, const nmo_object_id_t *ids, int32_t count,
                         const eng_dependencies_t *deps)
{
    copy_set_t set;
    memset(&set, 0, sizeof(set));
    set.engine = engine;
    set.deps = deps;
    set.deleting = true;
    for (int32_t i = 0; i < count; i++) {
        prepare(&set, ids[i]);
    }
    for (size_t i = 0; i < set.ids.count; i++) {
        nmo_engine_destroy_object(engine, set.ids.data[i]);
    }
    free(set.ids.data);
    free(set.index.slots);
}

nmo_status_t nmo_engine_copy_objects(nmo_engine_t *engine, const nmo_object_id_t *ids,
                                     int32_t count, bool activate, nmo_object_id_t *out_copies)
{
    if (engine == NULL || (count > 0 && (ids == NULL || out_copies == NULL)) || count < 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    eng_dependencies_t deps;
    eng_default_copy_dependencies(&deps);
    return eng_copy_objects(engine, ids, count, &deps, activate, false, out_copies);
}
