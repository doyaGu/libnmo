/**
 * @file test_corpus_semantics_scene.c
 * @brief Decoded scene, group, data array, animation and attribute values of the
 *        reference corpus satisfy what the Virtools engine guarantees
 *
 * test_corpus_chunk_roundtrip proves that no bytes are lost; it cannot tell a
 * field read into the wrong member from a correct one. Every invariant below
 * is a relationship between decoded values that the engine enforces in a
 * setter or loader, or that its own Save always satisfies; the function is
 * named at each check. Each invariant keeps two counters: how many values were
 * checked, which the test requires to be positive so a check cannot pass by
 * never running, and how many violated, which must be zero.
 *
 * A few checks are domain checks the engine does not enforce (a length is not
 * negative, a key quaternion is a unit quaternion); they say so. The corpus
 * holds no morph animation, so nothing is checked about morph keys.
 */

#include "../test_framework.h"

#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_data.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_attributemanager_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_interfaceobjectmanager_schemas.h"
#include "object/builtin/nmo_level_schemas.h"
#include "object/builtin/nmo_messagemanager_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/builtin/nmo_synchro_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_manager_guids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_context.h"
#include "session/nmo_session.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_query.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The first violations of each invariant are printed, so one noisy invariant
 * does not hide the others. */
#define MAX_REPORTED_VIOLATIONS 6

typedef enum invariant_id {
    /* References followed by the checks below */
    INV_REFS_RESOLVE,
    /* CKObject / CKBeObject */
    INV_OBJECT_VISIBILITY,
    INV_BEO_SCRIPT_CLASS,
    INV_BEO_SCRIPT_UNIQUE,
    INV_BEO_SCRIPT_ORDER,
    INV_BEO_PRIORITY,
    INV_BEO_ACTIVITY_FLAGS,
    INV_BEO_ACTIVITY_FILE,
    INV_BEO_ATTR_SECTION,
    INV_BEO_ATTR_TYPE_SAVED,
    INV_BEO_ATTR_UNIQUE,
    INV_BEO_ATTR_CLASS,
    INV_BEO_ATTR_PARAM,
    INV_BEO_ATTR_PARAM_TYPE,
    INV_BEO_ATTR_PARAM_UNIQUE,
    /* CKAttributeManager */
    INV_ATM_SAVED_FLAGS,
    INV_ATM_NAME,
    INV_ATM_CATEGORY_INDEX,
    INV_ATM_CATEGORY_NAME,
    INV_ATM_CLASS,
    INV_ATM_PARAM_VALUE,
    /* CKMessageManager */
    INV_MSG_NAMES,
    INV_MSG_DEFAULT_SLOTS,
    INV_MSG_PARAM_VALUE,
    /* CKGroup */
    INV_GRP_MEMBER_CLASS,
    INV_GRP_MEMBER_UNIQUE,
    INV_GRP_SECTION,
    /* CKScene, including the level scene stored in a CKLevel */
    INV_SCN_DESC_CLASS,
    INV_SCN_DESC_UNIQUE,
    INV_SCN_DESC_FLAGS,
    INV_SCN_DESC_CHUNKS,
    INV_SCN_SCRIPTS_IN_SCENE,
    INV_SCN_LEVEL,
    INV_SCN_LEVEL_LISTS_SCENE,
    INV_SCN_BACKGROUND,
    INV_SCN_CAMERA,
    INV_SCN_FOG,
    INV_SCN_ENVIRONMENT,
    /* CKLevel */
    INV_LVL_SCENE_CLASS,
    INV_LVL_SCENE_UNIQUE,
    INV_LVL_SCENE_BACKREF,
    INV_LVL_CURRENT_SCENE,
    INV_LVL_LEVEL_SCENE,
    INV_LVL_SECTIONS,
    INV_LVL_SCENE_CHUNK,
    INV_LVL_SCENE_IDS,
    INV_LVL_SCENE_IDS_COMPLETE,
    /* CKDataArray */
    INV_DA_COLUMN_TYPE,
    INV_DA_PARAM_COLUMN,
    INV_DA_ROW_WIDTH,
    INV_DA_KEY_COLUMN,
    INV_DA_OBJECT_CELL,
    INV_DA_PARAM_CELL,
    /* CKAnimation / CKKeyedAnimation / CKCharacter */
    INV_ANIM_SECTIONS,
    INV_ANIM_VALUES,
    INV_ANIM_FLAGS,
    INV_ANIM_STEP,
    INV_ANIM_CHARACTER,
    INV_ANIM_ROOT,
    INV_CHARACTER_ANIMATIONS,
    INV_KEYED_LIST,
    INV_KEYED_PARENT,
    INV_KEYED_MERGED,
    INV_KEYED_LENGTH,
    /* CKObjectAnimation */
    INV_OBJANIM_LENGTH,
    INV_OBJANIM_FLAGS,
    INV_OBJANIM_ENTITY,
    INV_OBJANIM_MERGE,
    INV_OBJANIM_SHARED,
    INV_OBJANIM_CONTROLLER,
    INV_OBJANIM_BLOB,
    INV_OBJANIM_KEY_ORDER,
    INV_OBJANIM_QUATERNION,
    INV_OBJANIM_ROOT,
    /* CKSynchroObject / CKCriticalSectionObject / CKInterfaceObjectManager */
    INV_SYNCHRO,
    INV_CRITICAL_SECTION,
    INV_IOM,
    INV_COUNT
} invariant_id_t;

typedef struct invariant {
    size_t checked;
    size_t violated;
} invariant_t;

static const char *const invariant_names[INV_COUNT] = {
    [INV_REFS_RESOLVE] = "references the checks follow name an object of the file",
    [INV_OBJECT_VISIBILITY] = "object visibility: visible, hidden or hierarchically hidden",
    [INV_BEO_SCRIPT_CLASS] = "be-object scripts are behaviors",
    [INV_BEO_SCRIPT_UNIQUE] = "be-object lists a script once",
    [INV_BEO_SCRIPT_ORDER] = "be-object scripts are sorted by decreasing priority",
    [INV_BEO_PRIORITY] = "be-object data section is the priority flag and a priority",
    [INV_BEO_ACTIVITY_FLAGS] = "be-object single activity flags are scene object flags",
    [INV_BEO_ACTIVITY_FILE] = "single activity flags only in a file without scene or level",
    [INV_BEO_ATTR_SECTION] = "be-object attribute section holds attributes",
    [INV_BEO_ATTR_TYPE_SAVED] = "be-object attribute type is saved by the attribute manager",
    [INV_BEO_ATTR_UNIQUE] = "be-object holds an attribute type once",
    [INV_BEO_ATTR_CLASS] = "be-object class derives from the attribute's class",
    [INV_BEO_ATTR_PARAM] = "attribute has a parameter out iff its type has a parameter",
    [INV_BEO_ATTR_PARAM_TYPE] = "attribute parameter has the attribute's parameter type",
    [INV_BEO_ATTR_PARAM_UNIQUE] = "attribute parameter belongs to one attribute",
    [INV_ATM_SAVED_FLAGS] = "saved attribute and category flags",
    [INV_ATM_NAME] = "saved attribute names are unique, set, below 64 characters",
    [INV_ATM_CATEGORY_INDEX] = "attribute category index is none or a saved category",
    [INV_ATM_CATEGORY_NAME] = "saved category names are unique, set, savable",
    [INV_ATM_CLASS] = "attribute compatible class derives from CKObject",
    [INV_ATM_PARAM_VALUE] = "attribute-typed parameters hold a saved attribute index",
    [INV_MSG_NAMES] = "message type names are unique",
    [INV_MSG_DEFAULT_SLOTS] = "message slots 0 and 1 hold the default messages",
    [INV_MSG_PARAM_VALUE] = "message-typed parameters hold a saved message type",
    [INV_GRP_MEMBER_CLASS] = "group members are be-objects",
    [INV_GRP_MEMBER_UNIQUE] = "group lists a member once and never itself",
    [INV_GRP_SECTION] = "group section exists iff the group has members",
    [INV_SCN_DESC_CLASS] = "scene objects are scene objects, not scenes",
    [INV_SCN_DESC_UNIQUE] = "scene lists an object once",
    [INV_SCN_DESC_FLAGS] = "scene object flags: known bits, one start mode, no INTERNAL_IC",
    [INV_SCN_DESC_CHUNKS] = "scene object initial value is a chunk of its class, reserved is null",
    [INV_SCN_SCRIPTS_IN_SCENE] = "scene behaviors are scripts of an object of the scene",
    [INV_SCN_LEVEL] = "scene level is a level",
    [INV_SCN_LEVEL_LISTS_SCENE] = "scene level lists the scene (level scene: is its level)",
    [INV_SCN_BACKGROUND] = "scene background is a texture",
    [INV_SCN_CAMERA] = "scene starting camera is a camera of the scene",
    [INV_SCN_FOG] = "scene fog mode, range and density",
    [INV_SCN_ENVIRONMENT] = "scene environment settings are known bits",
    [INV_LVL_SCENE_CLASS] = "level scenes are scenes",
    [INV_LVL_SCENE_UNIQUE] = "level lists a scene once",
    [INV_LVL_SCENE_BACKREF] = "level scenes name the level",
    [INV_LVL_CURRENT_SCENE] = "current scene is a listed scene",
    [INV_LVL_LEVEL_SCENE] = "level scene id is null, the scene lives inside the level",
    [INV_LVL_SECTIONS] = "level legacy arrays empty, duplicate managers need inactive ones",
    [INV_LVL_SCENE_CHUNK] = "level scene chunk reads as a scene",
    [INV_LVL_SCENE_IDS] = "level scene chunk ids are objects",
    [INV_LVL_SCENE_IDS_COMPLETE] = "level scene chunk ids are the references of its scene",
    [INV_DA_COLUMN_TYPE] = "data array column type is a CK_ARRAYTYPE",
    [INV_DA_PARAM_COLUMN] = "data array parameter columns have a parameter type",
    [INV_DA_ROW_WIDTH] = "data array row has one cell per column",
    [INV_DA_KEY_COLUMN] = "data array key column is none or at most the column count",
    [INV_DA_OBJECT_CELL] = "data array object cells are objects",
    [INV_DA_PARAM_CELL] = "data array parameter cells have the column type",
    [INV_ANIM_SECTIONS] = "animation data, length, root, character, step all saved",
    [INV_ANIM_VALUES] = "animation length and frame rate are finite, not negative",
    [INV_ANIM_FLAGS] = "animation flags are CK_ANIMATION_FLAGS bits",
    [INV_ANIM_STEP] = "animation step is between 0 and 1",
    [INV_ANIM_CHARACTER] = "animation character is a character",
    [INV_ANIM_ROOT] = "animation root entity is an entity of the character",
    [INV_CHARACTER_ANIMATIONS] = "character and animations list each other, active one listed",
    [INV_KEYED_LIST] = "keyed animation lists object animations",
    [INV_KEYED_PARENT] = "object animation is listed by one keyed animation",
    [INV_KEYED_MERGED] = "keyed animation merged is a flag, merge factor finite",
    [INV_KEYED_LENGTH] = "keyed animation length is its object animations' length",
    [INV_OBJANIM_LENGTH] = "object animation length is finite, not negative",
    [INV_OBJANIM_FLAGS] = "object animation flags are CK_OBJECTANIMATION_FLAGS bits",
    [INV_OBJANIM_ENTITY] = "object animation entity is a 3d entity",
    [INV_OBJANIM_MERGE] = "object animation merge data iff merged, parts are animations",
    [INV_OBJANIM_SHARED] = "shared animation is another object animation with keys",
    [INV_OBJANIM_CONTROLLER] = "object animation controllers: known types, one per slot, in order",
    [INV_OBJANIM_BLOB] = "controller key count matches its blob size",
    [INV_OBJANIM_KEY_ORDER] = "controller key times are finite and do not decrease",
    [INV_OBJANIM_QUATERNION] = "rotation key quaternions have unit length",
    [INV_OBJANIM_ROOT] = "root vector: zero extra, root vector only on a root animation",
    [INV_SYNCHRO] = "synchro objects: waiters, arrived and passed objects",
    [INV_CRITICAL_SECTION] = "critical section holds a be-object or none",
    [INV_IOM] = "interface object manager: chunk list and guid",
};

/* Object ids with an object id attached, searched linearly: a file holds a few
 * thousand at most. */
typedef struct id_pairs {
    nmo_object_id_t *keys;
    nmo_object_id_t *values;
    size_t count;
    size_t capacity;
} id_pairs_t;

typedef struct corpus_semantics {
    nmo_context_t *ctx;
    const nmo_type_registry_t *types;
    nmo_arena_t *arena;

    /* The file being checked */
    const char *path;
    const nmo_object_t *current;
    nmo_object_repository_t *repository;
    uint32_t product_build;
    int file_has_scene_or_level;
    nmo_attributemanager_state_t attribute_manager;
    int has_attribute_manager;
    nmo_messagemanager_state_t message_manager;
    int has_message_manager;

    /* Per file: the keyed animation that lists each object animation, the
       be-object that holds each script, and the attribute parameters already
       claimed by an attribute */
    id_pairs_t animation_parents;
    id_pairs_t script_owners;
    id_pairs_t claimed_parameters;

    size_t files;
    size_t load_errors;
    size_t objects;
    size_t objects_without_state;

    invariant_t invariants[INV_COUNT];
} corpus_semantics_t;

#define STATE(type, object) ((const type *)nmo_object_get_state(object))

static const char *class_name(const corpus_semantics_t *stats, nmo_class_id_t class_id)
{
    const char *name = nmo_type_query_class_name_from_id(stats->types, class_id);
    return name != NULL ? name : "?";
}

static const char *object_class_name(const corpus_semantics_t *stats, const nmo_object_t *object)
{
    return object != NULL ? class_name(stats, object->class_id) : "null";
}

static void report(corpus_semantics_t *stats, invariant_id_t id, const char *format, ...)
{
    if (stats->invariants[id].violated > MAX_REPORTED_VIOLATIONS) {
        return;
    }
    printf("  %s: ", stats->path);
    if (stats->current != NULL) {
        printf("%s %u: ", class_name(stats, stats->current->class_id),
               (unsigned)stats->current->file_id);
    }
    printf("[%s] ", invariant_names[id]);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

#define CHECK(stats, id, condition, ...)                \
    do {                                                \
        (stats)->invariants[id].checked++;              \
        if (!(condition)) {                             \
            (stats)->invariants[id].violated++;         \
            report((stats), (id), __VA_ARGS__);         \
        }                                               \
    } while (0)

static const nmo_object_id_t *id_pairs_find(const id_pairs_t *pairs, nmo_object_id_t key)
{
    for (size_t i = 0; i < pairs->count; i++) {
        if (pairs->keys[i] == key) {
            return &pairs->values[i];
        }
    }
    return NULL;
}

static void id_pairs_add(id_pairs_t *pairs, nmo_object_id_t key, nmo_object_id_t value)
{
    if (pairs->count == pairs->capacity) {
        const size_t capacity = pairs->capacity != 0 ? pairs->capacity * 2 : 256;
        nmo_object_id_t *keys = realloc(pairs->keys, capacity * sizeof(*keys));
        if (keys == NULL) {
            return;
        }
        pairs->keys = keys;
        nmo_object_id_t *values = realloc(pairs->values, capacity * sizeof(*values));
        if (values == NULL) {
            return;
        }
        pairs->values = values;
        pairs->capacity = capacity;
    }
    pairs->keys[pairs->count] = key;
    pairs->values[pairs->count] = value;
    pairs->count++;
}

/* ---------------------------------------------------------------------------
 * Reference and class helpers
 * ------------------------------------------------------------------------- */

static int class_is(const corpus_semantics_t *stats, nmo_class_id_t class_id, nmo_class_id_t base)
{
    return class_id == base || nmo_type_query_class_is_derived_from(stats->types, class_id, base);
}

static int object_is(const corpus_semantics_t *stats, const nmo_object_t *object,
                     nmo_class_id_t base)
{
    return object != NULL && class_is(stats, object->class_id, base);
}

/* The object a reference names, NULL for a null reference. CKStateChunk::
 * WriteObjectID stores CKFile::SaveFindObjectIndex, which is -1 for an object
 * the file does not hold, and ReadObjectID turns -1 into null: a reference that
 * is not null must name an object of the file. */
static const nmo_object_t *ref_object(corpus_semantics_t *stats, const nmo_ref_t *ref)
{
    /* A sub-chunk the loader leaves as it was written still holds -1 for null. */
    if (ref->state == NMO_REF_NONE || nmo_ref_serialized_id(ref) == NMO_OBJECT_ID_INVALID) {
        return NULL;
    }
    const nmo_object_t *object = NULL;
    if (ref->state == NMO_REF_RESOLVED || ref->state == NMO_REF_CLASS_MISMATCH) {
        object = nmo_object_repository_find_by_id(stats->repository, ref->id);
    }
    CHECK(stats, INV_REFS_RESOLVE, object != NULL, "reference %u (state %d) names no object",
          (unsigned)ref->raw_id, (int)ref->state);
    return object;
}

static int ref_is_null(const nmo_ref_t *ref)
{
    return ref->state == NMO_REF_NONE;
}

static int refs_equal(const nmo_ref_t *a, const nmo_ref_t *b)
{
    return a->state != NMO_REF_NONE && a->state == b->state &&
           nmo_ref_serialized_id(a) == nmo_ref_serialized_id(b);
}

static int ref_names(const nmo_ref_t *ref, nmo_object_id_t id)
{
    return nmo_ref_runtime_id(ref) == id;
}

/* ---------------------------------------------------------------------------
 * Managers of the file
 * ------------------------------------------------------------------------- */

static const nmo_attribute_descriptor_t *attribute_by_index(const corpus_semantics_t *stats,
                                                            uint32_t index)
{
    if (!stats->has_attribute_manager || index >= stats->attribute_manager.attribute_count ||
        stats->attribute_manager.attributes == NULL) {
        return NULL;
    }
    return &stats->attribute_manager.attributes[index];
}

static void load_managers(corpus_semantics_t *stats, nmo_session_t *session)
{
    const nmo_file_state_t *file_state = nmo_session_get_file_state(session);
    nmo_deserialize_context_t context =
        nmo_deserialize_context_create(stats->arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    memset(&stats->attribute_manager, 0, sizeof(stats->attribute_manager));
    memset(&stats->message_manager, 0, sizeof(stats->message_manager));
    stats->has_attribute_manager = 0;
    stats->has_message_manager = 0;
    if (file_state == NULL) {
        return;
    }
    for (uint32_t i = 0; i < file_state->manager_data_count; i++) {
        const nmo_manager_data_t *manager = &file_state->manager_data[i];
        if (manager->chunk == NULL) {
            continue;
        }
        nmo_chunk_t *chunk = nmo_chunk_clone(manager->chunk, stats->arena);
        if (chunk == NULL || nmo_chunk_start_read(chunk) != NMO_OK) {
            continue;
        }
        if (nmo_guid_equals(manager->guid, NMO_MANAGER_GUID_ATTRIBUTE)) {
            stats->has_attribute_manager = nmo_attributemanager_deserialize(
                                               &stats->attribute_manager, chunk, NULL, &context) == NMO_OK;
        } else if (nmo_guid_equals(manager->guid, NMO_MANAGER_GUID_MESSAGE)) {
            stats->has_message_manager = nmo_messagemanager_deserialize(
                                             &stats->message_manager, chunk, NULL, &context) == NMO_OK;
        }
    }
}

#define CK_ATTRIBUT_KNOWN_BITS 0x7Fu

static void check_attribute_manager(corpus_semantics_t *stats)
{
    if (!stats->has_attribute_manager) {
        return;
    }
    const nmo_attributemanager_state_t *manager = &stats->attribute_manager;
    for (uint32_t i = 0; i < manager->category_count; i++) {
        const nmo_attribute_category_t *category = &manager->categories[i];
        if (!category->present) {
            continue;
        }
        /* CKAttributeManager::SaveData clears the used bit of a DONOTSAVE
           category; AddCategory returns the index of an existing name. */
        int unique = category->name != NULL && category->name[0] != '\0';
        for (uint32_t j = 0; j < i && unique; j++) {
            if (manager->categories[j].present && manager->categories[j].name != NULL &&
                strcmp(manager->categories[j].name, category->name) == 0) {
                unique = 0;
            }
        }
        CHECK(stats, INV_ATM_CATEGORY_NAME,
              unique && (category->flags & CK_ATTRIBUT_DONOTSAVE) == 0,
              "category %u \"%s\" flags %#x", i,
              category->name != NULL ? category->name : "(null)", (unsigned)category->flags);
        CHECK(stats, INV_ATM_SAVED_FLAGS, (category->flags & ~CK_ATTRIBUT_KNOWN_BITS) == 0,
              "category %u flags %#x", i, (unsigned)category->flags);
    }
    for (uint32_t i = 0; i < manager->attribute_count; i++) {
        const nmo_attribute_descriptor_t *attribute = &manager->attributes[i];
        if (!attribute->present) {
            continue;
        }
        const char *name = attribute->name != NULL ? attribute->name : "(null)";

        /* SaveData skips an attribute flagged DONOTSAVE, and
           RegisterNewAttributeType gives every attribute the user or system bit. */
        CHECK(stats, INV_ATM_SAVED_FLAGS,
              (attribute->flags & CK_ATTRIBUT_DONOTSAVE) == 0 &&
                  (attribute->flags & (CK_ATTRIBUT_USER | CK_ATTRIBUT_SYSTEM)) != 0 &&
                  (attribute->flags & ~CK_ATTRIBUT_KNOWN_BITS) == 0,
              "attribute %u \"%s\" flags %#x", i, name, (unsigned)attribute->flags);

        /* RegisterNewAttributeType keeps 63 characters of a name and returns
           the existing index when the name is registered. */
        int name_ok = attribute->name != NULL && attribute->name[0] != '\0' &&
                      strlen(attribute->name) <= 63;
        for (uint32_t j = 0; j < i && name_ok; j++) {
            if (manager->attributes[j].present && manager->attributes[j].name != NULL &&
                strcmp(manager->attributes[j].name, attribute->name) == 0) {
                name_ok = 0;
            }
        }
        CHECK(stats, INV_ATM_NAME, name_ok, "attribute %u name \"%s\"", i, name);

        /* SaveData marks the category of each saved attribute as used (a
           DONOTSAVE category excepted) and writes it at its in-memory index. */
        const int32_t category_index = attribute->category_index;
        CHECK(stats, INV_ATM_CATEGORY_INDEX,
              category_index == -1 ||
                  (category_index >= 0 && (uint32_t)category_index < manager->category_count &&
                   manager->categories[category_index].present),
              "attribute %u \"%s\" category index %d of %u", i, name, (int)category_index,
              (unsigned)manager->category_count);

        /* SetAttribute applies an attribute to the objects of its class
           (CKIsChildClassOf), and CKBeObject::Load rejects a class outside CKObject. */
        CHECK(stats, INV_ATM_CLASS,
              attribute->compatible_class_id > 0 &&
                  class_is(stats, (nmo_class_id_t)attribute->compatible_class_id, NMO_CID_OBJECT),
              "attribute %u \"%s\" compatible class %d", i, name,
              (int)attribute->compatible_class_id);
    }
}

static void check_message_manager(corpus_semantics_t *stats)
{
    if (!stats->has_message_manager) {
        return;
    }
    const nmo_messagemanager_state_t *manager = &stats->message_manager;
    for (uint32_t i = 0; i < manager->message_type_count; i++) {
        const char *name = manager->message_type_names[i];
        if (name == NULL || name[0] == '\0') {
            continue; /* SaveData writes an unused slot as an empty string */
        }
        /* The constructor (RegisterDefaultMessages) registers OnClick and
           OnDblClick first, so they are slots 0 and 1 and nothing else is. */
        const int is_click = strcmp(name, "OnClick") == 0;
        const int is_double_click = strcmp(name, "OnDblClick") == 0;
        CHECK(stats, INV_MSG_DEFAULT_SLOTS,
              (i != 0 || is_click) && (i != 1 || is_double_click) && (!is_click || i == 0) &&
                  (!is_double_click || i == 1),
              "message type %u is \"%s\"", i, name);
        /* CKMessageManager::AddMessageType returns the index of an existing name. */
        int unique = 1;
        for (uint32_t j = 0; j < i && unique; j++) {
            const char *other = manager->message_type_names[j];
            unique = other == NULL || strcmp(other, name) != 0;
        }
        CHECK(stats, INV_MSG_NAMES, unique, "message type %u \"%s\" repeats", i, name);
    }
}

/* A parameter of the attribute type or the message type holds a manager int:
 * the index CKAttributeManager::SaveData / CKMessageManager::SaveData collect
 * from the parameters of the file, and then save (a message with its name). */
static void check_manager_parameter(corpus_semantics_t *stats,
                                    const nmo_parameter_state_t *parameter)
{
    if (parameter->mode != CKPARAM_MODE_MANAGER) {
        return;
    }
    const int32_t value = (int32_t)parameter->manager_value;
    if (value < 0) {
        return; /* SaveData ignores a negative value: none */
    }
    if (nmo_guid_equals(parameter->type_guid, CKPGUID_ATTRIBUTE) && stats->has_attribute_manager) {
        const nmo_attribute_descriptor_t *descriptor = attribute_by_index(stats, (uint32_t)value);
        CHECK(stats, INV_ATM_PARAM_VALUE, descriptor != NULL && descriptor->present,
              "attribute parameter holds %d of %u", (int)value,
              (unsigned)stats->attribute_manager.attribute_count);
    } else if (nmo_guid_equals(parameter->type_guid, CKPGUID_MESSAGE) &&
               stats->has_message_manager) {
        const nmo_messagemanager_state_t *manager = &stats->message_manager;
        CHECK(stats, INV_MSG_PARAM_VALUE,
              (uint32_t)value < manager->message_type_count &&
                  manager->message_type_names[value] != NULL &&
                  manager->message_type_names[value][0] != '\0',
              "message parameter holds %d of %u", (int)value,
              (unsigned)manager->message_type_count);
    }
}

/* ---------------------------------------------------------------------------
 * CKObject / CKBeObject
 * ------------------------------------------------------------------------- */

#define SCENEOBJECT_KNOWN_BITS                                                      \
    ((uint32_t)(CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_ACTIVE |            \
                CK_SCENEOBJECT_START_DEACTIVATE | CK_SCENEOBJECT_START_LEAVE |     \
                CK_SCENEOBJECT_START_RESET | CK_SCENEOBJECT_INTERNAL_IC))

#define SCENEOBJECT_START_MODES                                                     \
    ((uint32_t)(CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_DEACTIVATE |  \
                CK_SCENEOBJECT_START_LEAVE))

/* The three start modes (activate, deactivate, leave) exclude one another:
 * CKScene::Init takes the first as `active` and the last as `doNothing`, and
 * CKSceneObjectDesc::ReadState sets exactly one of the first two. The engine
 * keeps whatever SetObjectFlags was given in the high word (two level scenes
 * of the corpus carry 1 to 4 there), so only the low word is checked. */
static int scene_object_flags_ok(uint32_t flags)
{
    const uint32_t start = flags & SCENEOBJECT_START_MODES;
    return (flags & 0xFFFFu & ~SCENEOBJECT_KNOWN_BITS) == 0 && (start & (start - 1)) == 0;
}

/* Returns 1 for the first claim of a parameter. */
static int claim_parameter(corpus_semantics_t *stats, nmo_object_id_t parameter)
{
    if (id_pairs_find(&stats->claimed_parameters, parameter) != NULL) {
        return 0;
    }
    id_pairs_add(&stats->claimed_parameters, parameter, 0);
    return 1;
}

static void check_scripts(corpus_semantics_t *stats, const nmo_beobject_state_t *state)
{
    /* CKBeObject::AddScript takes a CKBehavior and refuses one it holds, then
       SortScripts orders the scripts by BehaviorPrioritySort: the highest
       priority first (CKBehavior::SetPriority sorts again). */
    const nmo_ref_t *scripts = NMO_ARRAY_DATA(nmo_ref_t, &state->scripts);
    const nmo_behavior_state_t *previous = NULL;
    for (size_t i = 0; i < state->scripts.count; i++) {
        const nmo_object_t *script = ref_object(stats, &scripts[i]);
        if (script != NULL) {
            CHECK(stats, INV_BEO_SCRIPT_CLASS, object_is(stats, script, NMO_CID_BEHAVIOR),
                  "script %zu is a %s", i, class_name(stats, script->class_id));
            const nmo_behavior_state_t *behavior = STATE(nmo_behavior_state_t, script);
            if (behavior != NULL && object_is(stats, script, NMO_CID_BEHAVIOR)) {
                if (previous != NULL) {
                    CHECK(stats, INV_BEO_SCRIPT_ORDER, previous->priority >= behavior->priority,
                          "script %zu has priority %d after %d", i, (int)behavior->priority,
                          (int)previous->priority);
                }
                previous = behavior;
            }
        }
        int unique = 1;
        for (size_t j = 0; j < i && unique; j++) {
            unique = !refs_equal(&scripts[j], &scripts[i]);
        }
        CHECK(stats, INV_BEO_SCRIPT_UNIQUE, unique, "script %zu repeats", i);
    }
}

/* CKBeObject::SetAttribute holds one entry per type, only for a class
 * compatible with the type, with a parameter made by CreateCKParameterOut from
 * the type's parameter type when it has one. */
static void check_attributes(corpus_semantics_t *stats, const nmo_beobject_state_t *state,
                             nmo_class_id_t class_id)
{
    const nmo_beobject_attribute_t *attributes =
        NMO_ARRAY_DATA(nmo_beobject_attribute_t, &state->attributes);
    /* CKBeObject::Save writes NEWATTRIBUTES only for a non-empty list. */
    if (state->has_attributes_section) {
        CHECK(stats, INV_BEO_ATTR_SECTION, state->attributes.count > 0, "empty attribute section");
    }
    for (size_t i = 0; i < state->attributes.count; i++) {
        const nmo_beobject_attribute_t *entry = &attributes[i];
        /* SaveData saves every attribute that an object of the file holds. */
        const nmo_attribute_descriptor_t *descriptor = attribute_by_index(stats, entry->type_id);
        CHECK(stats, INV_BEO_ATTR_TYPE_SAVED, descriptor != NULL && descriptor->present,
              "attribute type %u of %u", (unsigned)entry->type_id,
              (unsigned)stats->attribute_manager.attribute_count);
        int unique = 1;
        for (size_t j = 0; j < i && unique; j++) {
            unique = attributes[j].type_id != entry->type_id;
        }
        CHECK(stats, INV_BEO_ATTR_UNIQUE, unique, "attribute type %u repeats",
              (unsigned)entry->type_id);
        if (descriptor == NULL || !descriptor->present) {
            continue;
        }
        CHECK(stats, INV_BEO_ATTR_CLASS,
              class_is(stats, class_id, (nmo_class_id_t)descriptor->compatible_class_id),
              "attribute \"%s\" needs class %d", descriptor->name,
              (int)descriptor->compatible_class_id);

        const nmo_object_t *parameter = ref_object(stats, &entry->parameter);
        const int typed = !nmo_guid_is_null(descriptor->parameter_type_guid);
        CHECK(stats, INV_BEO_ATTR_PARAM,
              typed ? (parameter != NULL && object_is(stats, parameter, NMO_CID_PARAMETEROUT))
                    : ref_is_null(&entry->parameter),
              "attribute \"%s\" %s, its parameter is %s", descriptor->name,
              typed ? "has a parameter type" : "has none", object_class_name(stats, parameter));
        if (parameter == NULL) {
            continue;
        }
        const nmo_parameter_state_t *parameter_state = STATE(nmo_parameter_state_t, parameter);
        if (parameter_state != NULL) {
            const nmo_guid_t wanted = descriptor->parameter_type_guid;
            CHECK(stats, INV_BEO_ATTR_PARAM_TYPE,
                  nmo_guid_equals(parameter_state->type_guid, wanted),
                  "attribute \"%s\" wants %08x-%08x, parameter is %08x-%08x", descriptor->name,
                  (unsigned)wanted.d1, (unsigned)wanted.d2, (unsigned)parameter_state->type_guid.d1,
                  (unsigned)parameter_state->type_guid.d2);
        }
        /* Each SetAttribute makes a parameter of its own. */
        CHECK(stats, INV_BEO_ATTR_PARAM_UNIQUE, claim_parameter(stats, parameter->id),
              "attribute \"%s\" shares parameter %u", descriptor->name,
              (unsigned)parameter->file_id);
    }
}

static void check_beobject(corpus_semantics_t *stats, const nmo_beobject_state_t *state,
                           nmo_class_id_t class_id)
{
    /* CKBeObject::Save writes DATAS only for a non-zero priority, as the
       priority flag then the priority; Load reads the priority on that flag. */
    if (state->has_data_section && !state->data_is_legacy && !state->has_runtime_data_section) {
        CHECK(stats, INV_BEO_PRIORITY, (state->data_flags & 0x10000000u) != 0 && state->priority != 0,
              "data flags %#x priority %d", (unsigned)state->data_flags, (int)state->priority);
    }
    /* CKBeObject::Save writes the scene flags of the object's description in
       the current scene, with INTERNAL_IC when it has an initial value ... */
    if (state->has_single_activity) {
        CHECK(stats, INV_BEO_ACTIVITY_FLAGS, scene_object_flags_ok(state->single_activity_flags),
              "flags %#x", (unsigned)state->single_activity_flags);
        /* ... and only when no scene was saved: CKFile::SaveObject sets
           m_SceneSaved for a CKScene or a CKLevel, and Save tests it. */
        CHECK(stats, INV_BEO_ACTIVITY_FILE, !stats->file_has_scene_or_level,
              "the file holds a scene or a level");
    }
    check_scripts(stats, state);
    check_attributes(stats, state, class_id);
}

static void check_object_common(corpus_semantics_t *stats, const nmo_object_t *object,
                                const void *state)
{
    const nmo_object_state_t *base = (const nmo_object_state_t *)state;
    /* CKObject::Save writes OBJECTHIERAHIDDEN or OBJECTHIDDEN, never both;
       Load turns them into hierarchically hidden, hidden or visible. */
    CHECK(stats, INV_OBJECT_VISIBILITY,
          base->visibility_flags == 0 || base->visibility_flags == NMO_CKOBJECT_VISIBLE ||
              base->visibility_flags == NMO_CKOBJECT_HIERARCHICAL,
          "visibility flags %#x", (unsigned)base->visibility_flags);

    if (object_is(stats, object, NMO_CID_BEOBJECT)) {
        check_beobject(stats, (const nmo_beobject_state_t *)state, object->class_id);
    }
}

/* ---------------------------------------------------------------------------
 * CKGroup
 * ------------------------------------------------------------------------- */

static void check_group(corpus_semantics_t *stats, const nmo_object_t *object,
                        const nmo_group_state_t *group)
{
    const nmo_ref_t *members = NMO_ARRAY_DATA(nmo_ref_t, &group->object_ids);
    /* CKGroup::Save writes GROUPALL only for a non-empty member array. */
    CHECK(stats, INV_GRP_SECTION, (group->has_group_data != 0) == (group->object_ids.count > 0),
          "section %d, %zu members", (int)group->has_group_data, group->object_ids.count);
    for (size_t i = 0; i < group->object_ids.count; i++) {
        /* CKGroup::AddObject and PostLoad keep only be-objects, null ones not. */
        const nmo_object_t *member = ref_object(stats, &members[i]);
        CHECK(stats, INV_GRP_MEMBER_CLASS, object_is(stats, member, NMO_CID_BEOBJECT),
              "member %zu is %s", i, object_class_name(stats, member));
        /* CKGroup::AddObject refuses the group itself and an object it holds. */
        int unique = member != object;
        for (size_t j = 0; j < i && unique; j++) {
            unique = !refs_equal(&members[j], &members[i]);
        }
        CHECK(stats, INV_GRP_MEMBER_UNIQUE, unique, "member %zu repeats or is the group", i);
    }
}

/* ---------------------------------------------------------------------------
 * CKScene / CKLevel
 * ------------------------------------------------------------------------- */

static int scene_has_object(const nmo_scene_state_t *scene, nmo_object_id_t id)
{
    const nmo_scene_object_desc_t *descs =
        NMO_ARRAY_DATA(nmo_scene_object_desc_t, &scene->object_descs);
    for (size_t i = 0; i < scene->object_descs.count; i++) {
        if (ref_names(&descs[i].ref, id)) {
            return 1;
        }
    }
    return 0;
}

static int level_lists_scene(const nmo_level_state_t *level, nmo_object_id_t scene)
{
    const nmo_ref_t *scenes = NMO_ARRAY_DATA(nmo_ref_t, &level->scene_ids);
    for (size_t i = 0; i < level->scene_ids.count; i++) {
        if (ref_names(&scenes[i], scene)) {
            return 1;
        }
    }
    return 0;
}

/* NULL when a behavior of a scene is described as the engine does: a script
 * (CKBEHAVIOR_SCRIPT) with an owner, which CKBeObject::AddToScene added next
 * to it, so the owner is in the scene (a scene also holds its own scripts). */
static const char *script_problem(const corpus_semantics_t *stats, const nmo_scene_state_t *scene,
                                  nmo_object_id_t scene_id, const nmo_object_t *behavior_object)
{
    const nmo_behavior_state_t *behavior = STATE(nmo_behavior_state_t, behavior_object);
    const nmo_object_id_t *owner = id_pairs_find(&stats->script_owners, behavior_object->id);
    if (behavior == NULL || (behavior->flags & CKBEHAVIOR_SCRIPT) == 0) {
        return "it is not a script";
    }
    if (owner == NULL) {
        return "it has no owner";
    }
    if (*owner != scene_id && !scene_has_object(scene, *owner)) {
        return "its owner is not in the scene";
    }
    return NULL;
}

static void check_scene_object(corpus_semantics_t *stats, const nmo_object_t *object,
                               const nmo_scene_state_t *scene, size_t index)
{
    const nmo_scene_object_desc_t *descs =
        NMO_ARRAY_DATA(nmo_scene_object_desc_t, &scene->object_descs);
    const nmo_scene_object_desc_t *desc = &descs[index];
    const nmo_object_t *target = ref_object(stats, &desc->ref);
    if (target != NULL) {
        /* CKScene::Load keeps a description only for a CKSceneObject and
           CKScene::AddObject refuses a scene. */
        CHECK(stats, INV_SCN_DESC_CLASS,
              object_is(stats, target, NMO_CID_SCENEOBJECT) && target->class_id != NMO_CID_SCENE,
              "object %zu is a %s", index, class_name(stats, target->class_id));
        if (object_is(stats, target, NMO_CID_BEHAVIOR)) {
            const char *problem = script_problem(stats, scene, object->id, target);
            CHECK(stats, INV_SCN_SCRIPTS_IN_SCENE, problem == NULL, "behavior %u: %s",
                  (unsigned)target->file_id, problem != NULL ? problem : "");
        }
    }
    /* The descriptions live in a hash table keyed by object id. */
    int unique = 1;
    for (size_t j = 0; j < index && unique; j++) {
        unique = !refs_equal(&descs[j].ref, &desc->ref);
    }
    CHECK(stats, INV_SCN_DESC_UNIQUE, unique, "object %zu repeats", index);
    /* CKScene::AddObject strips INTERNAL_IC, which only carries an initial
       value in the single activity flags of a be-object. */
    CHECK(stats, INV_SCN_DESC_FLAGS,
          scene_object_flags_ok(desc->flags) && (desc->flags & CK_SCENEOBJECT_INTERNAL_IC) == 0,
          "object %zu flags %#x", index, (unsigned)desc->flags);
    /* CKScene::Save writes the saved state of the object (a chunk of its class,
       or of an ancestor that implements Save) and a null second chunk. */
    CHECK(stats, INV_SCN_DESC_CHUNKS,
          desc->reserved == NULL &&
              (desc->initial_value == NULL ||
               (target != NULL && class_is(stats, target->class_id, desc->initial_value->class_id))),
          "object %zu: initial value of class %u for a %s, reserved chunk %s", index,
          desc->initial_value != NULL ? (unsigned)desc->initial_value->class_id : 0u,
          object_class_name(stats, target), desc->reserved != NULL ? "set" : "null");
}

/* The scene `scene` of `object`. A level scene (`owner` is its level) is
 * stored inside the level; its reference to the level is the level itself. */
static void check_scene(corpus_semantics_t *stats, const nmo_object_t *object,
                        const nmo_scene_state_t *scene, const nmo_object_t *owner)
{
    for (size_t i = 0; i < scene->object_descs.count; i++) {
        check_scene_object(stats, object, scene, i);
    }

    /* CKScene::SetLevel takes a CKLevel; CKLevel::AddScene sets it and lists
       the scene, CKLevel::CreateLevelScene sets the level itself. */
    const nmo_object_t *level = ref_object(stats, &scene->level);
    if (level != NULL) {
        CHECK(stats, INV_SCN_LEVEL, object_is(stats, level, NMO_CID_LEVEL), "level is a %s",
              class_name(stats, level->class_id));
        const nmo_level_state_t *level_state = STATE(nmo_level_state_t, level);
        if (owner != NULL) {
            CHECK(stats, INV_SCN_LEVEL_LISTS_SCENE, level == owner,
                  "level scene names level %u, not %u", (unsigned)level->file_id,
                  (unsigned)owner->file_id);
        } else if (object_is(stats, level, NMO_CID_LEVEL)) {
            CHECK(stats, INV_SCN_LEVEL_LISTS_SCENE,
                  level_state != NULL && level_lists_scene(level_state, object->id),
                  "level %u does not list the scene", (unsigned)level->file_id);
        }
    }

    /* CKScene::SetBackgroundTexture takes a CKTexture. */
    const nmo_object_t *background = ref_object(stats, &scene->background_texture);
    if (background != NULL) {
        CHECK(stats, INV_SCN_BACKGROUND, object_is(stats, background, NMO_CID_TEXTURE),
              "background is a %s", class_name(stats, background->class_id));
    }
    /* CKScene::SetStartingCamera takes a CKCamera, and only one in the scene. */
    const nmo_object_t *camera = ref_object(stats, &scene->starting_camera);
    if (camera != NULL) {
        const int in_scene = scene_has_object(scene, camera->id);
        CHECK(stats, INV_SCN_CAMERA, object_is(stats, camera, NMO_CID_CAMERA) && in_scene,
              "camera is a %s%s", class_name(stats, camera->class_id),
              in_scene ? "" : " outside the scene");
    }

    /* A VXFOG_MODE, a density that is not negative, and for linear fog a range
       that runs up (domain checks). CKScene::Load turns EXP and EXP2 into
       LINEAR in a file of a product build below 0x2010000 and libnmo keeps the
       mode of the file, so the range is judged with the mode the engine ends
       up with. */
    uint32_t fog_mode = scene->fog_mode;
    if (stats->product_build < 0x2010000u && (fog_mode == VXFOG_EXP || fog_mode == VXFOG_EXP2)) {
        fog_mode = VXFOG_LINEAR;
    }
    CHECK(stats, INV_SCN_FOG,
          scene->fog_mode <= VXFOG_LINEAR && isfinite(scene->fog_start) &&
              isfinite(scene->fog_end) && isfinite(scene->fog_density) &&
              scene->fog_density >= 0.0f &&
              (fog_mode != VXFOG_LINEAR || scene->fog_start <= scene->fog_end),
          "fog mode %u (engine %u) start %g end %g density %g", (unsigned)scene->fog_mode,
          (unsigned)fog_mode, scene->fog_start, scene->fog_end, scene->fog_density);
    /* CKScene::EnvironmentSettings tests CK_SCENE_USEENVIRONMENTSETTINGS and
       CKScene::Init sets CK_SCENE_LAUNCHEDONCE. */
    CHECK(stats, INV_SCN_ENVIRONMENT,
          (scene->environment_settings &
           ~(uint32_t)(CK_SCENE_LAUNCHEDONCE | CK_SCENE_USEENVIRONMENTSETTINGS)) == 0,
          "settings %#x", (unsigned)scene->environment_settings);

    check_beobject(stats, &scene->base, NMO_CID_SCENE);
}

/* The object ids a level scene chunk holds, as runtime ids. */
typedef struct level_scene_ids {
    uint32_t *values;
    size_t count;
    size_t capacity;
} level_scene_ids_t;

static void level_scene_ids_add(level_scene_ids_t *ids, uint32_t value)
{
    if (ids->count == ids->capacity) {
        const size_t capacity = ids->capacity != 0 ? ids->capacity * 2 : 256;
        uint32_t *grown = realloc(ids->values, capacity * sizeof(*grown));
        if (grown == NULL) {
            return;
        }
        ids->values = grown;
        ids->capacity = capacity;
    }
    ids->values[ids->count++] = value;
}

/* The level scene is a chunk of CKScene format inside the level, carrying no
 * file mapping. Where the loader found its object ids (level_scene_id_positions)
 * it already put runtime ids there. Otherwise the chunk tracks its ids itself
 * and they are the file's object ids, which are turned into runtime ids here
 * so that the chunk reads as it was loaded. */
static void level_scene_ids_to_runtime(const corpus_semantics_t *stats, nmo_chunk_t *chunk,
                                       level_scene_ids_t *out)
{
    uint32_t *data = (uint32_t *)chunk->data.data;
    const uint32_t *ids = (const uint32_t *)chunk->ids.data;
    for (size_t i = 0; i < chunk->ids.count; i++) {
        size_t first = ids[i];
        size_t count = 1;
        if (ids[i] == 0xFFFFFFFFu && i + 1 < chunk->ids.count) {
            const size_t header = ids[++i];
            if (header >= chunk->data.count) {
                continue;
            }
            first = header + 1;
            count = data[header];
        }
        for (size_t k = 0; k < count && first + k < chunk->data.count; k++) {
            uint32_t *id = &data[first + k];
            if (*id == 0 || *id == 0xFFFFFFFFu) {
                *id = 0;
                continue;
            }
            const nmo_object_t *object =
                nmo_object_repository_find_by_file_id(stats->repository, *id);
            *id = object != NULL ? object->id : 0xFFFFFFFEu;
            level_scene_ids_add(out, *id);
        }
    }
}

static int read_level_scene(corpus_semantics_t *stats, const nmo_level_state_t *level,
                            nmo_scene_state_t *out_scene, level_scene_ids_t *out_ids)
{
    if (level->level_scene_chunk == NULL) {
        return 0;
    }
    nmo_chunk_t *chunk = nmo_chunk_clone(level->level_scene_chunk, stats->arena);
    if (chunk == NULL) {
        return 0;
    }
    chunk->file_context = NULL;
    if (level->level_scene_id_count == 0) {
        level_scene_ids_to_runtime(stats, chunk, out_ids);
    } else {
        const uint32_t *data = (const uint32_t *)chunk->data.data;
        for (uint32_t i = 0; i < level->level_scene_id_count; i++) {
            const uint32_t position = level->level_scene_id_positions[i];
            level_scene_ids_add(out_ids, position < chunk->data.count ? data[position] : 0xFFFFFFFEu);
        }
    }
    if (nmo_chunk_start_read(chunk) != NMO_OK ||
        nmo_scene_vtable.create(out_scene, NULL, NULL) != NMO_OK) {
        return 0;
    }
    nmo_deserialize_context_t context = nmo_deserialize_context_create(
        stats->arena, stats->repository, nmo_context_get_type_runtime(stats->ctx),
        NMO_DESER_FLAG_FILE_MODE);
    if (nmo_scene_deserialize(out_scene, chunk, NULL, &context) != NMO_OK) {
        nmo_scene_vtable.destroy(out_scene, NULL, NULL);
        return 0;
    }
    return 1;
}

static int compare_ids(const void *a, const void *b)
{
    const uint32_t x = *(const uint32_t *)a;
    const uint32_t y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

/* A non-null reference of a scene read from the chunk; -1 is null as well. */
static void scene_reference_add(level_scene_ids_t *out, const nmo_ref_t *ref)
{
    const nmo_object_id_t id = nmo_ref_runtime_id(ref);
    if (id != NMO_OBJECT_ID_NONE && id != NMO_OBJECT_ID_INVALID) {
        level_scene_ids_add(out, id);
    }
}

/* The non-null references a scene holds, in no particular order. */
static void scene_reference_ids(const nmo_scene_state_t *scene, level_scene_ids_t *out)
{
    scene_reference_add(out, &scene->level);
    scene_reference_add(out, &scene->background_texture);
    scene_reference_add(out, &scene->starting_camera);
    const nmo_scene_object_desc_t *descs =
        NMO_ARRAY_DATA(nmo_scene_object_desc_t, &scene->object_descs);
    for (size_t i = 0; i < scene->object_descs.count; i++) {
        scene_reference_add(out, &descs[i].ref);
    }
    const nmo_ref_t *scripts = NMO_ARRAY_DATA(nmo_ref_t, &scene->base.scripts);
    for (size_t i = 0; i < scene->base.scripts.count; i++) {
        scene_reference_add(out, &scripts[i]);
    }
    const nmo_beobject_attribute_t *attributes =
        NMO_ARRAY_DATA(nmo_beobject_attribute_t, &scene->base.attributes);
    for (size_t i = 0; i < scene->base.attributes.count; i++) {
        scene_reference_add(out, &attributes[i].parameter);
    }
}

/* The ids the loader tracks in the level scene chunk are the references the
 * scene reads from it, no more and no fewer: saving writes exactly those as
 * the object ids of the file it writes. */
static void check_level_scene_ids(corpus_semantics_t *stats, const nmo_scene_state_t *scene,
                                  level_scene_ids_t *tracked)
{
    for (size_t i = 0; i < tracked->count; i++) {
        const uint32_t id = tracked->values[i];
        CHECK(stats, INV_LVL_SCENE_IDS,
              nmo_object_repository_find_by_id(stats->repository, id) != NULL,
              "level scene id %zu is %u", i, (unsigned)id);
    }
    level_scene_ids_t read = {0};
    scene_reference_ids(scene, &read);
    size_t tracked_ids = 0;
    for (size_t i = 0; i < tracked->count; i++) {
        tracked->values[tracked_ids] = tracked->values[i];
        tracked_ids += tracked->values[i] != NMO_OBJECT_ID_NONE;
    }
    int same = tracked_ids == read.count;
    if (same && read.count > 0) {
        qsort(tracked->values, tracked_ids, sizeof(uint32_t), compare_ids);
        qsort(read.values, read.count, sizeof(uint32_t), compare_ids);
        same = memcmp(tracked->values, read.values, read.count * sizeof(uint32_t)) == 0;
    }
    CHECK(stats, INV_LVL_SCENE_IDS_COMPLETE, same,
          "%zu ids tracked, the scene reads %zu references", tracked_ids, read.count);
    free(read.values);
}

static void check_level_scenes(corpus_semantics_t *stats, const nmo_object_t *object,
                               const nmo_level_state_t *level)
{
    const nmo_ref_t *scenes = NMO_ARRAY_DATA(nmo_ref_t, &level->scene_ids);
    for (size_t i = 0; i < level->scene_ids.count; i++) {
        const nmo_object_t *scene = ref_object(stats, &scenes[i]);
        if (scene != NULL) {
            CHECK(stats, INV_LVL_SCENE_CLASS, scene->class_id == NMO_CID_SCENE,
                  "scene %zu is a %s", i, class_name(stats, scene->class_id));
            /* CKLevel::AddScene calls CKScene::SetLevel. */
            const nmo_scene_state_t *scene_state = STATE(nmo_scene_state_t, scene);
            CHECK(stats, INV_LVL_SCENE_BACKREF,
                  scene_state != NULL && ref_names(&scene_state->level, object->id),
                  "scene %zu names another level", i);
        }
        /* CKLevel::AddScene and Load (AddIfNotHere) never repeat a scene. */
        int unique = 1;
        for (size_t j = 0; j < i && unique; j++) {
            unique = !refs_equal(&scenes[j], &scenes[i]);
        }
        CHECK(stats, INV_LVL_SCENE_UNIQUE, unique, "scene %zu repeats", i);
    }
    /* CKLevel::LaunchScene sets the current scene to a scene of the level; the
       level scene is not an object of the file, so the id is then null. */
    const nmo_object_t *current = ref_object(stats, &level->current_scene);
    if (current != NULL) {
        CHECK(stats, INV_LVL_CURRENT_SCENE, level_lists_scene(level, current->id),
              "current scene %u is not listed", (unsigned)current->file_id);
    }
}

static void check_level(corpus_semantics_t *stats, const nmo_object_t *object,
                        const nmo_level_state_t *level)
{
    /* CKLevel::Save writes two empty legacy arrays before the scene list, and
       the duplicate manager names only after the inactive manager guids. */
    CHECK(stats, INV_LVL_SECTIONS,
          level->legacy_object_ids.count == 0 && level->legacy_pointer_ids.count == 0 &&
              (!level->has_duplicate_manager_section || level->has_inactive_manager_section),
          "%zu + %zu legacy ids, inactive section %d, duplicate section %d",
          level->legacy_object_ids.count, level->legacy_pointer_ids.count,
          (int)level->has_inactive_manager_section, (int)level->has_duplicate_manager_section);

    check_level_scenes(stats, object, level);

    /* CKLevel::Save writes the default scene as a sub-chunk and its id next to
       it; no file object is that scene, so the id is null (Load drops it). */
    CHECK(stats, INV_LVL_LEVEL_SCENE, ref_is_null(&level->level_scene), "level scene id %u (state %d)",
          (unsigned)level->level_scene.raw_id, (int)level->level_scene.state);

    /* ... and the sub-chunk is a scene, always. */
    nmo_scene_state_t level_scene;
    level_scene_ids_t tracked = {0};
    const int read = read_level_scene(stats, level, &level_scene, &tracked);
    CHECK(stats, INV_LVL_SCENE_CHUNK, read, "level scene chunk does not read");
    if (read) {
        check_scene(stats, object, &level_scene, object);
        check_level_scene_ids(stats, &level_scene, &tracked);
        nmo_scene_vtable.destroy(&level_scene, NULL, NULL);
    }
    free(tracked.values);
}

/* ---------------------------------------------------------------------------
 * CKDataArray
 * ------------------------------------------------------------------------- */

static void check_dataarray_cell(corpus_semantics_t *stats, const nmo_dataarray_column_format_t *format,
                                 const nmo_dataarray_cell_t *cell, uint32_t row, uint32_t column)
{
    if (format->type == CKARRAYTYPE_OBJECT) {
        const nmo_object_t *target = ref_object(stats, &cell->object_ref);
        if (target != NULL) {
            CHECK(stats, INV_DA_OBJECT_CELL, object_is(stats, target, NMO_CID_OBJECT),
                  "row %u column %u is a %s", row, column, class_name(stats, target->class_id));
        }
    } else if (format->type == CKARRAYTYPE_PARAMETER) {
        /* A cell is a CKParameterOut made with the column's type, or a shortcut
           (PasteShortcut) to another parameter of that type. */
        const nmo_object_t *target = ref_object(stats, &cell->parameter.ref);
        if (target == NULL) {
            return;
        }
        const nmo_parameter_state_t *parameter = STATE(nmo_parameter_state_t, target);
        const nmo_guid_t cell_type = parameter != NULL ? parameter->type_guid : NMO_GUID_NULL;
        CHECK(stats, INV_DA_PARAM_CELL,
              object_is(stats, target, NMO_CID_PARAMETER) && parameter != NULL &&
                  nmo_guid_equals(cell_type, format->parameter_type_guid),
              "row %u column %u: %s of type %08x-%08x, column is %08x-%08x", row, column,
              class_name(stats, target->class_id), (unsigned)cell_type.d1, (unsigned)cell_type.d2,
              (unsigned)format->parameter_type_guid.d1, (unsigned)format->parameter_type_guid.d2);
    }
}

static void check_dataarray(corpus_semantics_t *stats, const nmo_dataarray_state_t *array)
{
    for (uint32_t c = 0; c < array->column_count; c++) {
        const nmo_dataarray_column_format_t *format = &array->column_formats[c];
        /* CKDataArray::Load builds a column only for these five types. */
        CHECK(stats, INV_DA_COLUMN_TYPE,
              format->type >= CKARRAYTYPE_INT && format->type <= CKARRAYTYPE_PARAMETER,
              "column %u type %d", c, (int)format->type);
        /* Save writes the parameter type of a parameter column; every cell of
           the column is made with it by CreateCKParameterOut. */
        if (format->type == CKARRAYTYPE_PARAMETER) {
            CHECK(stats, INV_DA_PARAM_COLUMN, !nmo_guid_is_null(format->parameter_type_guid),
                  "column %u has no parameter type", c);
        }
    }
    /* SetKeyColumn takes a column index, the constructor sets -1, RemoveColumn
       keeps it from growing: it can be left one past the last column. */
    CHECK(stats, INV_DA_KEY_COLUMN,
          array->key_column >= -1 && array->key_column <= (int32_t)array->column_count,
          "key column %d of %u", (int)array->key_column, (unsigned)array->column_count);

    for (uint32_t r = 0; r < array->row_count; r++) {
        const nmo_dataarray_row_t *row = &array->rows[r];
        /* CKDataArray::Load resizes every row to the number of columns. */
        CHECK(stats, INV_DA_ROW_WIDTH, row->column_count == array->column_count,
              "row %u has %u cells for %u columns", r, (unsigned)row->column_count,
              (unsigned)array->column_count);
        for (uint32_t c = 0; row->column_count == array->column_count && c < array->column_count;
             c++) {
            check_dataarray_cell(stats, &array->column_formats[c], &row->cells[c], r, c);
        }
    }
}

/* ---------------------------------------------------------------------------
 * Animations
 * ------------------------------------------------------------------------- */

#define ANIMATION_KNOWN_FLAGS                                                         \
    ((uint32_t)(CKANIMATION_LINKTOFRAMERATE | CKANIMATION_CANBEBREAK |               \
                CKANIMATION_ALLOWTURN | CKANIMATION_ALIGNORIENTATION |               \
                CKANIMATION_SECONDARYWARPER | CKANIMATION_SUBANIMSSORTED |           \
                CKANIMATION_TRANSITION_ALL | CKANIMATION_SECONDARY_ALL |             \
                CKANIMATION_TRANSITION_PRESET | CKANIMATION_SECONDARY_PRESET))

#define OBJECTANIMATION_KNOWN_FLAGS                                                   \
    ((uint32_t)(CK_OBJECTANIMATION_TAG0 | CK_OBJECTANIMATION_TAG1 |                  \
                CK_OBJECTANIMATION_IGNOREPOS | CK_OBJECTANIMATION_IGNOREROT |        \
                CK_OBJECTANIMATION_IGNORESCALE | CK_OBJECTANIMATION_IGNOREMORPH |    \
                CK_OBJECTANIMATION_IGNORESCALEROT | CK_OBJECTANIMATION_MERGED |      \
                CK_OBJECTANIMATION_WARPER))

static int finite_non_negative(float value)
{
    return isfinite(value) && value >= 0.0f;
}

static void check_animation_character(corpus_semantics_t *stats, const nmo_object_t *object,
                                      const nmo_object_t *character)
{
    CHECK(stats, INV_ANIM_CHARACTER, object_is(stats, character, NMO_CID_CHARACTER),
          "character is a %s", class_name(stats, character->class_id));
    /* RCKCharacter::AddAnimation lists the animation and sets its character. */
    const nmo_character_state_t *state =
        object_is(stats, character, NMO_CID_CHARACTER) ? STATE(nmo_character_state_t, character) : NULL;
    const nmo_ref_t *listed = state != NULL ? NMO_ARRAY_DATA(nmo_ref_t, &state->animations) : NULL;
    int found = 0;
    for (size_t i = 0; state != NULL && i < state->animations.count; i++) {
        found = found || ref_names(&listed[i], object->id);
    }
    CHECK(stats, INV_CHARACTER_ANIMATIONS, found, "character %u does not list the animation",
          (unsigned)character->file_id);
}

/* RCKKeyedAnimation::UpdateRoot picks as root the entity whose parent is a
 * character, or the animation's character (none for both: a top-level entity
 * of an animation without character). */
static void check_animation_root(corpus_semantics_t *stats, const nmo_object_t *root,
                                 const nmo_object_t *character)
{
    const nmo_3dentity_state_t *entity = STATE(nmo_3dentity_state_t, root);
    const nmo_object_t *parent = entity != NULL ? ref_object(stats, &entity->parent) : NULL;
    CHECK(stats, INV_ANIM_ROOT,
          object_is(stats, root, NMO_CID_3DENTITY) && entity != NULL &&
              (parent == character || object_is(stats, parent, NMO_CID_CHARACTER)),
          "root entity is a %s, its parent is %s, the character %s", class_name(stats, root->class_id),
          object_class_name(stats, parent), object_class_name(stats, character));
}

static void check_animation(corpus_semantics_t *stats, const nmo_object_t *object,
                            const nmo_animation_state_t *animation)
{
    /* RCKAnimation::Save with a file writes all five sections. */
    CHECK(stats, INV_ANIM_SECTIONS,
          animation->has_data && animation->has_length && animation->has_root_entity &&
              animation->has_character && animation->has_current_step,
          "sections data %d length %d root %d character %d step %d", (int)animation->has_data,
          (int)animation->has_length, (int)animation->has_root_entity,
          (int)animation->has_character, (int)animation->has_current_step);
    /* A frame count (SetLength) and a frame rate (LinkToFrameRate): domain checks. */
    CHECK(stats, INV_ANIM_VALUES,
          finite_non_negative(animation->frame_rate) &&
              (!animation->has_length || finite_non_negative(animation->length)),
          "length %g frame rate %g", animation->length, animation->frame_rate);
    CHECK(stats, INV_ANIM_FLAGS, (animation->flags & ~ANIMATION_KNOWN_FLAGS) == 0, "flags %#x",
          (unsigned)animation->flags);
    /* RCKKeyedAnimation::SetStep clamps the step to [0, 1] (a fraction of the length). */
    if (animation->has_current_step) {
        CHECK(stats, INV_ANIM_STEP, animation->current_step >= 0.0f && animation->current_step <= 1.0f,
              "step %g", animation->current_step);
    }
    const nmo_object_t *character = ref_object(stats, &animation->character);
    if (character != NULL) {
        check_animation_character(stats, object, character);
    }
    const nmo_object_t *root = ref_object(stats, &animation->root_entity);
    if (root != NULL) {
        check_animation_root(stats, root, character);
    }
}

static void check_character_animations(corpus_semantics_t *stats, const nmo_object_t *object,
                                       const nmo_character_state_t *character)
{
    const nmo_ref_t *animations = NMO_ARRAY_DATA(nmo_ref_t, &character->animations);
    int active_listed = ref_is_null(&character->active_animation);
    for (size_t i = 0; i < character->animations.count; i++) {
        /* RCKCharacter::AddAnimation sets the character of the animation it lists. */
        const nmo_object_t *animation = ref_object(stats, &animations[i]);
        const nmo_animation_state_t *state = object_is(stats, animation, NMO_CID_ANIMATION)
                                                 ? STATE(nmo_animation_state_t, animation)
                                                 : NULL;
        CHECK(stats, INV_CHARACTER_ANIMATIONS, state != NULL && ref_names(&state->character, object->id),
              "animation %zu is %s", i, object_class_name(stats, animation));
        active_listed = active_listed || refs_equal(&animations[i], &character->active_animation);
    }
    /* SetActiveAnimation takes an animation of the character. */
    CHECK(stats, INV_CHARACTER_ANIMATIONS, active_listed, "the active animation is not listed");
}

static void check_keyed_animation(corpus_semantics_t *stats, const nmo_object_t *object,
                                  const nmo_keyedanimation_state_t *keyed)
{
    /* RCKKeyedAnimation::Save writes m_Merged, a CKBOOL, and the merge factor. */
    CHECK(stats, INV_KEYED_MERGED,
          (keyed->merged == 0 || keyed->merged == 1) && isfinite(keyed->merge_factor),
          "merged %d factor %g", (int)keyed->merged, keyed->merge_factor);
    for (uint32_t i = 0; i < keyed->animation_count; i++) {
        const nmo_object_t *animation = ref_object(stats, &keyed->animation_ids[i]);
        if (animation == NULL) {
            continue;
        }
        /* The list holds CKObjectAnimation (AddAnimation, and Load keeps what
           XSObjectPointerArray::Check accepts). */
        CHECK(stats, INV_KEYED_LIST, animation->class_id == NMO_CID_OBJECTANIMATION,
              "animation %u is a %s", (unsigned)i, class_name(stats, animation->class_id));
        /* AddAnimation and Load set the one parent pointer of the animation to
           the keyed animation that lists it. */
        const nmo_object_id_t *other = id_pairs_find(&stats->animation_parents, animation->id);
        CHECK(stats, INV_KEYED_PARENT, other == NULL,
              "animation %u is also listed by keyed animation %u", (unsigned)i,
              other != NULL ? (unsigned)*other : 0u);
        if (other == NULL) {
            id_pairs_add(&stats->animation_parents, animation->id, object->id);
        }
        /* RCKKeyedAnimation::SetLength sets the length of every animation it
           lists and its own. An animation that shares keys has no length of
           its own in the file. */
        const nmo_objectanimation_state_t *state = STATE(nmo_objectanimation_state_t, animation);
        if (animation->class_id == NMO_CID_OBJECTANIMATION && state != NULL && state->has_length &&
            keyed->base.has_length) {
            CHECK(stats, INV_KEYED_LENGTH, state->length == keyed->base.length,
                  "animation %u has length %g, keyed %g", (unsigned)i, state->length,
                  keyed->base.length);
        }
    }
}

typedef struct key_layout {
    uint32_t size;
    int rotation;
    size_t rotation_offset;
} key_layout_t;

static key_layout_t key_layout_of(uint32_t type, nmo_objectanimation_format_t format)
{
    key_layout_t layout = {nmo_objanim_controller_format_key_size(type, format), 0, 0};
    switch (type) {
    case 0x49ed4002u: /* CKANIMATION_LINROT_CONTROL: time, quaternion */
    case 0x45b52a02u: /* CKANIMATION_TCBROT_CONTROL: time, quaternion, tension... */
        layout.rotation = 1;
        layout.rotation_offset = 4;
        break;
    case 0x2f200b08u: /* CKANIMATION_LINSCLAXIS_CONTROL: time, quaternion (NEWDATA: time, pad, ...) */
    case 0x32595908u: /* CKANIMATION_TCBSCLAXIS_CONTROL */
        layout.rotation = 1;
        layout.rotation_offset = layout.size == 24 ? 8 : 4;
        break;
    default:
        break;
    }
    return layout;
}

/* The slot of an animation's keyframe data a controller type fills, in the
 * order RCKObjectAnimation::Save writes them (position, rotation, scale, scale
 * axis, morph); -1 for a type the engine does not know. */
static int controller_slot(uint32_t type)
{
    switch (type) {
    case 0x637c4301u: /* LINPOS */
    case 0x347e4a01u: /* TCBPOS */
    case 0x921ab801u: /* BEZIERPOS */
        return 0;
    case 0x49ed4002u: /* LINROT */
    case 0x45b52a02u: /* TCBROT */
        return 1;
    case 0x654a3a04u: /* LINSCL */
    case 0x1b545904u: /* TCBSCL */
    case 0x18ab4404u: /* BEZIERSCL */
        return 2;
    case 0x2f200b08u: /* LINSCLAXIS */
    case 0x32595908u: /* TCBSCLAXIS */
        return 3;
    case NMO_OBJANIM_CONTROLLER_MORPH:
        return 4;
    default:
        return -1;
    }
}

static void check_key_time(corpus_semantics_t *stats, uint32_t type, uint32_t key, float time,
                           float *previous)
{
    /* The controllers' AddKey keeps keys in increasing time order (an equal
       time replaces the key); Evaluate steps through them in that order. */
    CHECK(stats, INV_OBJANIM_KEY_ORDER, isfinite(time) && time >= *previous,
          "controller %#x key %u time %g after %g", (unsigned)type, (unsigned)key, time, *previous);
    *previous = time;
}

static void check_controller_keys(corpus_semantics_t *stats,
                                  const nmo_objectanimation_state_t *animation,
                                  const nmo_objanim_controller_t *controller)
{
    const uint8_t *bytes = (const uint8_t *)controller->data;
    float previous = -INFINITY;
    if (nmo_objanim_controller_is_bezier(controller->type)) {
        size_t offset = 0;
        for (uint32_t k = 0; k < controller->key_count; k++) {
            nmo_objanim_bezier_key_t key;
            const size_t size = nmo_objanim_bezier_key_decode(
                bytes + offset, controller->data_size - offset, &key);
            if (size == 0) {
                break;
            }
            check_key_time(stats, controller->type, k, key.time, &previous);
            offset += size;
        }
        return;
    }
    const key_layout_t layout = key_layout_of(controller->type, animation->format);
    for (uint32_t k = 0; layout.size != 0 && k < controller->key_count; k++) {
        const uint8_t *key = bytes + (size_t)k * layout.size;
        float time;
        memcpy(&time, key, sizeof(time));
        check_key_time(stats, controller->type, k, time, &previous);
        if (layout.rotation) {
            /* VxQuaternion Slerp, which Evaluate uses, takes unit quaternions;
               AddRotationKey stores what it is given (domain check). */
            float q[4];
            memcpy(q, key + layout.rotation_offset, sizeof(q));
            const double length = sqrt((double)q[0] * q[0] + (double)q[1] * q[1] +
                                       (double)q[2] * q[2] + (double)q[3] * q[3]);
            CHECK(stats, INV_OBJANIM_QUATERNION, fabs(length - 1.0) < 1e-3,
                  "controller %#x key %u quaternion length %.6f", (unsigned)controller->type,
                  (unsigned)k, length);
        }
    }
}

static void check_controllers(corpus_semantics_t *stats,
                              const nmo_objectanimation_state_t *animation)
{
    /* CKKeyframeData::CreateController keeps one controller per slot (a later
       one replaces the earlier) and none for a type it does not know, and Save
       writes the slots in a fixed order. */
    const int saved_order = animation->format == CKOBJANIM_FORMAT_CONTROLLERS;
    unsigned seen_slots = 0;
    int previous_slot = -1;
    for (uint32_t i = 0; i < animation->controller_count; i++) {
        const nmo_objanim_controller_t *controller = &animation->controllers[i];
        const int slot = controller_slot(controller->type);
        const int type_ok =
            slot >= 0 && (seen_slots & (1u << slot)) == 0 && (!saved_order || slot > previous_slot);
        if (slot >= 0) {
            seen_slots |= 1u << slot;
            previous_slot = slot;
        }
        CHECK(stats, INV_OBJANIM_CONTROLLER, type_ok, "controller %u has type %#x", (unsigned)i,
              (unsigned)controller->type);
        if (controller->type == NMO_OBJANIM_CONTROLLER_MORPH) {
            nmo_objanim_morph_info_t info;
            CHECK(stats, INV_OBJANIM_BLOB, nmo_objanim_morph_controller_info(controller, &info),
                  "morph controller of %u bytes", (unsigned)controller->data_size);
            continue;
        }
        /* ReadKeysFrom reads a key count and then that many keys of the
           controller's key size, so a controller blob is exactly that. A
           controller without keys is the count alone. */
        size_t keys_size = 0;
        const int blob_ok =
            controller->key_count > 0
                ? nmo_objanim_controller_keys_size(controller->type, animation->format,
                                                   controller->data, controller->data_size,
                                                   controller->key_count, &keys_size) &&
                      keys_size == controller->data_size
                : controller->data_size == sizeof(uint32_t);
        CHECK(stats, INV_OBJANIM_BLOB, blob_ok, "controller %#x: %u keys, %u bytes",
              (unsigned)controller->type, (unsigned)controller->key_count,
              (unsigned)controller->data_size);
        if (blob_ok && controller->key_count > 0) {
            check_controller_keys(stats, animation, controller);
        }
    }
}

static void check_merge_and_sharing(corpus_semantics_t *stats, const nmo_object_t *object,
                                    const nmo_objectanimation_state_t *animation)
{
    /* RCKObjectAnimation::Save writes the merge block iff
       CK_OBJECTANIMATION_MERGED is set; CreateMergedAnimation sets the two
       parts to CKObjectAnimation. */
    if (animation->format == CKOBJANIM_FORMAT_SHARED ||
        animation->format == CKOBJANIM_FORMAT_CONTROLLERS ||
        animation->format == CKOBJANIM_FORMAT_NEWDATA) {
        const int merged = (animation->flags & CK_OBJECTANIMATION_MERGED) != 0;
        int merge_ok = (animation->has_merge != 0) == merged;
        if (merged) {
            const nmo_object_t *anim1 = ref_object(stats, &animation->anim1);
            const nmo_object_t *anim2 = ref_object(stats, &animation->anim2);
            merge_ok = merge_ok && anim1 != NULL && anim1->class_id == NMO_CID_OBJECTANIMATION &&
                       anim2 != NULL && anim2->class_id == NMO_CID_OBJECTANIMATION &&
                       finite_non_negative(animation->merge_factor);
        }
        CHECK(stats, INV_OBJANIM_MERGE, merge_ok, "flags %#x merge block %d",
              (unsigned)animation->flags, (int)animation->has_merge);
    }

    /* RCKObjectAnimation::Save writes SHARED only for an animation whose
       keyframe data belongs to another animation of the file, which then
       writes its keys (CONTROLLERS or NEWDATA), never SHARED itself. */
    if (animation->format == CKOBJANIM_FORMAT_SHARED) {
        const nmo_object_t *shared = ref_object(stats, &animation->shared_anim);
        const nmo_objectanimation_state_t *owner =
            shared != NULL ? STATE(nmo_objectanimation_state_t, shared) : NULL;
        CHECK(stats, INV_OBJANIM_SHARED,
              shared != NULL && shared != object && shared->class_id == NMO_CID_OBJECTANIMATION &&
                  owner != NULL && owner->format != CKOBJANIM_FORMAT_SHARED &&
                  owner->format != CKOBJANIM_FORMAT_NONE,
              "shared animation is %s", object_class_name(stats, shared));
    }
}

static void check_object_animation(corpus_semantics_t *stats, const nmo_object_t *object,
                                   const nmo_objectanimation_state_t *animation)
{
    /* The keyframe data holds a frame count (SetLength): a domain check. */
    if (animation->has_length) {
        CHECK(stats, INV_OBJANIM_LENGTH, finite_non_negative(animation->length), "length %g",
              animation->length);
    }
    CHECK(stats, INV_OBJANIM_FLAGS, (animation->flags & ~OBJECTANIMATION_KNOWN_FLAGS) == 0,
          "flags %#x", (unsigned)animation->flags);
    /* RCKObjectAnimation::Save writes four zero floats after the root vector. */
    if (animation->has_root_pos) {
        CHECK(stats, INV_OBJANIM_ROOT,
              animation->root_extra.x == 0.0f && animation->root_extra.y == 0.0f &&
                  animation->root_extra.z == 0.0f && animation->root_extra.w == 0.0f,
              "root extra %g %g %g %g", animation->root_extra.x, animation->root_extra.y,
              animation->root_extra.z, animation->root_extra.w);
    }
    /* RCKObjectAnimation::Set3dEntity takes a CK3dEntity. */
    const nmo_object_t *entity = ref_object(stats, &animation->entity);
    if (entity != NULL) {
        CHECK(stats, INV_OBJANIM_ENTITY, object_is(stats, entity, NMO_CID_3DENTITY),
              "entity is a %s", class_name(stats, entity->class_id));
    }
    check_merge_and_sharing(stats, object, animation);
    check_controllers(stats, animation);
}

/* RCKObjectAnimation::Save writes the root vector of its keyed animation for
 * the root animation of that keyed animation (the one of the root entity) and
 * a zero vector for every other animation. */
static void check_root_positions(corpus_semantics_t *stats)
{
    const size_t count = nmo_object_repository_get_count(stats->repository);
    for (size_t i = 0; i < count; i++) {
        const nmo_object_t *object = nmo_object_repository_get_by_index(stats->repository, i);
        const nmo_objectanimation_state_t *animation =
            object != NULL && object->class_id == NMO_CID_OBJECTANIMATION
                ? STATE(nmo_objectanimation_state_t, object)
                : NULL;
        if (animation == NULL || !animation->has_root_pos) {
            continue;
        }
        stats->current = object;
        int ok = 1;
        if (animation->root_pos.x != 0.0f || animation->root_pos.y != 0.0f ||
            animation->root_pos.z != 0.0f) {
            const nmo_object_id_t *keyed_id = id_pairs_find(&stats->animation_parents, object->id);
            const nmo_object_t *keyed =
                keyed_id != NULL ? nmo_object_repository_find_by_id(stats->repository, *keyed_id)
                                 : NULL;
            const nmo_keyedanimation_state_t *keyed_state =
                keyed != NULL ? STATE(nmo_keyedanimation_state_t, keyed) : NULL;
            ok = keyed_state != NULL && nmo_ref_runtime_id(&animation->entity) != NMO_OBJECT_ID_NONE &&
                 ref_names(&keyed_state->base.root_entity, nmo_ref_runtime_id(&animation->entity));
        }
        CHECK(stats, INV_OBJANIM_ROOT, ok, "root vector %g %g %g on an animation that is not a root",
              animation->root_pos.x, animation->root_pos.y, animation->root_pos.z);
    }
    stats->current = NULL;
}

/* ---------------------------------------------------------------------------
 * Synchro, critical section and interface objects
 * ------------------------------------------------------------------------- */

static void check_synchro(corpus_semantics_t *stats, const nmo_synchro_state_t *synchro)
{
    const nmo_ref_t *arrived = NMO_ARRAY_DATA(nmo_ref_t, &synchro->arrived_ids);
    const nmo_ref_t *passed = NMO_ARRAY_DATA(nmo_ref_t, &synchro->passed_ids);
    /* CanIPassRendezVous: objects arrive once (PtrSeek) while fewer than
       m_MaxWaiters have, an object passes only after it arrived, and both
       lists are cleared when m_MaxWaiters objects have passed. */
    int ok = synchro->max_waiters >= 0 && synchro->arrived_ids.count <= (size_t)synchro->max_waiters &&
             synchro->passed_ids.count <= (size_t)synchro->max_waiters;
    for (size_t i = 0; i < synchro->arrived_ids.count; i++) {
        ok = ok && object_is(stats, ref_object(stats, &arrived[i]), NMO_CID_BEOBJECT);
        for (size_t j = 0; j < i; j++) {
            ok = ok && !refs_equal(&arrived[j], &arrived[i]);
        }
    }
    for (size_t i = 0; i < synchro->passed_ids.count; i++) {
        int arrived_before = 0;
        for (size_t j = 0; j < synchro->arrived_ids.count; j++) {
            arrived_before = arrived_before || refs_equal(&arrived[j], &passed[i]);
        }
        ok = ok && object_is(stats, ref_object(stats, &passed[i]), NMO_CID_BEOBJECT) &&
             arrived_before;
    }
    CHECK(stats, INV_SYNCHRO, ok, "max waiters %d, %zu arrived, %zu passed", (int)synchro->max_waiters,
          synchro->arrived_ids.count, synchro->passed_ids.count);
}

static void check_critical_section(corpus_semantics_t *stats,
                                   const nmo_criticalsection_state_t *section)
{
    /* CKCriticalSectionObject::EnterCriticalSection takes a CKBeObject. */
    const nmo_object_t *holder = ref_object(stats, &section->object_in_section);
    CHECK(stats, INV_CRITICAL_SECTION, holder == NULL || object_is(stats, holder, NMO_CID_BEOBJECT),
          "holder is %s", object_class_name(stats, holder));
}

static void check_interface_object(corpus_semantics_t *stats,
                                   const nmo_interfaceobjectmanager_state_t *iom)
{
    /* CKInterfaceObjectManager::Save writes the chunk count and that many
       sub-chunks, then the guid of the manager. */
    int chunks_ok = iom->chunk_count >= 0 && (iom->chunk_count == 0 || iom->chunks != NULL);
    for (int32_t i = 0; chunks_ok && i < iom->chunk_count; i++) {
        chunks_ok = iom->chunks[i] != NULL;
    }
    CHECK(stats, INV_IOM,
          chunks_ok && iom->has_chunks_chunk && iom->has_guid_chunk && !nmo_guid_is_null(iom->guid),
          "%d chunks, chunk section %d, guid section %d", (int)iom->chunk_count,
          (int)iom->has_chunks_chunk, (int)iom->has_guid_chunk);
}

/* ---------------------------------------------------------------------------
 * Corpus walk
 * ------------------------------------------------------------------------- */

/* The scripts of every be-object, and whether the file holds a scene or level. */
static void index_file(corpus_semantics_t *stats)
{
    const size_t count = nmo_object_repository_get_count(stats->repository);
    stats->file_has_scene_or_level = 0;
    for (size_t i = 0; i < count; i++) {
        const nmo_object_t *object = nmo_object_repository_get_by_index(stats->repository, i);
        if (object == NULL) {
            continue;
        }
        if (object->class_id == NMO_CID_SCENE || object->class_id == NMO_CID_LEVEL) {
            stats->file_has_scene_or_level = 1;
        }
        if (!object_is(stats, object, NMO_CID_BEOBJECT) || nmo_object_get_state(object) == NULL) {
            continue;
        }
        const nmo_beobject_state_t *beobject = STATE(nmo_beobject_state_t, object);
        const nmo_ref_t *scripts = NMO_ARRAY_DATA(nmo_ref_t, &beobject->scripts);
        for (size_t s = 0; s < beobject->scripts.count; s++) {
            if (nmo_ref_runtime_id(&scripts[s]) != NMO_OBJECT_ID_NONE) {
                id_pairs_add(&stats->script_owners, nmo_ref_runtime_id(&scripts[s]), object->id);
            }
        }
    }
}

static void check_object(corpus_semantics_t *stats, const nmo_object_t *object, const void *state)
{
    check_object_common(stats, object, state);

    switch (object->class_id) {
    case NMO_CID_GROUP:
        check_group(stats, object, (const nmo_group_state_t *)state);
        break;
    case NMO_CID_SCENE:
        check_scene(stats, object, (const nmo_scene_state_t *)state, NULL);
        break;
    case NMO_CID_LEVEL:
        check_level(stats, object, (const nmo_level_state_t *)state);
        break;
    case NMO_CID_DATAARRAY:
        check_dataarray(stats, (const nmo_dataarray_state_t *)state);
        break;
    case NMO_CID_ANIMATION:
        check_animation(stats, object, (const nmo_animation_state_t *)state);
        break;
    case NMO_CID_KEYEDANIMATION:
        check_animation(stats, object, &((const nmo_keyedanimation_state_t *)state)->base);
        check_keyed_animation(stats, object, (const nmo_keyedanimation_state_t *)state);
        break;
    case NMO_CID_OBJECTANIMATION:
        check_object_animation(stats, object, (const nmo_objectanimation_state_t *)state);
        break;
    case NMO_CID_CHARACTER:
        check_character_animations(stats, object, (const nmo_character_state_t *)state);
        break;
    case NMO_CID_SYNCHRO:
        check_synchro(stats, (const nmo_synchro_state_t *)state);
        break;
    case NMO_CID_CRITICALSECTION:
        check_critical_section(stats, (const nmo_criticalsection_state_t *)state);
        break;
    case NMO_CID_INTERFACEOBJECTMANAGER:
        check_interface_object(stats, (const nmo_interfaceobjectmanager_state_t *)state);
        break;
    case NMO_CID_PARAMETEROUT:
    case NMO_CID_PARAMETERLOCAL:
        check_manager_parameter(stats, (const nmo_parameter_state_t *)state);
        break;
    default:
        break;
    }
}

static void check_file(const char *path, void *user)
{
    corpus_semantics_t *stats = (corpus_semantics_t *)user;
    stats->files++;
    stats->path = path;
    stats->current = NULL;
    nmo_arena_reset(stats->arena);

    nmo_session_t *session = nmo_session_create(stats->ctx);
    if (session == NULL || nmo_session_load_file(session, path, NULL, NULL) != NMO_OK) {
        stats->load_errors++;
        printf("  %s: load failed\n", path);
        nmo_session_destroy(session);
        return;
    }
    stats->repository = nmo_session_get_repository(session);
    stats->product_build = nmo_session_get_file_info(session).product_build;
    stats->animation_parents.count = 0;
    stats->script_owners.count = 0;
    stats->claimed_parameters.count = 0;
    load_managers(stats, session);
    check_attribute_manager(stats);
    check_message_manager(stats);
    index_file(stats);

    const size_t count = nmo_object_repository_get_count(stats->repository);
    for (size_t i = 0; i < count; i++) {
        const nmo_object_t *object = nmo_object_repository_get_by_index(stats->repository, i);
        if (object == NULL) {
            continue;
        }
        stats->current = object;
        stats->objects++;
        const void *state = nmo_object_get_state(object);
        if (state == NULL) {
            stats->objects_without_state++;
            continue;
        }
        check_object(stats, object, state);
    }
    check_root_positions(stats);
    stats->current = NULL;
    nmo_session_destroy(session);
}

TEST(corpus_semantics_scene, decoded_values_satisfy_engine_guarantees)
{
    /* Corpus scan: skip when the gitignored Virtools sample set is absent. */
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");

    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    corpus_semantics_t *stats = calloc(1, sizeof(*stats));
    ASSERT_NOT_NULL(stats);
    stats->ctx = ctx;
    stats->types = nmo_context_get_type_registry(ctx);
    stats->arena = nmo_arena_create(NULL, 1 << 20);
    ASSERT_NOT_NULL(stats->arena);

    const int walk_status = test_corpus_walk(NMO_TEST_DATA_DIR, check_file, stats);

    printf("  Corpus semantics: files=%zu load_errors=%zu objects=%zu without_state=%zu\n",
           stats->files, stats->load_errors, stats->objects, stats->objects_without_state);
    size_t vacuous = 0;
    size_t violated = 0;
    for (int i = 0; i < INV_COUNT; i++) {
        const invariant_t *invariant = &stats->invariants[i];
        printf("  %-70s checked=%-8zu violated=%zu\n", invariant_names[i], invariant->checked,
               invariant->violated);
        vacuous += invariant->checked == 0;
        violated += invariant->violated;
    }

    const size_t files = stats->files;
    const size_t load_errors = stats->load_errors;
    const size_t objects_without_state = stats->objects_without_state;
    free(stats->animation_parents.keys);
    free(stats->animation_parents.values);
    free(stats->script_owners.keys);
    free(stats->script_owners.values);
    free(stats->claimed_parameters.keys);
    free(stats->claimed_parameters.values);
    nmo_arena_destroy(stats->arena);
    nmo_context_release(ctx);
    free(stats);

    ASSERT_EQ(0, walk_status);
    ASSERT_GE(files, 1u);
    ASSERT_EQ(0u, load_errors);
    ASSERT_EQ(0u, objects_without_state);
    ASSERT_EQ(0u, vacuous);
    ASSERT_EQ(0u, violated);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(corpus_semantics_scene, decoded_values_satisfy_engine_guarantees);
TEST_MAIN_END()
