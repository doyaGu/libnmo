/**
 * @file test_corpus_semantics_behavior.c
 * @brief Decoded behavior graphs and parameters of the corpus satisfy what the engine guarantees
 *
 * test_corpus_chunk_roundtrip proves that no bytes are lost; it cannot tell a
 * field that was read correctly from one that was read into the wrong member.
 * These checks decode every file of the reference corpus and assert the
 * relationships the Virtools engine (CK2.dll) maintains between the decoded
 * CKBehavior, CKBehaviorIO, CKBehaviorLink, CKParameterIn/Out/Local and
 * CKParameterOperation values and the InterfaceChunk of a behavior, so a
 * swapped, misread or mis-scaled field shows up as a violation.
 *
 * Every invariant keeps two counters: how many values were checked (the test
 * insists on a non-zero count, so a check cannot silently become vacuous) and
 * how many violated (must stay zero). The engine function that makes it hold
 * is named next to each check; the InterfaceChunk is editor metadata that
 * CK2.dll never interprets, so its checks state what the editor writes.
 *
 * A file does not store owners and parents of its objects. They are derived
 * the way CKBehavior::HierarchyPostLoad and CKBeObject::ApplyOwner derive
 * them, from which container lists an object. ExportInputParameter and
 * ExportOutputParameter let a graph list a parameter of a descendant, so an
 * object may be listed by several behaviors as long as they are ancestors of
 * one another.
 */

#include "../test_framework.h"

#include "extension/nmo_behavior_registry.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include "core/nmo_guid.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorio_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "object/nmo_statesave_ids.h"
#include "runtime/nmo_context.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_session.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_operation_system.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_query.h"
#include "type/nmo_type_system.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Violations printed per invariant; the counters always see all of them. */
#ifndef MAX_REPORTED_VIOLATIONS
#define MAX_REPORTED_VIOLATIONS 6
#endif

/* Flag bits of CKBehavior::m_Flags (CKEnums.h, CK_BEHAVIOR_FLAGS). */
#define BEH_SCRIPT        0x00000002u
#define BEH_USEFUNCTION   0x00000008u
#define BEH_TOPMOST       0x00004000u
#define BEH_BUILDINGBLOCK 0x00008000u
#define BEH_TARGETABLE    0x00040000u

/* Flag bits CK keeps only while a behavior runs (CKBEHAVIOR_ACTIVE, EXECUTEDLASTFRAME,
   DEACTIVATENEXTFRAME, RESETNEXTFRAME, ACTIVATENEXTFRAME, RESERVED0, LAUNCHEDONCE). */
#define BEH_RUNTIME_BITS 0x90F00001u

/* CKBehaviorIO::GetOldFlags: IN, OUT and, while the IO is active, 0x100. */
#define IO_OLD_IN     0x1u
#define IO_OLD_OUT    0x2u
#define IO_OLD_ACTIVE 0x100u

#define MAX_HOLDERS 8

#define INVARIANTS(X) \
    X(BEH_FUNCTION_FLAGS, \
      "behavior: BUILDINGBLOCK and USEFUNCTION are set and cleared together") \
    X(BEH_BB_GUID, "building block: prototype GUID is not null") \
    X(BEH_BB_VERSION, "building block: prototype version is not zero") \
    X(BEH_BB_NO_GRAPH, \
      "building block: no sub-behaviors, links or operations of its own") \
    X(BEH_SAVE_FLAGS, "behavior: save flags name exactly the non-empty arrays") \
    X(BEH_ARRAY_CLASSES, \
      "behavior: every listed object resolves and has the class of its array") \
    X(BEH_TARGET, \
      "behavior: a target is a ParameterIn of a TARGETABLE behavior, not in its own in-parameters") \
    X(BEH_TARGET_TYPE, \
      "behavior: the target's type is compatible with the type of the compatible class") \
    X(BEH_COMPAT_CLASS, "behavior: the compatible class is a known class") \
    X(BEH_SINGLE_ACTIVITY, "behavior: the scene activity flags are CK_SCENEOBJECT_FLAGS bits") \
    X(BEH_SUB_ORDER, "behavior: sub-behaviors are listed by decreasing priority") \
    X(BEH_SCRIPT_LISTED, \
      "CKBeObject: scripts are scripts, listed by decreasing priority") \
    X(BEH_SUB_NOT_TOPMOST, "behavior: a sub-behavior is not TOPMOST") \
    X(BEH_SCRIPT_TOPLEVEL, "behavior: a script is TOPMOST and nobody's sub-behavior") \
    X(BEH_PARENT_CHAIN, "behavior: one parent at most, parent chain ends at a script") \
    X(BEH_SCRIPT_OWNER, "script: at most one CKBeObject lists it") \
    X(BEH_SCRIPT_COMPAT, \
      "script: the owner's class derives from the compatible class (unless it takes a target)") \
    X(BEH_SUB_COMPAT, \
      "sub-behavior: the script owner's class derives from its compatible class") \
    X(BEH_PROTO_FLAGS, "building block: the prototype's behavior flags are kept") \
    X(IO_FLAGS, "behavior IO: exactly one of IN and OUT is stored") \
    X(IO_LIST_FLAG, "behavior IO: the list that holds it matches its IN/OUT flag") \
    X(IO_SINGLE_OWNER, "behavior IO: listed by exactly one behavior") \
    X(LINK_ENDPOINT_CLASS, "behavior link: both ends resolve to a CKBehaviorIO") \
    X(LINK_IN_GRAPH, \
      "behavior link: listed by one graph and joins IOs of that graph or its sub-behaviors") \
    X(LINK_DIRECTION, \
      "behavior link: leaves a sub-behavior output or graph input, enters a sub-behavior " \
      "input or graph output") \
    X(LINK_DELAY, "behavior link: the delay counts down from the initial delay, never below zero") \
    X(PIN_SOURCE_CLASS, \
      "ParameterIn: a shared source is a ParameterIn, a direct one a ParameterOut/Local") \
    X(PIN_SOURCE_TYPE, "ParameterIn: source and pin types are compatible") \
    X(PIN_SHARED_CHAIN, "ParameterIn: shared sources end in a direct source, without a cycle") \
    X(PIN_HOLDERS, \
      "ParameterIn: held by a list, target slot or operation, exported only by ancestors") \
    X(PIN_TYPE_KNOWN, "ParameterIn: the type GUID is a registered type") \
    X(POUT_DEST, "ParameterOut: destinations are parameters, each listed once") \
    X(POUT_DEST_BACKEDGE, \
      "ParameterOut: no destination is itself, a 'myself' local or lists this one back") \
    X(POUT_DEST_TYPE, "ParameterOut: destination and source types are compatible") \
    X(POUT_TYPE_KNOWN, "ParameterOut: the type GUID is a registered type") \
    X(POUT_HOLDERS, \
      "ParameterOut: held by behaviors of one parent chain, an operation or another object") \
    X(PLOCAL_MYSELF, "ParameterLocal: a 'myself' parameter stores no value") \
    X(PLOCAL_TYPE_KNOWN, "ParameterLocal: the type GUID of a stored value is a registered type") \
    X(PLOCAL_HOLDERS, "ParameterLocal: listed by behaviors of one parent chain") \
    X(PLOCAL_SETTING, "ParameterLocal: only a building block has setting parameters") \
    X(OP_ROLES, "ParameterOperation: in1 and in2 are ParameterIn, the result a ParameterOut") \
    X(OP_LISTED, "ParameterOperation: listed by exactly one graph") \
    X(OP_PINS, \
      "ParameterOperation: three distinct pins, exported at most by its graph or an ancestor") \
    X(OP_SIGNATURE, "ParameterOperation: its GUID admits the pin types or the result is copied") \
    X(PARAM_OBJECT_VALUE, "parameter: a value stored as an object is null or a resolved object") \
    X(PARAM_SIZE, "parameter: a buffer value of a fixed-size type holds exactly that size") \
    X(PARAM_MODE_TYPE, \
      "parameter: an object-class type stores an object reference, no other type does") \
    X(PARAM_MODE_STABLE, \
      "parameter: one storage mode and one manager per type across the corpus") \
    X(IF_SUBS, \
      "interface: the sub-behaviors are exactly the graph's descendants, each once") \
    X(IF_LINKS, "interface: behavior links are exactly the graph's CKBehaviorLinks") \
    X(IF_LINK_ENDS, "interface: behavior link ends name the IOs of their CKBehaviorLink") \
    X(IF_OPS, "interface: operations belong to the graph") \
    X(IF_LOCALS, "interface: the local parameter count is the graph's") \
    X(IF_PARAM_LINK_ENDS, "interface: parameter link ends name existing parameter slots") \
    X(IF_FINITE, "interface: positions, sizes and routing points are finite") \
    X(IF_ROOT_KIND, "interface: the root has a script header exactly when the behavior is a script")

typedef enum inv_id {
#define X(id, text) INV_##id,
    INVARIANTS(X)
#undef X
    INV_COUNT
} inv_id_t;

static const char *const inv_text[INV_COUNT] = {
#define X(id, text) text,
    INVARIANTS(X)
#undef X
};

static const char *const inv_name[INV_COUNT] = {
#define X(id, text) #id,
    INVARIANTS(X)
#undef X
};

/* What a container lists. */
typedef enum member_kind {
    K_SUB,
    K_LINK,
    K_OP,
    K_PIN,
    K_POUT,
    K_PLOCAL,
    K_IO_IN,
    K_IO_OUT,
    K_TARGET,
    K_OP_IN,
    K_OP_OUT,
    K_FOREIGN,
    K_SCRIPT,
    K_COUNT
} member_kind_t;

/* An object of the file and the containers that list it, per kind. */
typedef struct fact {
    nmo_object_t *object;
    uint16_t holders[K_COUNT];                  /* how many containers list it */
    nmo_object_id_t holder[K_COUNT][MAX_HOLDERS]; /* the first MAX_HOLDERS of them, in file order */
} fact_t;

/* How the values of one parameter type were stored (not counting values without data). */
typedef struct mode_record {
    nmo_guid_t type_guid;
    size_t modes[5];
    nmo_guid_t manager_guid;
    int has_manager;
    int manager_conflict;
} mode_record_t;

#define MAX_MODE_RECORDS 512

typedef struct corpus_semantics {
    nmo_context_t *ctx;
    size_t files;
    size_t load_errors;
    size_t checked[INV_COUNT];
    size_t violated[INV_COUNT];
    size_t reported[INV_COUNT];

    /* Not violations: values the corpus holds that a check cannot judge. */
    size_t plugin_types;
    size_t proto_version_skipped;
    size_t unowned_scripts;
    size_t orphan_locals;
    size_t self_managed_locals;
    size_t exported_params;
    size_t interfaces;
    size_t interface_parse_failed;
    size_t ops_no_signature_copied;
    size_t mode_records;
    size_t mode_records_dropped;
    mode_record_t mode[MAX_MODE_RECORDS];
} corpus_semantics_t;

typedef struct file_ctx {
    corpus_semantics_t *stats;
    const char *path;
    nmo_object_repository_t *repo;
    nmo_arena_t *arena;
    nmo_type_registry_t *types;
    nmo_operation_registry_t *ops;
    nmo_behavior_registry_t *bbs;
    fact_t *facts;
    size_t fact_count;
    uint32_t *slots;
    size_t slot_cap;
} file_ctx_t;

/* ------------------------------------------------------------------ */
/* Reporting                                                           */
/* ------------------------------------------------------------------ */

static void report_violation(file_ctx_t *f, inv_id_t id, const nmo_object_t *object,
                             const char *fmt, ...)
{
    corpus_semantics_t *stats = f->stats;
    stats->violated[id]++;
    if (stats->reported[id]++ >= MAX_REPORTED_VIOLATIONS) {
        return;
    }
    printf("  VIOLATION %s: %s: object %u (id %u, class %u): ", inv_name[id], f->path,
           object != NULL ? (unsigned)object->file_id : 0u,
           object != NULL ? (unsigned)object->id : 0u,
           object != NULL ? (unsigned)object->class_id : 0u);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

/* One value checked; the message arguments are only evaluated when it fails. */
#define CHECK(f, id, cond, object, ...)                         \
    do {                                                        \
        (f)->stats->checked[id]++;                              \
        if (!(cond)) {                                          \
            report_violation((f), (id), (object), __VA_ARGS__); \
        }                                                       \
    } while (0)

/* ------------------------------------------------------------------ */
/* Per-file object table                                               */
/* ------------------------------------------------------------------ */

static size_t slot_of(nmo_object_id_t id, size_t cap)
{
    return (size_t)((id * 2654435761u) & (cap - 1));
}

static fact_t *fact_by_id(const file_ctx_t *f, nmo_object_id_t id)
{
    if (id == NMO_OBJECT_ID_NONE || f->slot_cap == 0) {
        return NULL;
    }
    size_t slot = slot_of(id, f->slot_cap);
    while (f->slots[slot] != 0) {
        fact_t *fact = &f->facts[f->slots[slot] - 1];
        if (fact->object->id == id) {
            return fact;
        }
        slot = (slot + 1) & (f->slot_cap - 1);
    }
    return NULL;
}

static int build_facts(file_ctx_t *f)
{
    size_t count = nmo_object_repository_get_count(f->repo);
    f->facts = (fact_t *)calloc(count != 0 ? count : 1, sizeof(fact_t));
    size_t cap = 16;
    while (cap < count * 2) {
        cap <<= 1;
    }
    f->slots = (uint32_t *)calloc(cap, sizeof(uint32_t));
    if (f->facts == NULL || f->slots == NULL) {
        return -1;
    }
    f->slot_cap = cap;
    for (size_t i = 0; i < count; i++) {
        nmo_object_t *object = nmo_object_repository_get_by_index(f->repo, i);
        if (object == NULL) {
            continue;
        }
        f->facts[f->fact_count].object = object;
        f->fact_count++;
        size_t slot = slot_of(object->id, cap);
        while (f->slots[slot] != 0) {
            slot = (slot + 1) & (cap - 1);
        }
        f->slots[slot] = (uint32_t)f->fact_count;
    }
    return 0;
}

static void note_member(file_ctx_t *f, const nmo_ref_t *ref, member_kind_t kind,
                        nmo_object_id_t holder)
{
    if (ref == NULL || ref->state != NMO_REF_RESOLVED) {
        return;
    }
    fact_t *fact = fact_by_id(f, ref->id);
    if (fact == NULL) {
        return;
    }
    if (fact->holders[kind] < MAX_HOLDERS) {
        fact->holder[kind][fact->holders[kind]] = holder;
    }
    if (fact->holders[kind] < UINT16_MAX) {
        fact->holders[kind]++;
    }
}

static void note_array(file_ctx_t *f, const nmo_array_t *array, member_kind_t kind,
                       nmo_object_id_t holder)
{
    if (array == NULL || array->data == NULL) {
        return;
    }
    const nmo_behavior_ref_t *refs = NMO_ARRAY_DATA(nmo_behavior_ref_t, array);
    for (size_t i = 0; i < array->count; i++) {
        note_member(f, &refs[i].ref, kind, holder);
    }
}

static const nmo_behavior_state_t *behavior_state(const nmo_object_t *object)
{
    return object != NULL && object->class_id == NMO_CID_BEHAVIOR
               ? (const nmo_behavior_state_t *)nmo_object_get_state(object)
               : NULL;
}

static const nmo_beobject_state_t *beobject_state(file_ctx_t *f, nmo_object_t *object)
{
    return (const nmo_beobject_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
        f->types, object, CKPGUID_BEOBJECT);
}

/* Which containers list which objects: the ownership the engine derives on load. */
static void collect_membership(file_ctx_t *f)
{
    for (size_t i = 0; i < f->fact_count; i++) {
        nmo_object_t *object = f->facts[i].object;
        if (object->class_id == NMO_CID_BEHAVIOR) {
            const nmo_behavior_state_t *state = behavior_state(object);
            if (state == NULL) {
                continue;
            }
            note_array(f, &state->sub_behaviors, K_SUB, object->id);
            note_array(f, &state->sub_behavior_links, K_LINK, object->id);
            note_array(f, &state->operations, K_OP, object->id);
            note_array(f, &state->in_parameters, K_PIN, object->id);
            note_array(f, &state->out_parameters, K_POUT, object->id);
            note_array(f, &state->local_parameters, K_PLOCAL, object->id);
            note_array(f, &state->inputs, K_IO_IN, object->id);
            note_array(f, &state->outputs, K_IO_OUT, object->id);
            note_member(f, &state->target_parameter, K_TARGET, object->id);
        } else if (object->class_id == NMO_CID_PARAMETEROPERATION) {
            const nmo_parameteroperation_state_t *state =
                (const nmo_parameteroperation_state_t *)nmo_object_get_state(object);
            if (state != NULL) {
                note_member(f, &state->in1.ref, K_OP_IN, object->id);
                note_member(f, &state->in2.ref, K_OP_IN, object->id);
                note_member(f, &state->out.ref, K_OP_OUT, object->id);
            }
        }
        const nmo_beobject_state_t *be = beobject_state(f, object);
        if (be != NULL && be->scripts.data != NULL) {
            const nmo_ref_t *scripts = NMO_ARRAY_DATA(nmo_ref_t, &be->scripts);
            for (size_t s = 0; s < be->scripts.count; s++) {
                note_member(f, &scripts[s], K_SCRIPT, object->id);
            }
        }
    }

    /* Objects other than the graph classes hold parameters too: CKBeObject attributes keep
       their values as ParameterOut objects, a CKDataArray keeps one per object-typed cell. */
    nmo_ref_graph_t *graph = nmo_ref_graph_create(f->repo, f->types, f->arena);
    nmo_ref_edge_t *edges = NULL;
    size_t edge_count = 0;
    if (graph != NULL && nmo_ref_graph_get_edges(graph, &edges, &edge_count) == NMO_OK) {
        for (size_t i = 0; i < edge_count; i++) {
            const fact_t *from = fact_by_id(f, edges[i].from);
            if (from == NULL) {
                continue;
            }
            switch (from->object->class_id) {
            case NMO_CID_BEHAVIOR:
            case NMO_CID_BEHAVIORIO:
            case NMO_CID_BEHAVIORLINK:
            case NMO_CID_PARAMETER:
            case NMO_CID_PARAMETERIN:
            case NMO_CID_PARAMETEROUT:
            case NMO_CID_PARAMETERLOCAL:
            case NMO_CID_PARAMETEROPERATION:
                break;
            default: {
                const nmo_ref_t ref = nmo_ref_from_id(edges[i].to);
                note_member(f, &ref, K_FOREIGN, edges[i].from);
                break;
            }
            }
        }
    }
    if (graph != NULL) {
        nmo_ref_graph_destroy(graph);
    }
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static const fact_t *resolved_fact(const file_ctx_t *f, const nmo_ref_t *ref)
{
    return ref != NULL && ref->state == NMO_REF_RESOLVED ? fact_by_id(f, ref->id) : NULL;
}

static int ref_has_class(const file_ctx_t *f, const nmo_ref_t *ref, nmo_class_id_t class_id)
{
    const fact_t *fact = resolved_fact(f, ref);
    return fact != NULL && fact->object->class_id == class_id;
}

static int is_parameter_class(nmo_class_id_t class_id)
{
    return class_id == NMO_CID_PARAMETER || class_id == NMO_CID_PARAMETEROUT ||
           class_id == NMO_CID_PARAMETERLOCAL;
}

static int class_derives(const file_ctx_t *f, nmo_class_id_t class_id, nmo_class_id_t base)
{
    return nmo_type_query_class_is_derived_from(f->types, class_id, base);
}

static const nmo_type_descriptor_t *type_of(const file_ctx_t *f, nmo_guid_t guid)
{
    return nmo_guid_is_null(guid) ? NULL : nmo_type_query_find_by_guid(f->types, guid);
}

/* Parameter types that plugin DLLs register and data/virtools_parameter_types.json does not
   list: the "Modify Parameters" setting types of the LOD manager's blocks, the platform enum of
   the "Get Host Platform" block and an attribute value type of the Hydroboat sample. */
static int is_plugin_type(nmo_guid_t guid)
{
    static const nmo_guid_t plugin_types[] = {
        NMO_GUID_INIT(0x3C5302CCu, 0x2A611067u),
        NMO_GUID_INIT(0x7E4417E1u, 0x5D0D45B3u),
        NMO_GUID_INIT(0x74E426FAu, 0xA3E48963u),
        NMO_GUID_INIT(0x2B2D2F43u, 0x02AB7BC5u),
    };
    for (size_t i = 0; i < sizeof(plugin_types) / sizeof(plugin_types[0]); i++) {
        if (nmo_guid_equals(plugin_types[i], guid)) {
            return 1;
        }
    }
    return 0;
}

/* The type GUID is a registered type, or one of the few plugin types the registry lacks. */
static int type_accounted(file_ctx_t *f, nmo_guid_t guid)
{
    if (type_of(f, guid) != NULL) {
        return 1;
    }
    if (is_plugin_type(guid)) {
        f->stats->plugin_types++;
        return 1;
    }
    return 0;
}

/* CKParameterManager::IsDerivedFrom. The engine's export derives every enum and flags type
   from Integer; the builtin enum registrations of libnmo carry no base type, so that edge is
   added here. */
static int type_derives_from(const file_ctx_t *f, const nmo_type_descriptor_t *type,
                             const nmo_type_descriptor_t *base)
{
    if (nmo_type_is_derived_from(f->types, type->id, base->id)) {
        return 1;
    }
    return (type->category & (NMO_TYPE_CATEGORY_ENUM | NMO_TYPE_CATEGORY_FLAGS)) != 0 &&
           nmo_guid_equals(base->guid, CKPGUID_INT);
}

/* CKParameterManager::IsTypeCompatible: the types are equal or derived one from the other. */
static int types_compatible(const file_ctx_t *f, nmo_guid_t a, nmo_guid_t b, int *judged)
{
    const nmo_type_descriptor_t *ta = type_of(f, a);
    const nmo_type_descriptor_t *tb = type_of(f, b);
    *judged = ta != NULL && tb != NULL;
    return !*judged || type_derives_from(f, ta, tb) || type_derives_from(f, tb, ta);
}

static int guid_is_none(nmo_guid_t guid)
{
    return nmo_guid_is_null(guid) || nmo_guid_equals(guid, CKPGUID_NONE);
}

static nmo_guid_t parameter_type_guid(const nmo_object_t *object)
{
    if (object->class_id == NMO_CID_PARAMETERIN) {
        const nmo_parameterin_state_t *state =
            (const nmo_parameterin_state_t *)nmo_object_get_state(object);
        return state != NULL ? state->type_guid : NMO_GUID_NULL;
    }
    const nmo_parameter_state_t *state = nmo_parameter_get_state(object);
    return state != NULL ? nmo_parameter_effective_type_guid(state) : NMO_GUID_NULL;
}

/* The role the list of a behavior gives an IO (0 input, 1 output, -1 none). */
static int io_role(const fact_t *io)
{
    if (io->holders[K_IO_IN] != 0) {
        return 0;
    }
    if (io->holders[K_IO_OUT] != 0) {
        return 1;
    }
    return -1;
}

static nmo_object_id_t io_owner(const fact_t *io)
{
    return io->holders[K_IO_IN] != 0 ? io->holder[K_IO_IN][0] : io->holder[K_IO_OUT][0];
}

/* The behavior `ancestor` lies above `id` on the m_BehParent chain. */
static int is_ancestor(const file_ctx_t *f, nmo_object_id_t ancestor, nmo_object_id_t id)
{
    const fact_t *cursor = fact_by_id(f, id);
    for (size_t steps = 0; cursor != NULL && cursor->holders[K_SUB] != 0 && steps <= f->fact_count;
         steps++) {
        const nmo_object_id_t parent = cursor->holder[K_SUB][0];
        if (parent == ancestor) {
            return 1;
        }
        cursor = fact_by_id(f, parent);
    }
    return 0;
}

static int on_one_chain(const file_ctx_t *f, const nmo_object_id_t *ids, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (ids[i] != ids[j] && !is_ancestor(f, ids[i], ids[j]) &&
                !is_ancestor(f, ids[j], ids[i])) {
                return 0;
            }
        }
    }
    return 1;
}

/* The behaviors that hold an object through one kind of container; an operation holds
   through the graph that lists it. Returns the count, or MAX_HOLDERS + 1 when truncated. */
static size_t gather_behaviors(const file_ctx_t *f, const fact_t *fact, member_kind_t kind,
                               nmo_object_id_t *out, size_t count)
{
    const size_t listed = fact->holders[kind] < MAX_HOLDERS ? fact->holders[kind] : MAX_HOLDERS;
    for (size_t i = 0; i < listed && count <= MAX_HOLDERS * 3; i++) {
        nmo_object_id_t id = fact->holder[kind][i];
        if (kind == K_OP_IN || kind == K_OP_OUT) {
            const fact_t *op = fact_by_id(f, id);
            if (op == NULL || op->holders[K_OP] == 0) {
                continue;
            }
            id = op->holder[K_OP][0];
        }
        out[count++] = id;
    }
    return fact->holders[kind] > MAX_HOLDERS ? MAX_HOLDERS * 3 + 1 : count;
}

/* ------------------------------------------------------------------ */
/* CKBehavior                                                          */
/* ------------------------------------------------------------------ */

typedef struct array_spec {
    const nmo_array_t *array;
    nmo_class_id_t class_id;
    uint32_t save_bit;
    const char *name;
} array_spec_t;

static void check_behavior_arrays(file_ctx_t *f, const nmo_object_t *object,
                                  const nmo_behavior_state_t *state)
{
    const array_spec_t specs[] = {
        {&state->sub_behaviors, NMO_CID_BEHAVIOR, CK_STATESAVE_BEHAVIORSUBBEHAV, "sub-behaviors"},
        {&state->sub_behavior_links, NMO_CID_BEHAVIORLINK, CK_STATESAVE_BEHAVIORSUBLINKS, "links"},
        {&state->operations, NMO_CID_PARAMETEROPERATION, CK_STATESAVE_BEHAVIOROPERATIONS,
         "operations"},
        {&state->in_parameters, NMO_CID_PARAMETERIN, CK_STATESAVE_BEHAVIORINPARAMS,
         "in-parameters"},
        {&state->out_parameters, NMO_CID_PARAMETEROUT, CK_STATESAVE_BEHAVIOROUTPARAMS,
         "out-parameters"},
        {&state->local_parameters, NMO_CID_PARAMETERLOCAL, CK_STATESAVE_BEHAVIORLOCALPARAMS,
         "local-parameters"},
        {&state->inputs, NMO_CID_BEHAVIORIO, CK_STATESAVE_BEHAVIORINPUTS, "inputs"},
        {&state->outputs, NMO_CID_BEHAVIORIO, CK_STATESAVE_BEHAVIOROUTPUTS, "outputs"},
    };
    int save_flags_match = 1;
    for (size_t s = 0; s < sizeof(specs) / sizeof(specs[0]); s++) {
        const array_spec_t *spec = &specs[s];
        if (spec->array->data != NULL) {
            const nmo_behavior_ref_t *refs = NMO_ARRAY_DATA(nmo_behavior_ref_t, spec->array);
            for (size_t i = 0; i < spec->array->count; i++) {
                /* CKBehavior keeps typed pointer arrays (CKBehaviorIO *, CKParameterIn *, ...). */
                CHECK(f, INV_BEH_ARRAY_CLASSES, ref_has_class(f, &refs[i].ref, spec->class_id),
                      object, "%s[%zu] state %d id %u is not a class %u object", spec->name, i,
                      (int)refs[i].ref.state, (unsigned)refs[i].ref.raw_id,
                      (unsigned)spec->class_id);
            }
        }
        /* CKBehavior::Save sets a save bit for exactly the arrays that hold something. */
        if (((state->save_flags & spec->save_bit) != 0) != (spec->array->count != 0)) {
            save_flags_match = 0;
        }
    }
    if (state->has_save_flags) {
        CHECK(f, INV_BEH_SAVE_FLAGS, save_flags_match, object,
              "save flags 0x%x against %zu/%zu/%zu/%zu/%zu/%zu/%zu/%zu listed",
              (unsigned)state->save_flags, state->sub_behaviors.count,
              state->sub_behavior_links.count, state->operations.count,
              state->in_parameters.count, state->out_parameters.count,
              state->local_parameters.count, state->inputs.count, state->outputs.count);
    }
}

static void check_behavior_proto(file_ctx_t *f, const nmo_object_t *object,
                                 const nmo_behavior_state_t *state)
{
    const nmo_behavior_proto_t *proto = nmo_behavior_registry_find(f->bbs, state->block_guid);
    if (proto == NULL) {
        return;
    }
    if (proto->version != state->block_version) {
        /* A block saved by an older version may differ from the registry's prototype. */
        f->stats->proto_version_skipped++;
        return;
    }
    /* CKBehavior::InitFctPtrFromPrototype ORs the prototype's behavior flags into m_Flags (the
       bits that only exist while a behavior runs, such as ACTIVE, are not kept). The counts
       and types of the IOs and parameters cannot be compared the same way: the corpus spans
       several library releases and a prototype can change without a new version number
       (blocks already placed in a file keep the layout they were created with). */
    const uint32_t kept = proto->behavior_flags & ~(BEH_USEFUNCTION | BEH_BUILDINGBLOCK |
                                                    BEH_RUNTIME_BITS);
    CHECK(f, INV_BEH_PROTO_FLAGS, (state->flags & kept) == kept, object,
          "'%s': flags 0x%x lack prototype flags 0x%x", proto->name != NULL ? proto->name : "?",
          (unsigned)state->flags, (unsigned)kept);
}

static void check_behavior(file_ctx_t *f, fact_t *fact)
{
    const nmo_object_t *object = fact->object;
    const nmo_behavior_state_t *state = behavior_state(object);
    if (state == NULL) {
        return;
    }
    const int is_bb = (state->flags & BEH_BUILDINGBLOCK) != 0;
    const int is_script = (state->flags & BEH_SCRIPT) != 0;
    const int is_sub = fact->holders[K_SUB] != 0;

    if (state->has_save_flags) {
        /* UseFunction sets BUILDINGBLOCK|USEFUNCTION, UseGraph clears both; Save writes m_Flags. */
        CHECK(f, INV_BEH_FUNCTION_FLAGS,
              ((state->flags & BEH_BUILDINGBLOCK) != 0) == ((state->flags & BEH_USEFUNCTION) != 0),
              object, "flags 0x%x", (unsigned)state->flags);
    }

    if (is_bb) {
        /* A function behavior stores its prototype in m_BlockData->m_Guid (a bare CKBehavior is a
           function with a null GUID until InitFromPrototype; the editor never saves one). */
        CHECK(f, INV_BEH_BB_GUID, !nmo_guid_is_null(state->block_guid), object,
              "building block without prototype GUID");
        /* InitFromPrototype stores the declaration's version, 0x10000 when it has none, and
           Load turns a stored 0 into 0x10000 for old chunks. */
        CHECK(f, INV_BEH_BB_VERSION, state->block_version != 0, object,
              "building block with version 0");
        /* UseFunction deletes m_GraphData, so a block never has graph data to save. */
        CHECK(f, INV_BEH_BB_NO_GRAPH,
              state->sub_behaviors.count == 0 && state->sub_behavior_links.count == 0 &&
                  state->operations.count == 0 &&
                  (!state->has_save_flags ||
                   (state->save_flags &
                    (CK_STATESAVE_BEHAVIORSUBBEHAV | CK_STATESAVE_BEHAVIORSUBLINKS |
                     CK_STATESAVE_BEHAVIOROPERATIONS)) == 0),
              object, "%zu sub-behaviors, %zu links, %zu operations, save flags 0x%x",
              state->sub_behaviors.count, state->sub_behavior_links.count,
              state->operations.count, (unsigned)state->save_flags);
    }

    check_behavior_arrays(f, object, state);

    /* m_CompatibleClassID is a CK_CLASSID: Load reads it as is and AddScript/AddSubBehavior test
       it with CKIsChildClassOf, which only knows the classes of the class table. */
    CHECK(f, INV_BEH_COMPAT_CLASS,
          nmo_type_query_class_name_from_id(f->types, (nmo_class_id_t)state->compatible_class_id) !=
              NULL,
          object, "compatible class %d", (int)state->compatible_class_id);

    /* CKBehavior::Save writes the scene's object description of the behavior: m_Global
       (CK_SCENEOBJECT_* bits, CheckBehaviorActivity clears the ACTIVE one) plus 0x80 for the
       initial value. */
    if (state->has_single_activity) {
        const uint32_t known = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_ACTIVE |
                               CK_SCENEOBJECT_START_DEACTIVATE | CK_SCENEOBJECT_START_LEAVE |
                               CK_SCENEOBJECT_START_RESET | CK_SCENEOBJECT_INTERNAL_IC;
        CHECK(f, INV_BEH_SINGLE_ACTIVITY, (state->single_activity_flags & ~known) == 0, object,
              "scene activity flags 0x%x", (unsigned)state->single_activity_flags);
    }

    /* AddSubBehavior and SetPriority sort m_SubBehaviors with BehaviorPrioritySort, which puts
       the larger priority first. */
    if (state->sub_behaviors.count > 1) {
        int ordered = 1;
        int varied = 0;
        const nmo_behavior_ref_t *subs = NMO_ARRAY_DATA(nmo_behavior_ref_t, &state->sub_behaviors);
        int32_t previous = 0;
        for (size_t i = 0; i < state->sub_behaviors.count; i++) {
            const fact_t *sub = resolved_fact(f, &subs[i].ref);
            const nmo_behavior_state_t *sub_state = sub != NULL ? behavior_state(sub->object) : NULL;
            if (sub_state == NULL) {
                continue;
            }
            if (i > 0 && sub_state->priority > previous) {
                ordered = 0;
            }
            if (i > 0 && sub_state->priority != previous) {
                varied = 1;
            }
            previous = sub_state->priority;
        }
        if (varied) {
            CHECK(f, INV_BEH_SUB_ORDER, ordered, object, "%zu sub-behaviors not in priority order",
                  state->sub_behaviors.count);
        }
    }

    /* UseTarget creates the target pIn outside m_InParameter, and Save writes it only for
       TARGETABLE behaviors. (A graph may export the target to its own parameters.) */
    {
        const int target_set = state->target_parameter.state != NMO_REF_NONE;
        const fact_t *target = resolved_fact(f, &state->target_parameter);
        if (target_set || (state->flags & BEH_TARGETABLE) != 0) {
            CHECK(f, INV_BEH_TARGET,
                  !target_set ||
                      ((state->flags & BEH_TARGETABLE) != 0 && target != NULL &&
                       target->object->class_id == NMO_CID_PARAMETERIN &&
                       !nmo_behavior_ref_array_find(&state->in_parameters, target->object->id,
                                                    NULL) &&
                       target->holders[K_TARGET] == 1),
                  object, "target ref state %d, flags 0x%x", (int)state->target_parameter.state,
                  (unsigned)state->flags);
        }
        if (target != NULL && target->object->class_id == NMO_CID_PARAMETERIN) {
            /* UseTarget gives the target pIn the parameter type of the compatible class
               (ClassIDToGuid); the editor may narrow either of the two afterwards, but they stay
               on one derivation chain. */
            const nmo_type_descriptor_t *class_type =
                nmo_type_query_find_by_class_id(f->types, (nmo_class_id_t)state->compatible_class_id);
            const nmo_guid_t target_guid = parameter_type_guid(target->object);
            int judged = 0;
            const int compatible =
                class_type != NULL && types_compatible(f, class_type->guid, target_guid, &judged);
            if (judged) {
                CHECK(f, INV_BEH_TARGET_TYPE, compatible, object,
                      "compatible class %d (type %08X-%08X), target type %08X-%08X",
                      (int)state->compatible_class_id, (unsigned)class_type->guid.d1,
                      (unsigned)class_type->guid.d2, (unsigned)target_guid.d1,
                      (unsigned)target_guid.d2);
            }
        }
    }

    /* The constructor sets TOPMOST; AddSubBehavior and HierarchyPostLoad clear it for subs. */
    CHECK(f, INV_BEH_SUB_NOT_TOPMOST, !is_sub || (state->flags & BEH_TOPMOST) == 0, object,
          "sub-behavior with flags 0x%x", (unsigned)state->flags);

    if (is_script) {
        /* A script is a root: GetOwnerScript walks m_BehParent up to a script. */
        CHECK(f, INV_BEH_SCRIPT_TOPLEVEL, !is_sub && (state->flags & BEH_TOPMOST) != 0, object,
              "script with flags 0x%x listed as sub-behavior %d times", (unsigned)state->flags,
              (int)fact->holders[K_SUB]);
    }

    /* m_BehParent is one slot, so one parent; following it must end at a script. */
    {
        const fact_t *root = fact;
        size_t steps = 0;
        int cyclic = 0;
        while (root->holders[K_SUB] != 0) {
            const fact_t *parent = fact_by_id(f, root->holder[K_SUB][0]);
            if (parent == NULL) {
                break;
            }
            if (++steps > f->fact_count) {
                cyclic = 1;
                break;
            }
            root = parent;
        }
        const nmo_behavior_state_t *root_state = behavior_state(root->object);
        CHECK(f, INV_BEH_PARENT_CHAIN,
              fact->holders[K_SUB] <= 1 && !cyclic && root_state != NULL &&
                  (root_state->flags & BEH_SCRIPT) != 0,
              object, "%d parents, %zu steps up, root flags 0x%x%s", (int)fact->holders[K_SUB],
              steps, root_state != NULL ? (unsigned)root_state->flags : 0u,
              cyclic ? " (cycle)" : "");
    }

    if (is_script) {
        /* m_Owner is one slot (CKBeObject::AddScript/ApplyOwner). A library file may hold a
           script nobody owns. */
        CHECK(f, INV_BEH_SCRIPT_OWNER, fact->holders[K_SCRIPT] <= 1, object,
              "listed by %d CKBeObjects", (int)fact->holders[K_SCRIPT]);
        if (fact->holders[K_SCRIPT] == 0) {
            f->stats->unowned_scripts++;
        } else if (fact->holders[K_SCRIPT] == 1 && state->target_parameter.state == NMO_REF_NONE) {
            /* CKBeObject::AddScript refuses an owner whose class is not a child of the
               compatible class (error -52) unless the script takes its target as an input. */
            const fact_t *owner = fact_by_id(f, fact->holder[K_SCRIPT][0]);
            CHECK(f, INV_BEH_SCRIPT_COMPAT,
                  owner != NULL && class_derives(f, owner->object->class_id,
                                                 (nmo_class_id_t)state->compatible_class_id),
                  object, "owner class %u, compatible class %d",
                  owner != NULL ? (unsigned)owner->object->class_id : 0u,
                  (int)state->compatible_class_id);
        }
    }

    if (is_bb) {
        check_behavior_proto(f, object, state);
    }
}

static const fact_t *script_root(const file_ctx_t *f, const fact_t *fact)
{
    size_t steps = 0;
    while (fact->holders[K_SUB] != 0) {
        const fact_t *parent = fact_by_id(f, fact->holder[K_SUB][0]);
        if (parent == NULL || ++steps > f->fact_count) {
            return NULL;
        }
        fact = parent;
    }
    return fact;
}

/* CKBeObject::AddScript keeps m_ScriptArray sorted with BehaviorPrioritySort, larger priority
   first; the editor attaches only scripts (CKBEHAVIOR_SCRIPT behaviors) to an object. */
static void check_beobject_scripts(file_ctx_t *f, fact_t *fact)
{
    const nmo_beobject_state_t *be = beobject_state(f, fact->object);
    if (be == NULL || be->scripts.count == 0 || be->scripts.data == NULL) {
        return;
    }
    const nmo_ref_t *scripts = NMO_ARRAY_DATA(nmo_ref_t, &be->scripts);
    int all_scripts = 1;
    int ordered = 1;
    int32_t previous = 0;
    for (size_t i = 0; i < be->scripts.count; i++) {
        const fact_t *script = resolved_fact(f, &scripts[i]);
        const nmo_behavior_state_t *script_state =
            script != NULL ? behavior_state(script->object) : NULL;
        if (script_state == NULL || (script_state->flags & BEH_SCRIPT) == 0) {
            all_scripts = 0;
            continue;
        }
        if (i > 0 && script_state->priority > previous) {
            ordered = 0;
        }
        previous = script_state->priority;
    }
    CHECK(f, INV_BEH_SCRIPT_LISTED, all_scripts && ordered, fact->object,
          "%zu scripts, all scripts %d, in priority order %d", be->scripts.count, all_scripts,
          ordered);
}

static void check_sub_compat(file_ctx_t *f, fact_t *fact)
{
    const nmo_behavior_state_t *state = behavior_state(fact->object);
    if (state == NULL || fact->holders[K_SUB] == 0 ||
        state->target_parameter.state != NMO_REF_NONE) {
        return;
    }
    const fact_t *root = script_root(f, fact);
    const nmo_behavior_state_t *root_state = root != NULL ? behavior_state(root->object) : NULL;
    if (root == NULL || root_state == NULL || (root_state->flags & BEH_SCRIPT) == 0 ||
        root->holders[K_SCRIPT] != 1) {
        return;
    }
    const fact_t *owner = fact_by_id(f, root->holder[K_SCRIPT][0]);
    if (owner == NULL) {
        return;
    }
    /* CKBehavior::AddSubBehavior: an owned graph takes a sub-behavior only when the owner's
       class is a child of the sub-behavior's compatible class (error -52 otherwise). */
    CHECK(f, INV_BEH_SUB_COMPAT,
          class_derives(f, owner->object->class_id, (nmo_class_id_t)state->compatible_class_id),
          fact->object, "owner class %u, compatible class %d", (unsigned)owner->object->class_id,
          (int)state->compatible_class_id);
}

/* ------------------------------------------------------------------ */
/* CKBehaviorIO, CKBehaviorLink                                        */
/* ------------------------------------------------------------------ */

static void check_io(file_ctx_t *f, fact_t *fact)
{
    const nmo_object_t *object = fact->object;
    const nmo_behaviorio_state_t *state =
        (const nmo_behaviorio_state_t *)nmo_object_get_state(object);
    if (state == NULL) {
        return;
    }
    /* CreateInput/CreateOutput set exactly one of the two bits; GetOldFlags adds 0x100 while
       the IO is active. */
    if (state->has_flags) {
        const uint32_t kind = state->old_flags & ~IO_OLD_ACTIVE;
        CHECK(f, INV_IO_FLAGS, kind == IO_OLD_IN || kind == IO_OLD_OUT, object,
              "stored flags 0x%x", (unsigned)state->old_flags);
        const int role = io_role(fact);
        if (role >= 0) {
            CHECK(f, INV_IO_LIST_FLAG,
                  (role == 0 && kind == IO_OLD_IN) || (role == 1 && kind == IO_OLD_OUT), object,
                  "listed as %s of behavior %u, stored flags 0x%x", role == 0 ? "input" : "output",
                  (unsigned)io_owner(fact), (unsigned)state->old_flags);
        }
    }
    /* m_OwnerBehavior is one slot: CreateInput/CreateOutput add the IO to one array. */
    CHECK(f, INV_IO_SINGLE_OWNER, fact->holders[K_IO_IN] + fact->holders[K_IO_OUT] == 1, object,
          "listed %d times as input, %d times as output", (int)fact->holders[K_IO_IN],
          (int)fact->holders[K_IO_OUT]);
}

static void check_link(file_ctx_t *f, fact_t *fact)
{
    const nmo_object_t *object = fact->object;
    const nmo_behaviorlink_state_t *state =
        (const nmo_behaviorlink_state_t *)nmo_object_get_state(object);
    if (state == NULL) {
        return;
    }
    const fact_t *source = resolved_fact(f, &state->in_io);
    const fact_t *dest = resolved_fact(f, &state->out_io);
    const int classes_ok = source != NULL && source->object->class_id == NMO_CID_BEHAVIORIO &&
                           dest != NULL && dest->object->class_id == NMO_CID_BEHAVIORIO;
    CHECK(f, INV_LINK_ENDPOINT_CLASS, classes_ok, object,
          "in_io state %d id %u, out_io state %d id %u", (int)state->in_io.state,
          (unsigned)state->in_io.raw_id, (int)state->out_io.state, (unsigned)state->out_io.raw_id);

    if (classes_ok) {
        /* CKBehavior::AddSubBehaviorLink puts the link in one graph's list. */
        const fact_t *graph = fact->holders[K_LINK] == 1 ? fact_by_id(f, fact->holder[K_LINK][0])
                                                         : NULL;
        int in_graph = 0;
        int direction = 0;
        if (graph != NULL) {
            const nmo_object_id_t graph_id = graph->object->id;
            const fact_t *source_owner = fact_by_id(f, io_owner(source));
            const fact_t *dest_owner = fact_by_id(f, io_owner(dest));
            const int source_in = source_owner != NULL &&
                                  (source_owner->object->id == graph_id ||
                                   (source_owner->holders[K_SUB] != 0 &&
                                    source_owner->holder[K_SUB][0] == graph_id));
            const int dest_in = dest_owner != NULL &&
                                (dest_owner->object->id == graph_id ||
                                 (dest_owner->holders[K_SUB] != 0 &&
                                  dest_owner->holder[K_SUB][0] == graph_id));
            in_graph = source_in && dest_in;
            if (in_graph) {
                /* CheckIOsActivation/FindNextBehaviorsToExecute: a link starts at an output of
                   a sub-behavior or at an input of the graph, and ends at an input of a
                   sub-behavior or an output of the graph. */
                const int source_is_graph = source_owner->object->id == graph_id;
                const int dest_is_graph = dest_owner->object->id == graph_id;
                direction = io_role(source) == (source_is_graph ? 0 : 1) &&
                            io_role(dest) == (dest_is_graph ? 1 : 0);
            }
        }
        CHECK(f, INV_LINK_IN_GRAPH, in_graph, object,
              "listed by %d graphs, endpoints owned by %u and %u", (int)fact->holders[K_LINK],
              (unsigned)io_owner(source), (unsigned)io_owner(dest));
        if (in_graph) {
            CHECK(f, INV_LINK_DIRECTION, direction, object,
                  "in_io role %d of behavior %u, out_io role %d of behavior %u in graph %u",
                  io_role(source), (unsigned)io_owner(source), io_role(dest),
                  (unsigned)io_owner(dest), (unsigned)graph->object->id);
        }
    }

    /* The countdown starts from the initial delay (Reset, FindNextBehaviorsToExecute) and
       CheckBehaviorActivity decrements it, clamped at zero. */
    CHECK(f, INV_LINK_DELAY,
          state->activation_delay >= 0 &&
              state->activation_delay <= state->initial_activation_delay,
          object, "delay %d, initial delay %d", (int)state->activation_delay,
          (int)state->initial_activation_delay);
}

/* ------------------------------------------------------------------ */
/* Parameters                                                          */
/* ------------------------------------------------------------------ */

/* Several holders are fine when they are behaviors on one parent chain (the graphs export
   what a descendant owns). Returns 1 when `ids` is such a set. */
static int exported_only_by_ancestors(const file_ctx_t *f, const nmo_object_id_t *ids,
                                      size_t count)
{
    return count <= MAX_HOLDERS * 3 && on_one_chain(f, ids, count);
}

static void check_pin(file_ctx_t *f, fact_t *fact)
{
    const nmo_object_t *object = fact->object;
    const nmo_parameterin_state_t *state =
        (const nmo_parameterin_state_t *)nmo_object_get_state(object);
    if (state == NULL) {
        return;
    }

    /* The type GUID names a parameter type of the registry. */
    CHECK(f, INV_PIN_TYPE_KNOWN, type_accounted(f, state->type_guid), object, "type GUID %08X-%08X",
          (unsigned)state->type_guid.d1, (unsigned)state->type_guid.d2);

    /* m_Owner is one slot (UseTarget, Update, HierarchyPostLoad): a pin has one home, an
       in-parameter list, a target slot or an operation. ExportInputParameter lets graphs
       above it list it as well. */
    {
        nmo_object_id_t holders[MAX_HOLDERS * 3 + 2];
        size_t count = gather_behaviors(f, fact, K_PIN, holders, 0);
        count = gather_behaviors(f, fact, K_TARGET, holders, count);
        count = gather_behaviors(f, fact, K_OP_IN, holders, count);
        const size_t homes = (size_t)fact->holders[K_TARGET] + fact->holders[K_OP_IN];
        CHECK(f, INV_PIN_HOLDERS,
              fact->holders[K_PIN] + homes >= 1 && homes <= 1 &&
                  exported_only_by_ancestors(f, holders, count),
              object, "listed %d times as in-parameter, %d as target, %d as operation input",
              (int)fact->holders[K_PIN], (int)fact->holders[K_TARGET], (int)fact->holders[K_OP_IN]);
        if (fact->holders[K_PIN] + homes > 1) {
            f->stats->exported_params++;
        }
    }

    if (state->source.state == NMO_REF_NONE) {
        return;
    }
    const fact_t *source = resolved_fact(f, &state->source);
    if (state->is_shared) {
        /* ShareSourceWith takes a CKParameterIn. */
        CHECK(f, INV_PIN_SOURCE_CLASS,
              source != NULL && source->object->class_id == NMO_CID_PARAMETERIN, object,
              "shared source state %d id %u", (int)state->source.state,
              (unsigned)state->source.raw_id);
    } else {
        /* SetDirectSource takes a CKParameter (out, local or attribute value). */
        CHECK(f, INV_PIN_SOURCE_CLASS, source != NULL && is_parameter_class(source->object->class_id),
              object, "direct source state %d id %u", (int)state->source.state,
              (unsigned)state->source.raw_id);
    }
    if (source == NULL) {
        return;
    }

    /* ShareSourceWith/SetDirectSource return -40 unless the two types are derived one from
       the other (IsDerivedFrom both ways, i.e. IsTypeCompatible). */
    const nmo_guid_t source_guid = parameter_type_guid(source->object);
    if (!guid_is_none(source_guid) && !guid_is_none(state->type_guid)) {
        int judged = 0;
        const int compatible = types_compatible(f, state->type_guid, source_guid, &judged);
        if (judged) {
            CHECK(f, INV_PIN_SOURCE_TYPE, compatible, object,
                  "pin type %08X-%08X, source type %08X-%08X", (unsigned)state->type_guid.d1,
                  (unsigned)state->type_guid.d2, (unsigned)source_guid.d1, (unsigned)source_guid.d2);
        }
    }

    /* GetRealSource follows m_InShared until a direct source; it would loop on a cycle. */
    if (state->is_shared) {
        const fact_t *cursor = fact;
        size_t steps = 0;
        int terminated = 0;
        while (steps++ <= f->fact_count) {
            const nmo_parameterin_state_t *cursor_state =
                (const nmo_parameterin_state_t *)nmo_object_get_state(cursor->object);
            if (cursor_state == NULL || cursor->object->class_id != NMO_CID_PARAMETERIN) {
                break;
            }
            const fact_t *next = resolved_fact(f, &cursor_state->source);
            if (!cursor_state->is_shared) {
                terminated = cursor_state->source.state == NMO_REF_NONE ||
                             (next != NULL && is_parameter_class(next->object->class_id));
                break;
            }
            if (next == NULL) {
                terminated = cursor_state->source.state == NMO_REF_NONE;
                break;
            }
            cursor = next;
        }
        CHECK(f, INV_PIN_SHARED_CHAIN, terminated, object, "shared chain of %zu pins", steps);
    }
}

static int is_myself_local(const nmo_object_t *object)
{
    if (object->class_id != NMO_CID_PARAMETERLOCAL) {
        return 0;
    }
    const nmo_parameterlocal_state_t *state =
        (const nmo_parameterlocal_state_t *)nmo_object_get_state(object);
    return state != NULL && state->is_myself;
}

static void check_pout(file_ctx_t *f, fact_t *fact)
{
    const nmo_object_t *object = fact->object;
    const nmo_parameterout_state_t *state =
        (const nmo_parameterout_state_t *)nmo_object_get_state(object);
    if (state == NULL) {
        return;
    }
    const nmo_guid_t guid = nmo_parameter_effective_type_guid(&state->base);
    CHECK(f, INV_POUT_TYPE_KNOWN, type_accounted(f, guid), object, "type GUID %08X-%08X",
          (unsigned)guid.d1, (unsigned)guid.d2);

    /* An out is a behavior output (HierarchyPostLoad), an operation result (Update) or the
       value of an attribute or array cell; a graph above may export it (ExportOutputParameter). */
    {
        nmo_object_id_t holders[MAX_HOLDERS * 3 + 2];
        size_t count = gather_behaviors(f, fact, K_POUT, holders, 0);
        count = gather_behaviors(f, fact, K_OP_OUT, holders, count);
        const size_t homes = (size_t)fact->holders[K_OP_OUT] + fact->holders[K_FOREIGN];
        CHECK(f, INV_POUT_HOLDERS,
              fact->holders[K_POUT] + homes >= 1 && homes <= 1 &&
                  exported_only_by_ancestors(f, holders, count),
              object, "listed %d times as behavior output, %d as operation result, %d by other objects",
              (int)fact->holders[K_POUT], (int)fact->holders[K_OP_OUT], (int)fact->holders[K_FOREIGN]);
        if (fact->holders[K_POUT] + homes > 1) {
            f->stats->exported_params++;
        }
    }

    for (uint32_t d = 0; d < state->destination_count; d++) {
        const nmo_ref_t *ref = &state->destination_ids[d];
        const fact_t *dest = resolved_fact(f, ref);
        /* AddDestination takes a CKParameter and rejects a duplicate (AddIfNotHere). */
        int once = 1;
        for (uint32_t e = 0; e < d; e++) {
            if (state->destination_ids[e].state == NMO_REF_RESOLVED &&
                ref->state == NMO_REF_RESOLVED && state->destination_ids[e].id == ref->id) {
                once = 0;
            }
        }
        CHECK(f, INV_POUT_DEST, dest != NULL && is_parameter_class(dest->object->class_id) && once,
              object, "destination %u state %d id %u%s", (unsigned)d, (int)ref->state,
              (unsigned)ref->raw_id, once ? "" : " listed twice");
        if (dest == NULL || !is_parameter_class(dest->object->class_id)) {
            continue;
        }
        /* AddDestination: -40 for a 'myself' parameter, -1 when the destination is an out that
           already lists this one. */
        int backedge = dest->object->id == object->id || is_myself_local(dest->object);
        if (dest->object->class_id == NMO_CID_PARAMETEROUT) {
            const nmo_parameterout_state_t *dest_state =
                (const nmo_parameterout_state_t *)nmo_object_get_state(dest->object);
            for (uint32_t e = 0; dest_state != NULL && e < dest_state->destination_count; e++) {
                if (dest_state->destination_ids[e].state == NMO_REF_RESOLVED &&
                    dest_state->destination_ids[e].id == object->id) {
                    backedge = 1;
                }
            }
        }
        CHECK(f, INV_POUT_DEST_BACKEDGE, !backedge, object,
              "destination %u (id %u) is this parameter, a 'myself' local or lists it back",
              (unsigned)d, (unsigned)dest->object->id);
        /* DataChanged copies the value to every destination, which needs compatible types. */
        const nmo_guid_t dest_guid = parameter_type_guid(dest->object);
        if (!guid_is_none(guid) && !guid_is_none(dest_guid)) {
            int judged = 0;
            const int compatible = types_compatible(f, guid, dest_guid, &judged);
            if (judged) {
                CHECK(f, INV_POUT_DEST_TYPE, compatible, object,
                      "type %08X-%08X, destination type %08X-%08X", (unsigned)guid.d1,
                      (unsigned)guid.d2, (unsigned)dest_guid.d1, (unsigned)dest_guid.d2);
            }
        }
    }
}

/* Fixed-size parameter types: CKParameter::SetType allocates the type's default size and
   CKParameter::Save writes m_Size bytes, so a stored value of one of these is exactly this long. */
typedef struct fixed_type {
    nmo_guid_t guid;
    size_t size;
} fixed_type_t;

static size_t fixed_size_of(nmo_guid_t guid)
{
    static const fixed_type_t fixed[] = {
        {CKPGUID_INT_INIT, 4},         {CKPGUID_FLOAT_INIT, 4},    {CKPGUID_BOOL_INIT, 4},
        {CKPGUID_ANGLE_INIT, 4},       {CKPGUID_PERCENTAGE_INIT, 4}, {CKPGUID_TIME_INIT, 4},
        {CKPGUID_2DVECTOR_INIT, 8},    {CKPGUID_VECTOR_INIT, 12},  {CKPGUID_EULERANGLES_INIT, 12},
        {CKPGUID_QUATERNION_INIT, 16}, {CKPGUID_RECT_INIT, 16},    {CKPGUID_COLOR_INIT, 16},
        {CKPGUID_BOX_INIT, 24},        {CKPGUID_MATRIX_INIT, 64},
    };
    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) {
        if (nmo_guid_equals(fixed[i].guid, guid)) {
            return fixed[i].size;
        }
    }
    return 0;
}

static void check_plocal(file_ctx_t *f, fact_t *fact)
{
    const nmo_object_t *object = fact->object;
    const nmo_parameterlocal_state_t *state =
        (const nmo_parameterlocal_state_t *)nmo_object_get_state(object);
    if (state == NULL) {
        return;
    }
    const nmo_guid_t guid = nmo_parameter_effective_type_guid(&state->base);

    if (state->is_myself) {
        /* CKParameterLocal::Save writes a 'myself' parameter with CKObject::Save only; Load
           rebuilds its type and value from the owner (SetAsMyselfParameter). */
        CHECK(f, INV_PLOCAL_MYSELF,
              !state->base.has_state && nmo_guid_is_null(state->base.type_guid), object,
              "'myself' local stores a value of type %08X-%08X",
              (unsigned)state->base.type_guid.d1, (unsigned)state->base.type_guid.d2);
    } else {
        CHECK(f, INV_PLOCAL_TYPE_KNOWN, type_accounted(f, guid), object, "type GUID %08X-%08X",
              (unsigned)guid.d1, (unsigned)guid.d2);
    }

    /* AddLocalParameter makes the behavior the owner (m_Owner is one slot); a script may also
       list the local of a block it contains. A local nobody lists stays alive as a pin source. */
    {
        nmo_object_id_t holders[MAX_HOLDERS * 3 + 2];
        const size_t count = gather_behaviors(f, fact, K_PLOCAL, holders, 0);
        int self_managed = 0;
        for (size_t h = 0; h < count; h++) {
            const fact_t *holder = fact_by_id(f, holders[h]);
            const nmo_behavior_state_t *holder_state =
                holder != NULL ? behavior_state(holder->object) : NULL;
            /* A block that creates its own local parameters (INTERNALLYCREATEDLOCALPARAMS) owns
               what its callback adds, so other blocks may list the same local. */
            self_managed |= holder_state != NULL &&
                            (holder_state->flags & CKBEHAVIOR_INTERNALLYCREATEDLOCALPARAMS) != 0;
        }
        if (count == 0) {
            f->stats->orphan_locals++;
        } else {
            const int on_chain = exported_only_by_ancestors(f, holders, count);
            if (!on_chain && self_managed) {
                f->stats->self_managed_locals++;
            }
            CHECK(f, INV_PLOCAL_HOLDERS, on_chain || self_managed, object,
                  "listed by %d behaviors that are not on one parent chain",
                  (int)fact->holders[K_PLOCAL]);
        }
    }

    /* IsLocalParameterSetting is only defined for a behavior with block data. */
    if (state->is_setting && fact->holders[K_PLOCAL] != 0) {
        const fact_t *owner = fact_by_id(f, fact->holder[K_PLOCAL][0]);
        const nmo_behavior_state_t *owner_state = owner != NULL ? behavior_state(owner->object) : NULL;
        CHECK(f, INV_PLOCAL_SETTING,
              owner_state != NULL && (owner_state->flags & BEH_BUILDINGBLOCK) != 0, object,
              "setting parameter of a non building block");
    }
}

static mode_record_t *mode_record_for(corpus_semantics_t *stats, nmo_guid_t guid)
{
    for (size_t i = 0; i < stats->mode_records; i++) {
        if (nmo_guid_equals(stats->mode[i].type_guid, guid)) {
            return &stats->mode[i];
        }
    }
    if (stats->mode_records == MAX_MODE_RECORDS) {
        stats->mode_records_dropped++;
        return NULL;
    }
    mode_record_t *record = &stats->mode[stats->mode_records++];
    memset(record, 0, sizeof(*record));
    record->type_guid = guid;
    return record;
}

/* The storage of a parameter value: its buffer, object reference and mode. */
static void check_parameter_storage(file_ctx_t *f, fact_t *fact)
{
    const nmo_object_t *object = fact->object;
    const nmo_parameter_state_t *state = nmo_parameter_get_state(object);
    if (state == NULL || !state->has_state) {
        return;
    }
    const nmo_guid_t guid = nmo_parameter_effective_type_guid(state);

    if (state->mode == CKPARAM_MODE_OBJECT) {
        /* CKParameter::Save writes a stored object with WriteObject: a null reference or an
           object of the file. The engine does not check its class against the parameter type (a
           block that declares a Sprite output may store a plain 2D entity), so none is checked
           here. */
        const nmo_ref_t *ref = &state->object_ref;
        CHECK(f, INV_PARAM_OBJECT_VALUE,
              ref->state == NMO_REF_NONE ||
                  ((ref->state == NMO_REF_RESOLVED || ref->state == NMO_REF_CLASS_MISMATCH) &&
                   fact_by_id(f, ref->id) != NULL),
              object, "object value state %d id %u", (int)ref->state, (unsigned)ref->raw_id);
    }

    /* An attribute or array cell is filled by plugin code with SetValue(buffer, size), which
       resizes m_Size, so only the parameters of a behavior keep the size of their type. */
    if (state->mode == CKPARAM_MODE_BUFFER && fact->holders[K_FOREIGN] == 0) {
        const size_t expected = fixed_size_of(guid);
        if (expected != 0) {
            CHECK(f, INV_PARAM_SIZE, state->buffer_data.count == expected, object,
                  "type %08X-%08X holds %zu bytes, expected %zu", (unsigned)guid.d1,
                  (unsigned)guid.d2, state->buffer_data.count, expected);
        }
    }

    /* CKParameter::Save picks the storage from the type alone: a type whose class derives from
       CKObject is stored as an object reference (mode 2), every other type as a buffer, a
       sub-chunk or a manager integer; mode 3 marks a value without data. */
    const nmo_type_descriptor_t *type = type_of(f, guid);
    if (type != NULL && state->mode != CKPARAM_MODE_NONE) {
        const int object_type = type->category == NMO_TYPE_CATEGORY_OBJECT_REF;
        CHECK(f, INV_PARAM_MODE_TYPE, object_type == (state->mode == CKPARAM_MODE_OBJECT), object,
              "type '%s' stored in mode %d", type->name != NULL ? type->name : "?",
              (int)state->mode);
    }

    mode_record_t *record = mode_record_for(f->stats, guid);
    if (record != NULL && state->mode != CKPARAM_MODE_NONE && (int)state->mode < 5) {
        record->modes[state->mode]++;
        if (state->mode == CKPARAM_MODE_MANAGER) {
            if (!record->has_manager) {
                record->manager_guid = state->manager_guid;
                record->has_manager = 1;
            } else if (!nmo_guid_equals(record->manager_guid, state->manager_guid)) {
                record->manager_conflict = 1;
            }
        }
    }
}

/* Once every file has been seen: each type used one storage mode and one manager. */
static void check_storage_stable(file_ctx_t *f)
{
    corpus_semantics_t *stats = f->stats;
    for (size_t i = 0; i < stats->mode_records; i++) {
        const mode_record_t *record = &stats->mode[i];
        size_t used = 0;
        for (int m = 0; m < 5; m++) {
            used += record->modes[m] != 0;
        }
        if (used == 0) {
            continue;
        }
        CHECK(f, INV_PARAM_MODE_STABLE, used == 1 && !record->manager_conflict, NULL,
              "type %08X-%08X stored in %zu modes, manager conflict %d",
              (unsigned)record->type_guid.d1, (unsigned)record->type_guid.d2, used,
              record->manager_conflict);
    }
}

typedef struct signature_probe {
    const file_ctx_t *f;
    const nmo_type_descriptor_t *p1;
    const nmo_type_descriptor_t *p2;
    const nmo_type_descriptor_t *result;
    int found;
} signature_probe_t;

static int type_derives_guid(const file_ctx_t *f, const nmo_type_descriptor_t *type,
                             nmo_guid_t base_guid)
{
    const nmo_type_descriptor_t *base = type_of(f, base_guid);
    return base != NULL && type_derives_from(f, type, base);
}

static nmo_status_t signature_cb(const nmo_operation_tree_cell_t *cell, void *user)
{
    signature_probe_t *probe = (signature_probe_t *)user;
    if (type_derives_guid(probe->f, probe->p1, cell->desc.p1_type_guid) &&
        type_derives_guid(probe->f, probe->p2, cell->desc.p2_type_guid) &&
        type_derives_guid(probe->f, probe->result, cell->desc.result_type_guid)) {
        probe->found = 1;
    }
    return NMO_OK;
}

static void check_operation(file_ctx_t *f, fact_t *fact)
{
    const nmo_object_t *object = fact->object;
    const nmo_parameteroperation_state_t *state =
        (const nmo_parameteroperation_state_t *)nmo_object_get_state(object);
    if (state == NULL) {
        return;
    }
    /* Reconstruct builds both inputs with CreateCKParameterIn and the result with
       CreateCKParameterOut; Save writes the three references in that order. */
    CHECK(f, INV_OP_ROLES,
          ref_has_class(f, &state->in1.ref, NMO_CID_PARAMETERIN) &&
              ref_has_class(f, &state->in2.ref, NMO_CID_PARAMETERIN) &&
              ref_has_class(f, &state->out.ref, NMO_CID_PARAMETEROUT),
          object, "in1 state %d, in2 state %d, out state %d", (int)state->in1.ref.state,
          (int)state->in2.ref.state, (int)state->out.ref.state);

    /* AddParameterOperation/HierarchyPostLoad give an operation one owner graph. */
    CHECK(f, INV_OP_LISTED, fact->holders[K_OP] == 1, object, "listed by %d graphs",
          (int)fact->holders[K_OP]);

    const fact_t *in1 = resolved_fact(f, &state->in1.ref);
    const fact_t *in2 = resolved_fact(f, &state->in2.ref);
    const fact_t *out = resolved_fact(f, &state->out.ref);
    if (in1 == NULL || in2 == NULL || out == NULL || in1->object->class_id != NMO_CID_PARAMETERIN ||
        in2->object->class_id != NMO_CID_PARAMETERIN ||
        out->object->class_id != NMO_CID_PARAMETEROUT) {
        return;
    }

    /* Update makes the operation the owner of its three pins. They are three objects used by
       this operation only; the graph that owns the operation (or one above it) may export
       them (ExportInputParameter/ExportOutputParameter). */
    {
        int pins_ok = in1 != in2 && in1->holders[K_OP_IN] == 1 && in2->holders[K_OP_IN] == 1 &&
                      out->holders[K_OP_OUT] == 1 && in1->holders[K_TARGET] == 0 &&
                      in2->holders[K_TARGET] == 0;
        const fact_t *pins[3] = {in1, in2, out};
        const member_kind_t lists[3] = {K_PIN, K_PIN, K_POUT};
        const fact_t *graph = fact->holders[K_OP] == 1 ? fact_by_id(f, fact->holder[K_OP][0]) : NULL;
        for (int p = 0; p < 3 && pins_ok; p++) {
            const size_t listed = pins[p]->holders[lists[p]] < MAX_HOLDERS
                                      ? pins[p]->holders[lists[p]]
                                      : MAX_HOLDERS;
            for (size_t h = 0; h < listed; h++) {
                const nmo_object_id_t exporter = pins[p]->holder[lists[p]][h];
                if (graph == NULL ||
                    (exporter != graph->object->id && !is_ancestor(f, exporter, graph->object->id))) {
                    pins_ok = 0;
                }
            }
        }
        CHECK(f, INV_OP_PINS, pins_ok, object,
              "pin lists %d/%d/%d, operation inputs %d/%d, results %d", (int)in1->holders[K_PIN],
              (int)in2->holders[K_PIN], (int)out->holders[K_POUT], (int)in1->holders[K_OP_IN],
              (int)in2->holders[K_OP_IN], (int)out->holders[K_OP_OUT]);
    }

    /* Update looks the implementation up with (result, in1, in2) and, failing that, with the
       inputs swapped; GetOperationFunction retries with each type replaced by its parent. When
       nothing is found DoOperation copies in1 to the result (CopyValue, which needs compatible
       types). */
    const nmo_parameterin_state_t *s1 =
        (const nmo_parameterin_state_t *)nmo_object_get_state(in1->object);
    const nmo_parameterin_state_t *s2 =
        (const nmo_parameterin_state_t *)nmo_object_get_state(in2->object);
    const nmo_parameter_state_t *so = nmo_parameter_get_state(out->object);
    const nmo_operation_family_t *family =
        s1 != NULL && s2 != NULL && so != NULL
            ? nmo_operation_registry_get_family(f->ops, &state->operation_guid)
            : NULL;
    if (family == NULL) {
        return;
    }
    const nmo_guid_t out_guid = nmo_parameter_effective_type_guid(so);
    const nmo_type_descriptor_t *t1 = type_of(f, s1->type_guid);
    const nmo_type_descriptor_t *t2 = type_of(f, s2->type_guid);
    const nmo_type_descriptor_t *to = type_of(f, out_guid);
    if (t1 == NULL || t2 == NULL || to == NULL) {
        return;
    }
    signature_probe_t probe = {f, t1, t2, to, 0};
    nmo_operation_family_enumerate(family, signature_cb, &probe);
    if (!probe.found) {
        probe.p1 = t2;
        probe.p2 = t1;
        nmo_operation_family_enumerate(family, signature_cb, &probe);
    }
    int found = probe.found;
    if (!found) {
        int judged = 0;
        found = types_compatible(f, s1->type_guid, out_guid, &judged);
        f->stats->ops_no_signature_copied++;
    }
    CHECK(f, INV_OP_SIGNATURE, found, object,
          "operation %08X-%08X ('%s'): %08X-%08X, %08X-%08X -> %08X-%08X",
          (unsigned)state->operation_guid.d1, (unsigned)state->operation_guid.d2,
          family->name != NULL ? family->name : "?", (unsigned)s1->type_guid.d1,
          (unsigned)s1->type_guid.d2, (unsigned)s2->type_guid.d1, (unsigned)s2->type_guid.d2,
          (unsigned)out_guid.d1, (unsigned)out_guid.d2);
}

/* ------------------------------------------------------------------ */
/* InterfaceChunk                                                      */
/* ------------------------------------------------------------------ */

static const fact_t *iface_fact(const file_ctx_t *f, nmo_object_id_t id, int runtime_ids)
{
    nmo_object_t *object = runtime_ids ? nmo_object_repository_find_by_id(f->repo, id)
                                       : nmo_object_repository_find_by_file_id(f->repo, id);
    if (object == NULL) {
        object = runtime_ids ? nmo_object_repository_find_by_file_id(f->repo, id)
                             : nmo_object_repository_find_by_id(f->repo, id);
    }
    return object != NULL ? fact_by_id(f, object->id) : NULL;
}

static size_t count_descendants(const file_ctx_t *f, const fact_t *fact, size_t depth)
{
    const nmo_behavior_state_t *state = behavior_state(fact->object);
    size_t total = 0;
    if (state == NULL || state->sub_behaviors.data == NULL || depth > f->fact_count) {
        return 0;
    }
    const nmo_behavior_ref_t *refs = NMO_ARRAY_DATA(nmo_behavior_ref_t, &state->sub_behaviors);
    for (size_t i = 0; i < state->sub_behaviors.count; i++) {
        const fact_t *sub = resolved_fact(f, &refs[i].ref);
        if (sub != NULL) {
            total += 1 + count_descendants(f, sub, depth + 1);
        }
    }
    return total;
}

static int finite_all(const float *values, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (!isfinite(values[i])) {
            return 0;
        }
    }
    return 1;
}

/* The parameter an interface endpoint names: slot `index` of a behavior's list, or the
   result and inputs of an operation. */
static nmo_object_id_t endpoint_parameter(const fact_t *owner, uint32_t type, int32_t index)
{
    if (owner == NULL) {
        return NMO_OBJECT_ID_NONE;
    }
    if (owner->object->class_id == NMO_CID_BEHAVIOR) {
        const nmo_behavior_state_t *state = behavior_state(owner->object);
        if (state == NULL) {
            return NMO_OBJECT_ID_NONE;
        }
        if (type == NMO_INTERFACE_ENDPOINT_TARGET_PIN) {
            return nmo_behavior_target_parameter_id(state);
        }
        if (index < 0) {
            return NMO_OBJECT_ID_NONE;
        }
        switch (type) {
        case NMO_INTERFACE_ENDPOINT_PIN:
            return nmo_behavior_ref_array_get_id(&state->in_parameters, (size_t)index);
        case NMO_INTERFACE_ENDPOINT_POUT:
            return nmo_behavior_ref_array_get_id(&state->out_parameters, (size_t)index);
        case NMO_INTERFACE_ENDPOINT_PLOCAL:
            return nmo_behavior_ref_array_get_id(&state->local_parameters, (size_t)index);
        default:
            return NMO_OBJECT_ID_NONE;
        }
    }
    if (owner->object->class_id == NMO_CID_PARAMETEROPERATION) {
        const nmo_parameteroperation_state_t *state =
            (const nmo_parameteroperation_state_t *)nmo_object_get_state(owner->object);
        if (state == NULL) {
            return NMO_OBJECT_ID_NONE;
        }
        if (type == NMO_INTERFACE_ENDPOINT_POUT && index == 0) {
            return nmo_parameteroperation_out_id(state);
        }
        if (type == NMO_INTERFACE_ENDPOINT_PIN && index == 0) {
            return nmo_parameteroperation_in1_id(state);
        }
        if (type == NMO_INTERFACE_ENDPOINT_PIN && index == 1) {
            return nmo_parameteroperation_in2_id(state);
        }
    }
    return NMO_OBJECT_ID_NONE;
}

/* The IO a behavior-link endpoint names: output `index` for BOUT, input `index` otherwise. */
static nmo_object_id_t endpoint_io(const fact_t *owner, const nmo_interface_endpoint_t *end)
{
    const nmo_behavior_state_t *state = owner != NULL ? behavior_state(owner->object) : NULL;
    if (state == NULL || end->index < 0) {
        return NMO_OBJECT_ID_NONE;
    }
    return nmo_behavior_ref_array_get_id(
        end->type == NMO_INTERFACE_ENDPOINT_BOUT ? &state->outputs : &state->inputs,
        (size_t)end->index);
}

static void check_interface_body(file_ctx_t *f, const nmo_object_t *script_object,
                                 const nmo_interface_body_t *body, const fact_t *owner,
                                 int runtime_ids)
{
    const nmo_behavior_state_t *state = owner != NULL ? behavior_state(owner->object) : NULL;
    if (state == NULL || !body->has_body) {
        return;
    }

    /* The editor draws one link per CKBehaviorLink of the graph. */
    size_t behavior_links = 0;
    int links_ok = 1;
    for (size_t i = 0; i < body->link_count; i++) {
        const nmo_interface_link_t *link = &body->links[i];
        if (link->type != NMO_INTERFACE_LINK_BEHAVIOR) {
            continue;
        }
        behavior_links++;
        const fact_t *link_fact = iface_fact(f, link->link_id, runtime_ids);
        if (link_fact == NULL || link_fact->object->class_id != NMO_CID_BEHAVIORLINK ||
            !nmo_behavior_ref_array_find(&state->sub_behavior_links, link_fact->object->id, NULL)) {
            links_ok = 0;
        }
    }
    for (size_t i = 0; links_ok && i < state->sub_behavior_links.count; i++) {
        const nmo_object_id_t graph_link =
            nmo_behavior_ref_array_get_id(&state->sub_behavior_links, i);
        int drawn = 0;
        for (size_t k = 0; k < body->link_count && !drawn; k++) {
            const fact_t *link_fact = body->links[k].type == NMO_INTERFACE_LINK_BEHAVIOR
                                          ? iface_fact(f, body->links[k].link_id, runtime_ids)
                                          : NULL;
            drawn = link_fact != NULL && link_fact->object->id == graph_link;
        }
        links_ok &= drawn;
    }
    CHECK(f, INV_IF_LINKS, links_ok && behavior_links == state->sub_behavior_links.count,
          owner->object, "%zu behavior links drawn, graph has %zu", behavior_links,
          state->sub_behavior_links.count);

    /* A behavior-link end names (behavior, index); the IO there is the link's own IO. A
       START_BIN end names the graph's own input by index. */
    for (size_t i = 0; i < body->link_count; i++) {
        const nmo_interface_link_t *link = &body->links[i];
        const fact_t *link_fact = link->type == NMO_INTERFACE_LINK_BEHAVIOR
                                      ? iface_fact(f, link->link_id, runtime_ids)
                                      : NULL;
        if (link_fact == NULL || link_fact->object->class_id != NMO_CID_BEHAVIORLINK) {
            continue;
        }
        const nmo_behaviorlink_state_t *link_state =
            (const nmo_behaviorlink_state_t *)nmo_object_get_state(link_fact->object);
        const fact_t *start_owner = link->start.type == NMO_INTERFACE_ENDPOINT_START_BIN
                                        ? owner
                                        : iface_fact(f, link->start.id, runtime_ids);
        const fact_t *end_owner = iface_fact(f, link->end.id, runtime_ids);
        nmo_interface_endpoint_t start = link->start;
        if (start.type == NMO_INTERFACE_ENDPOINT_START_BIN) {
            start.type = NMO_INTERFACE_ENDPOINT_BIN;
        }
        CHECK(f, INV_IF_LINK_ENDS,
              link_state != NULL && endpoint_io(start_owner, &start) != NMO_OBJECT_ID_NONE &&
                  endpoint_io(start_owner, &start) == nmo_behaviorlink_in_io_id(link_state) &&
                  endpoint_io(end_owner, &link->end) != NMO_OBJECT_ID_NONE &&
                  endpoint_io(end_owner, &link->end) == nmo_behaviorlink_out_io_id(link_state),
              owner->object,
              "link %u: start (%u, %d, type %u), end (%u, %d, type %u) against IOs %u -> %u",
              (unsigned)link->link_id, (unsigned)link->start.id, (int)link->start.index,
              (unsigned)link->start.type, (unsigned)link->end.id, (int)link->end.index,
              (unsigned)link->end.type,
              link_state != NULL ? (unsigned)nmo_behaviorlink_in_io_id(link_state) : 0u,
              link_state != NULL ? (unsigned)nmo_behaviorlink_out_io_id(link_state) : 0u);
    }

    /* A parameter link end names a slot that exists (the editor draws the pins of the graph). */
    for (size_t i = 0; i < body->link_count; i++) {
        const nmo_interface_link_t *link = &body->links[i];
        if (link->type != NMO_INTERFACE_LINK_PARAMETER) {
            continue;
        }
        const nmo_interface_endpoint_t *ends[2] = {&link->start, &link->end};
        for (int e = 0; e < 2; e++) {
            const uint32_t type = ends[e]->type;
            if (type != NMO_INTERFACE_ENDPOINT_PIN && type != NMO_INTERFACE_ENDPOINT_POUT &&
                type != NMO_INTERFACE_ENDPOINT_PLOCAL &&
                type != NMO_INTERFACE_ENDPOINT_TARGET_PIN) {
                continue;
            }
            const fact_t *end_owner = iface_fact(f, ends[e]->id, runtime_ids);
            CHECK(f, INV_IF_PARAM_LINK_ENDS,
                  endpoint_parameter(end_owner, type, ends[e]->index) != NMO_OBJECT_ID_NONE,
                  owner->object, "parameter link end (%u, %d, type %u) names no parameter",
                  (unsigned)ends[e]->id, (int)ends[e]->index, (unsigned)type);
        }
    }

    /* The editor draws operations of the graph (it hides some, so not all of them). */
    for (size_t i = 0; i < body->operation_count; i++) {
        const fact_t *op = iface_fact(f, body->operations[i].id, runtime_ids);
        CHECK(f, INV_IF_OPS,
              op != NULL && op->object->class_id == NMO_CID_PARAMETEROPERATION &&
                  nmo_behavior_ref_array_find(&state->operations, op->object->id, NULL),
              owner->object, "operation %u is not an operation of the graph",
              (unsigned)body->operations[i].id);
    }

    /* One local parameter position per local parameter of the graph. */
    if (body->has_params) {
        CHECK(f, INV_IF_LOCALS, body->params.local_count == state->local_parameters.count,
              owner->object, "%zu local positions, graph has %zu local parameters",
              body->params.local_count, state->local_parameters.count);
    }

    int finite = 1;
    for (size_t i = 0; i < body->link_count; i++) {
        finite &= finite_all(body->links[i].points, body->links[i].point_count * 2);
    }
    for (size_t i = 0; i < body->operation_count; i++) {
        finite &= isfinite(body->operations[i].h_pos) && isfinite(body->operations[i].v_pos);
    }
    for (size_t i = 0; i < body->comment_count; i++) {
        finite &= isfinite(body->comments[i].left) && isfinite(body->comments[i].top) &&
                  isfinite(body->comments[i].right) && isfinite(body->comments[i].bottom);
    }
    CHECK(f, INV_IF_FINITE, finite, script_object, "non-finite link point, operation or comment");
}

static void check_interface(file_ctx_t *f, fact_t *fact)
{
    const nmo_behavior_state_t *state = behavior_state(fact->object);
    if (state == NULL || state->interface_data == NULL) {
        return;
    }
    const nmo_interface_data_t *data = state->interface_data;
    const int runtime_ids = state->interface_ids_are_runtime;

    /* The editor writes a script header for a script and the header of a sub-behavior for a
       graph that is not one (the chunk of a script is rewritten whenever it changes). */
    CHECK(f, INV_IF_ROOT_KIND, data->script.graph_root == ((state->flags & CKBEHAVIOR_SCRIPT) == 0u),
          fact->object, "root header kind against the behavior's script flag");
    if (data->script.graph_root) {
        /* The chunk of a graph below a script is not kept in step with the graph (the editor
           only refreshes the script's), so only its numbers can be checked. */
        int graph_finite = isfinite(data->script.h_pos) && isfinite(data->script.v_pos) &&
                           isfinite(data->script.h_size) && isfinite(data->script.v_size) &&
                           isfinite(data->script.h_expand_size) &&
                           isfinite(data->script.v_expand_size);
        for (size_t k = 0; k < data->sub_count; k++) {
            const nmo_interface_behavior_t *sub = &data->subs[k];
            graph_finite &= isfinite(sub->h_pos) && isfinite(sub->v_pos) &&
                            isfinite(sub->h_size) && isfinite(sub->v_size);
        }
        CHECK(f, INV_IF_FINITE, graph_finite, fact->object, "non-finite position or size");
        return;
    }

    /* The sub-behavior entries are all the behaviors below the graph, each once. */
    int subs_ok = data->sub_count == count_descendants(f, fact, 0);
    for (size_t k = 0; k < data->sub_count; k++) {
        const fact_t *sub = iface_fact(f, data->subs[k].behavior_id, runtime_ids);
        subs_ok &= sub != NULL && sub->object->class_id == NMO_CID_BEHAVIOR &&
                   is_ancestor(f, fact->object->id, sub->object->id);
        for (size_t j = 0; j < k; j++) {
            subs_ok &= data->subs[j].behavior_id != data->subs[k].behavior_id;
        }
    }
    CHECK(f, INV_IF_SUBS, subs_ok, fact->object, "%zu sub-behavior entries, %zu behaviors below",
          data->sub_count, count_descendants(f, fact, 0));

    int finite = isfinite(data->script.h_pos) && isfinite(data->script.v_pos) &&
                 isfinite(data->script.h_start_pos) && isfinite(data->script.v_start_pos) &&
                 isfinite(data->script.v_size);
    for (size_t k = 0; k < data->sub_count; k++) {
        const nmo_interface_behavior_t *sub = &data->subs[k];
        finite &= isfinite(sub->h_pos) && isfinite(sub->v_pos) && isfinite(sub->h_size) &&
                  isfinite(sub->v_size) && isfinite(sub->h_expand_size) &&
                  isfinite(sub->v_expand_size);
    }
    CHECK(f, INV_IF_FINITE, finite, fact->object, "non-finite position or size");

    check_interface_body(f, fact->object, &data->script.body, fact, runtime_ids);
    for (size_t k = 0; k < data->sub_count; k++) {
        check_interface_body(f, fact->object, &data->subs[k].body,
                             iface_fact(f, data->subs[k].behavior_id, runtime_ids), runtime_ids);
    }
}

/* ------------------------------------------------------------------ */
/* Walk                                                                */
/* ------------------------------------------------------------------ */

static void check_file(const char *path, void *user)
{
    corpus_semantics_t *stats = (corpus_semantics_t *)user;
    stats->files++;

    nmo_session_t *session = nmo_session_create(stats->ctx);
    if (session == NULL || nmo_session_load_file(session, path, NULL, NULL) != NMO_OK) {
        stats->load_errors++;
        printf("  %s: load failed\n", path);
        nmo_session_destroy(session);
        return;
    }

    file_ctx_t file;
    memset(&file, 0, sizeof(file));
    file.stats = stats;
    file.path = path;
    file.repo = nmo_session_get_repository(session);
    file.arena = nmo_session_get_arena(session);
    file.types = nmo_context_get_type_registry(stats->ctx);
    file.ops = nmo_context_get_operation_registry(stats->ctx);
    file.bbs = nmo_context_get_bb_registry(stats->ctx);

    nmo_behavior_interface_parse_stats_t parse_stats;
    (void)nmo_behavior_parse_all_interfaces_ex(file.repo, NULL, &parse_stats);
    stats->interfaces += parse_stats.parsed_count;
    stats->interface_parse_failed += parse_stats.failed_count;

    if (build_facts(&file) != 0) {
        stats->load_errors++;
        printf("  %s: out of memory\n", path);
    } else {
        collect_membership(&file);
        for (size_t i = 0; i < file.fact_count; i++) {
            fact_t *fact = &file.facts[i];
            switch (fact->object->class_id) {
            case NMO_CID_BEHAVIOR:
                check_behavior(&file, fact);
                check_sub_compat(&file, fact);
                check_interface(&file, fact);
                break;
            case NMO_CID_BEHAVIORIO:
                check_io(&file, fact);
                break;
            case NMO_CID_BEHAVIORLINK:
                check_link(&file, fact);
                break;
            case NMO_CID_PARAMETERIN:
                check_pin(&file, fact);
                break;
            case NMO_CID_PARAMETER:
                check_parameter_storage(&file, fact);
                break;
            case NMO_CID_PARAMETEROUT:
                check_pout(&file, fact);
                check_parameter_storage(&file, fact);
                break;
            case NMO_CID_PARAMETERLOCAL:
                check_plocal(&file, fact);
                check_parameter_storage(&file, fact);
                break;
            case NMO_CID_PARAMETEROPERATION:
                check_operation(&file, fact);
                break;
            default:
                break;
            }
            check_beobject_scripts(&file, fact);
        }
    }

    free(file.facts);
    free(file.slots);
    nmo_session_destroy(session);
}

TEST(corpus_semantics_behavior, decoded_graphs_and_parameters_satisfy_engine_guarantees)
{
    /* Corpus scan: skip when the gitignored Virtools sample set is absent. */
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");

    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    corpus_semantics_t *stats = (corpus_semantics_t *)calloc(1, sizeof(*stats));
    ASSERT_NOT_NULL(stats);
    stats->ctx = ctx;
    int walk_status = test_corpus_walk(NMO_TEST_DATA_DIR, check_file, stats);
    file_ctx_t corpus;
    memset(&corpus, 0, sizeof(corpus));
    corpus.stats = stats;
    corpus.path = "(whole corpus)";
    check_storage_stable(&corpus);
    nmo_context_release(ctx);

    printf("  Corpus semantics: files=%zu load_errors=%zu\n", stats->files, stats->load_errors);
    size_t failed = 0;
    for (int i = 0; i < INV_COUNT; i++) {
        const int bad = stats->checked[i] == 0 || stats->violated[i] != 0;
        printf("  %s %-22s checked=%-8zu violated=%-6zu %s\n", bad ? "FAIL" : "ok  ", inv_name[i],
               stats->checked[i], stats->violated[i], inv_text[i]);
        failed += bad;
    }
    printf("  Not judged: plugin_types=%zu proto_version_skipped=%zu "
           "unowned_scripts=%zu orphan_locals=%zu "
           "self_managed_locals=%zu exported_params=%zu ops_without_signature_copied=%zu\n",
           stats->plugin_types, stats->proto_version_skipped, stats->unowned_scripts,
           stats->orphan_locals,
           stats->self_managed_locals, stats->exported_params, stats->ops_no_signature_copied);
    printf("  Interfaces: parsed=%zu not_parsed=%zu (old-layout chunks the parser rejects)\n",
           stats->interfaces, stats->interface_parse_failed);

    ASSERT_EQ(0, walk_status);
    ASSERT_GE(stats->files, 1u);
    ASSERT_EQ(0u, stats->load_errors);
    ASSERT_EQ(0u, failed);
    free(stats);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(corpus_semantics_behavior, decoded_graphs_and_parameters_satisfy_engine_guarantees);
TEST_MAIN_END()
