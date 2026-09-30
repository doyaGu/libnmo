/**
 * @file nmo_cmd_animation.c
 * @brief CLI animation command group implementation
 */

#include "nmo_cmd_animation.h"
#include "nmo_cmd_object_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_write.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_record.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "runtime/nmo_context.h"
#include "document/nmo_document_save.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "core/nmo_arena.h"
#include "core/nmo_parse.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

static int nmo_cmd_animation_export_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

int nmo_cmd_animation_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: animation list|show|keys|export ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "list") == 0 || strcmp(argv[0], "ls") == 0) {
        return nmo_cmd_object_list_class_in_session(ctx, argc, argv, "CKObjectAnimation");
    }
    if (strcmp(argv[0], "show") == 0 || strcmp(argv[0], "s") == 0 ||
        strcmp(argv[0], "keys") == 0 || strcmp(argv[0], "k") == 0) {
        return nmo_cmd_object_show_class_in_session(
            ctx, argc, argv, NMO_CID_OBJECTANIMATION, "CKObjectAnimation");
    }
    if (strcmp(argv[0], "export") == 0 || strcmp(argv[0], "x") == 0) {
        return nmo_cmd_animation_export_in_session(ctx, argc, argv);
    }

    fprintf(stderr, "Unsupported animation read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}

/* ============================================================================
 * Helpers
 * ============================================================================ */

static const char *controller_type_name(uint32_t type) {
    if (nmo_objanim_controller_is_bezier(type)) {
        return "bezier";
    }
    if (type == NMO_OBJANIM_CONTROLLER_MORPH) {
        return "morph";
    }
    switch (nmo_objanim_controller_key_size(type)) {
        case 16: return "position/scale";
        case 20: return "rotation";
        case 36: return "tcb-pos/scl";
        case 40: return "tcb-rot";
        default: return "unknown";
    }
}

/* The state keeps a morph controller as one blob with key_count 0; its key
 * count is the first dword of the blob. */
static uint32_t controller_key_count(const nmo_objanim_controller_t *ctrl) {
    nmo_objanim_morph_info_t morph;
    return nmo_objanim_morph_controller_info(ctrl, &morph)
        ? morph.key_count : ctrl->key_count;
}

static const char *animation_format_name(nmo_objectanimation_format_t fmt) {
    switch (fmt) {
        case CKOBJANIM_FORMAT_NONE:        return "NONE";
        case CKOBJANIM_FORMAT_SHARED:      return "SHARED";
        case CKOBJANIM_FORMAT_CONTROLLERS: return "CONTROLLERS";
        case CKOBJANIM_FORMAT_NEWDATA:     return "NEWDATA";
        case CKOBJANIM_FORMAT_LEGACY:      return "LEGACY";
        default:                           return "unknown";
    }
}

static const nmo_class_id_t animation_class_ids[] = {
    NMO_CID_ANIMATION,
    NMO_CID_KEYEDANIMATION,
    NMO_CID_OBJECTANIMATION,
};

static const nmo_class_id_t object_animation_class_ids[] = {
    NMO_CID_OBJECTANIMATION,
};

/** Check if an object is an animation-related class */
static bool is_animation_class(const nmo_cmd_ctx_t *c, nmo_class_id_t cid) {
    if (cid == NMO_CID_OBJECTANIMATION || cid == NMO_CID_KEYEDANIMATION)
        return true;
    if (c->registry &&
        nmo_type_registry_is_class_derived_from(c->registry, cid, NMO_CID_ANIMATION))
        return true;
    return false;
}

static bool animation_query_predicate(const nmo_object_t *obj, void *user_data) {
    const nmo_cmd_ctx_t *c = (const nmo_cmd_ctx_t *)user_data;
    if (!obj || !c) return false;
    return is_animation_class(c, nmo_object_get_class_id(obj));
}

/* ============================================================================
 * animation list
 * ============================================================================ */

typedef struct animation_list_data {
    nmo_cli_record_t **items;
    size_t count;
    size_t capacity;
} animation_list_data_t;

static bool animation_list_build_record(const nmo_cmd_ctx_t *c,
                                        nmo_object_t *obj,
                                        nmo_cli_record_t *rec)
{
    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    const char *cls = nmo_core_class_name(c, cid);
    const char *name = nmo_object_get_name(obj);

    bool ok = nmo_cli_record_uint(rec, "id", "ID", nmo_object_get_id(obj));
    ok = ok && (cls ? nmo_cli_record_str(rec, "class", "Class", cls)
                    : nmo_cli_record_str_fmt(rec, "class", "Class", "Class#%u", (unsigned)cid));
    ok = ok && nmo_cli_record_str(rec, "name", "Name", name);
    if (ok && (!name || !name[0])) {
        ok = nmo_cli_record_set_text(rec, "-");
    }

    if (cid == NMO_CID_OBJECTANIMATION) {
        nmo_objectanimation_state_t *st =
            (nmo_objectanimation_state_t *)nmo_object_get_state(obj);
        if (st) {
            if (st->has_length) {
                ok = ok && nmo_cli_record_real(rec, "length", "Length",
                                               (double)st->length, "%.1f");
            } else {
                ok = ok && nmo_cli_record_real(rec, "length", NULL, 0.0, NULL);
                ok = ok && nmo_cli_record_text(rec, "Length", "-");
            }
            ok = ok && nmo_cli_record_text(rec, "FPS", "-");
            const nmo_object_id_t entity_id = nmo_ref_runtime_id(&st->entity);
            if (entity_id != NMO_OBJECT_ID_NONE) {
                ok = ok && nmo_cli_record_uint(rec, "entity_id", "Target", entity_id);
            } else {
                ok = ok && nmo_cli_record_text(rec, "Target", "-");
            }
            return ok;
        }
    } else {
        nmo_animation_state_t *st =
            (nmo_animation_state_t *)nmo_object_get_state(obj);
        if (st) {
            if (st->has_length) {
                ok = ok && nmo_cli_record_real(rec, "length", "Length",
                                               (double)st->length, "%.1f");
            } else {
                ok = ok && nmo_cli_record_real(rec, "length", NULL, 0.0, NULL);
                ok = ok && nmo_cli_record_text(rec, "Length", "-");
            }
            if (st->has_data) {
                ok = ok && nmo_cli_record_real(rec, "frame_rate", "FPS",
                                               (double)st->frame_rate, "%.1f");
            } else {
                ok = ok && nmo_cli_record_real(rec, "frame_rate", NULL, 0.0, NULL);
                ok = ok && nmo_cli_record_text(rec, "FPS", "-");
            }
            if (st->has_root_entity &&
                st->root_entity.state == NMO_REF_RESOLVED) {
                ok = ok && nmo_cli_record_text_fmt(rec, "Target", "%u", st->root_entity.id);
            } else {
                ok = ok && nmo_cli_record_text(rec, "Target", "-");
            }
            return ok;
        }
    }
    /* No deserialized state: text placeholders only. */
    ok = ok && nmo_cli_record_text(rec, "Length", "-");
    ok = ok && nmo_cli_record_text(rec, "FPS", "-");
    ok = ok && nmo_cli_record_text(rec, "Target", "-");
    return ok;
}

static int animation_list_visitor(size_t index,
                                  nmo_object_t *obj,
                                  const nmo_cmd_ctx_t *c,
                                  void *user)
{
    (void)index;
    animation_list_data_t *data = (animation_list_data_t *)user;
    if (obj == NULL || data == NULL) {
        return 0;
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL && animation_list_build_record(c, obj, rec);
    if (ok && data->count == data->capacity) {
        size_t capacity = data->capacity ? data->capacity * 2u : 16u;
        nmo_cli_record_t **grown = (nmo_cli_record_t **)realloc(
            data->items, capacity * sizeof(*grown));
        ok = grown != NULL;
        if (ok) {
            data->items = grown;
            data->capacity = capacity;
        }
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        return 0;
    }
    data->items[data->count++] = rec;
    return 0;
}

static const nmo_cli_table_col_t animation_list_columns[] = {
    {"ID",     NMO_CLI_ALIGN_RIGHT, 6,  0},
    {"Class",  NMO_CLI_ALIGN_LEFT,  18, 0},
    {"Name",   NMO_CLI_ALIGN_LEFT,  20, 50},
    {"Length",  NMO_CLI_ALIGN_RIGHT, 8,  0},
    {"FPS",    NMO_CLI_ALIGN_RIGHT, 6,  0},
    {"Target", NMO_CLI_ALIGN_RIGHT, 8,  0},
};

/* Takes ownership of the collected items. */
static nmo_cli_record_t *animation_list_record_new(animation_list_data_t *data)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "count", NULL, data->count) &&
              nmo_cli_record_raw_fmt(rec, "Animations: %zu\n\n", data->count);
    nmo_cli_record_array_t *animations =
        ok ? nmo_cli_record_array(rec, "animations", NULL) : NULL;
    ok = ok && animations != NULL &&
         nmo_cli_record_array_set_table(
             animations, animation_list_columns,
             sizeof(animation_list_columns) / sizeof(animation_list_columns[0]));
    for (size_t i = 0; i < data->count; ++i) {
        if (ok) {
            ok = nmo_cli_record_array_add(animations, data->items[i]);
        } else {
            nmo_cli_record_free(data->items[i]);
        }
    }
    free(data->items);
    data->items = NULL;
    data->count = 0;
    data->capacity = 0;
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

int nmo_cmd_animation_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_query_t query = {
        .predicate = animation_query_predicate,
        .predicate_user_data = &c,
    };

    animation_list_data_t ld = {0};
    rc = nmo_core_object_query_run(&c, &query, animation_list_visitor, &ld, NULL);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        for (size_t i = 0; i < ld.count; ++i) {
            nmo_cli_record_free(ld.items[i]);
        }
        free(ld.items);
        return nmo_cmd_ctx_done(&c, rc);
    }

    rc = nmo_cmd_ctx_emit_record(&c, animation_list_record_new(&ld),
                                 "animation.list", 0, c.colorize);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * animation show
 * ============================================================================ */

/* Flags: unsigned in JSON, lowercase hex in text. */
static bool animation_record_flags(nmo_cli_record_t *rec, uint32_t flags)
{
    return nmo_cli_record_uint(rec, "flags", "Flags", flags) &&
           nmo_cli_record_set_text_fmt(rec, "0x%08x", flags);
}

static bool animation_show_build_record(const nmo_cmd_ctx_t *c,
                                        nmo_cli_record_t *rec,
                                        nmo_object_t *obj,
                                        nmo_object_id_t obj_id,
                                        nmo_class_id_t cid,
                                        const char *cls,
                                        const char *name)
{
    bool ok = nmo_cli_record_uint(rec, "id", "ID", obj_id);
    ok = ok && (cls ? nmo_cli_record_str(rec, "class", "Class", cls)
                    : nmo_cli_record_str_fmt(rec, "class", "Class", "Class#%u", (unsigned)cid));
    ok = ok && nmo_cli_record_str(rec, "name", "Name", name);
    if (ok && (!name || !name[0])) {
        ok = nmo_cli_record_set_text(rec, "-");
    }
    if (!ok) {
        return false;
    }

    if (cid == NMO_CID_OBJECTANIMATION) {
        nmo_objectanimation_state_t *st =
            (nmo_objectanimation_state_t *)nmo_object_get_state(obj);
        if (!st) {
            return true;
        }
        ok = nmo_cli_record_str(rec, "format", "Format", animation_format_name(st->format));
        ok = ok && animation_record_flags(rec, st->flags);
        const nmo_object_id_t entity_id = nmo_ref_runtime_id(&st->entity);
        if (entity_id != NMO_OBJECT_ID_NONE) {
            ok = ok && nmo_cli_record_uint(rec, "entity_id", "Entity ID", entity_id);
        }
        if (st->has_length) {
            ok = ok && nmo_cli_record_real(rec, "length", "Length", (double)st->length, "%.2f");
        }
        ok = ok && nmo_cli_record_uint(rec, "controller_count", "Controllers",
                                       st->controller_count);
        for (uint32_t ci = 0; ok && ci < st->controller_count; ++ci) {
            const nmo_objanim_controller_t *ctrl = &st->controllers[ci];
            ok = nmo_cli_record_text_fmt(rec, "", "type=0x%08x (%s), keys=%u, data=%u bytes",
                                         ctrl->type, controller_type_name(ctrl->type),
                                         controller_key_count(ctrl), ctrl->data_size) &&
                 nmo_cli_record_set_label_fmt(rec, "  [%u]", ci);
        }
        if (st->has_morph_counts) {
            ok = ok && nmo_cli_record_int(rec, "morph_vertex_count", NULL, st->morph_vertex_count);
            ok = ok && nmo_cli_record_int(rec, "morph_key_count", NULL, st->morph_key_count);
            ok = ok && nmo_cli_record_text_fmt(rec, "Morph", "%d vertices, %d keys",
                                               st->morph_vertex_count, st->morph_key_count);
        }
        if (st->has_merge) {
            const nmo_object_id_t anim1_id = nmo_ref_runtime_id(&st->anim1);
            const nmo_object_id_t anim2_id = nmo_ref_runtime_id(&st->anim2);
            ok = ok && nmo_cli_record_real(rec, "merge_factor", NULL, (double)st->merge_factor, NULL);
            if (anim1_id != NMO_OBJECT_ID_NONE) {
                ok = ok && nmo_cli_record_uint(rec, "anim1_id", NULL, anim1_id);
            }
            if (anim2_id != NMO_OBJECT_ID_NONE) {
                ok = ok && nmo_cli_record_uint(rec, "anim2_id", NULL, anim2_id);
            }
            ok = ok && nmo_cli_record_text_fmt(rec, "Merge", "factor=%.2f, anim1=%u, anim2=%u",
                                               (double)st->merge_factor, anim1_id, anim2_id);
        }
        return ok;
    }

    if (cid == NMO_CID_KEYEDANIMATION) {
        nmo_keyedanimation_state_t *st =
            (nmo_keyedanimation_state_t *)nmo_object_get_state(obj);
        if (!st) {
            return true;
        }
        if (st->base.has_data) {
            ok = animation_record_flags(rec, st->base.flags);
            ok = ok && nmo_cli_record_real(rec, "frame_rate", "Frame Rate",
                                           (double)st->base.frame_rate, "%.2f");
        }
        if (st->base.has_length) {
            ok = ok && nmo_cli_record_real(rec, "length", "Length",
                                           (double)st->base.length, "%.2f");
        }
        ok = ok && nmo_cli_record_uint(rec, "animation_count", "Animations",
                                       st->animation_count);
        nmo_cli_record_array_t *anims = nmo_cli_record_array(rec, "animations", NULL);
        ok = ok && anims != NULL;
        for (uint32_t ai = 0; ok && ai < st->animation_count; ++ai) {
            const nmo_object_id_t animation_id =
                nmo_ref_runtime_id(&st->animation_ids[ai]);
            if (animation_id == NMO_OBJECT_ID_NONE) continue;
            nmo_object_t *aobj = nmo_core_find_by_id(c, animation_id);
            const char *aname = aobj ? nmo_object_get_name(aobj) : NULL;
            nmo_cli_record_t *entry = nmo_cli_record_new();
            ok = entry != NULL &&
                 nmo_cli_record_uint(entry, "id", NULL, animation_id) &&
                 (!aobj || nmo_cli_record_str(entry, "name", NULL, aname)) &&
                 nmo_cli_record_array_add(anims, entry);
            if (!ok) break;
            ok = ((aname && aname[0])
                      ? nmo_cli_record_text_fmt(rec, "", "#%u (%s)", animation_id, aname)
                      : nmo_cli_record_text_fmt(rec, "", "#%u", animation_id)) &&
                 nmo_cli_record_set_label_fmt(rec, "  [%u]", ai);
        }
        ok = ok && nmo_cli_record_uint(rec, "subanim_count", "Subanims", st->subanim_count);
        if (st->has_merge) {
            ok = ok && nmo_cli_record_int(rec, "merged", NULL, st->merged);
            ok = ok && nmo_cli_record_real(rec, "merge_factor", NULL, (double)st->merge_factor, NULL);
            ok = ok && nmo_cli_record_text_fmt(rec, "Merge", "merged=%d, factor=%.2f",
                                               st->merged, (double)st->merge_factor);
        }
        return ok;
    }

    /* CKAnimation base */
    nmo_animation_state_t *st = (nmo_animation_state_t *)nmo_object_get_state(obj);
    if (!st) {
        return true;
    }
    if (st->has_data) {
        ok = animation_record_flags(rec, st->flags);
        ok = ok && nmo_cli_record_real(rec, "frame_rate", "Frame Rate",
                                       (double)st->frame_rate, "%.2f");
    }
    if (st->has_length) {
        ok = ok && nmo_cli_record_real(rec, "length", "Length", (double)st->length, "%.2f");
    }
    if (st->has_root_entity && st->root_entity.state == NMO_REF_RESOLVED) {
        ok = ok && nmo_cli_record_uint(rec, "root_entity_id", "Root Entity", st->root_entity.id);
    }
    if (st->has_character && st->character.state == NMO_REF_RESOLVED) {
        ok = ok && nmo_cli_record_uint(rec, "character_id", "Character", st->character.id);
    }
    if (st->has_current_step) {
        ok = ok && nmo_cli_record_real(rec, "current_step", "Current Step",
                                       (double)st->current_step, "%.2f");
    }
    return ok;
}

int nmo_cmd_animation_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--id",   "-i", NMO_OPT_UINT,   "Animation object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Animation object name"},
    };
    enum { OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = (!has_selector_opt && r.pos_count >= 2) ? r.pos_args[0] : NULL;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_core_object_selector_t selector = {
        .has_id = vals[OPT_ID].present,
        .id = nmo_opt_uint_or(&vals[OPT_ID], 0),
        .positional_id = positional_id,
        .name = nmo_opt_str(&vals[OPT_NAME]),
        .allowed_class_ids = animation_class_ids,
        .allowed_class_count = sizeof(animation_class_ids) /
                               sizeof(animation_class_ids[0]),
        .selector_label = "Animation",
        .type_label = "animation class",
    };
    nmo_object_t *obj = NULL;
    nmo_object_id_t obj_id = 0;
    rc = nmo_core_resolve_one_object(&c, &selector, &obj, &obj_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo animation show [--id <id> | --name <name> | <id>] <file>\n");
        return nmo_cmd_ctx_done(&c, rc);
    }

    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    if (!is_animation_class(&c, cid)) {
        fprintf(stderr, "Error: Object %u is not an animation class\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const char *name = nmo_object_get_name(obj);
    const char *cls = nmo_core_class_name(&c, cid);

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec || !nmo_cli_record_title(rec, "Animation") ||
        !animation_show_build_record(&c, rec, obj, obj_id, cid, cls, name)) {
        nmo_cli_record_free(rec);
        fprintf(stderr, "Error: Out of memory while describing animation %u\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    rc = nmo_cmd_ctx_emit_record(&c, rec, "animation.show", 18, c.colorize);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * animation keys
 * ============================================================================ */

/* The text view shows at most this many keys per controller; JSON lists all. */
enum { ANIMATION_KEYS_TEXT_LIMIT = 20 };

/* One text line per key, prefixed with the key time. */
static bool animation_keys_add_text_line(nmo_cli_record_t *item,
                                         const float *values,
                                         uint32_t value_count,
                                         const nmo_objanim_bezier_key_t *bezier)
{
    if (!nmo_cli_record_raw_fmt(item, "    t=%.4f", (double)values[0])) {
        return false;
    }
    for (uint32_t v = 1; v < value_count; ++v) {
        if (!nmo_cli_record_raw_fmt(item, " %.6g", (double)values[v])) {
            return false;
        }
    }
    if (bezier != NULL) {
        if (!nmo_cli_record_raw_fmt(item, " flags=0x%08x", bezier->flags)) {
            return false;
        }
        for (size_t t = 0; t < 2u; ++t) {
            if (bezier->has_tangent[t] &&
                !nmo_cli_record_raw_fmt(
                    item, " tangent%zu=(%.6g %.6g %.6g)", t,
                    (double)bezier->tangent[t][0],
                    (double)bezier->tangent[t][1],
                    (double)bezier->tangent[t][2])) {
                return false;
            }
        }
    }
    return nmo_cli_record_raw(item, "\n");
}

/*
 * Bytes per key of a controller whose keys are floats (time first). Taken from
 * the data itself so that it holds for every file layout, including the
 * 24-byte scale-axis keys of NEWDATA and LEGACY files, and never reads past
 * data. 0 when the keys cannot be decoded that way.
 */
static uint32_t animation_float_key_stride(const nmo_objanim_controller_t *ctrl)
{
    if (!ctrl->data || ctrl->key_count == 0 ||
        nmo_objanim_controller_is_bezier(ctrl->type) ||
        ctrl->data_size % ctrl->key_count != 0) {
        return 0;
    }
    uint32_t stride = ctrl->data_size / ctrl->key_count;
    return (stride >= sizeof(float) && stride % sizeof(float) == 0) ? stride : 0;
}

/*
 * Text lines for the keys of one controller: "t=<time> <values>" per key.
 * Controller types with an unknown layout have no decodable keys.
 */
static bool animation_keys_add_text(nmo_cli_record_t *item,
                                    const nmo_objanim_controller_t *ctrl)
{
    nmo_objanim_morph_info_t morph;
    if (nmo_objanim_morph_controller_info(ctrl, &morph)) {
        if (!nmo_cli_record_raw_fmt(item, "    %u vertices per key, %s\n",
                                    morph.vertex_count,
                                    morph.has_normals ? "with normals" : "no normals")) {
            return false;
        }
        uint32_t morph_shown = morph.key_count > ANIMATION_KEYS_TEXT_LIMIT
            ? ANIMATION_KEYS_TEXT_LIMIT : morph.key_count;
        for (uint32_t k = 0; k < morph_shown; ++k) {
            float time = 0.0f;
            if (!nmo_objanim_morph_controller_key(ctrl, &morph, k, &time, NULL, NULL) ||
                !nmo_cli_record_raw_fmt(item, "    t=%.4f\n", (double)time)) {
                return false;
            }
        }
        if (morph.key_count > ANIMATION_KEYS_TEXT_LIMIT) {
            return nmo_cli_record_raw_fmt(item, "    ... (%u more keys)\n",
                                          morph.key_count - ANIMATION_KEYS_TEXT_LIMIT);
        }
        return true;
    }
    if (!ctrl->data || ctrl->key_count == 0) {
        return true;
    }

    uint32_t shown = ctrl->key_count > ANIMATION_KEYS_TEXT_LIMIT
        ? ANIMATION_KEYS_TEXT_LIMIT
        : ctrl->key_count;

    if (nmo_objanim_controller_is_bezier(ctrl->type)) {
        const uint8_t *cursor = (const uint8_t *)ctrl->data;
        size_t left = ctrl->data_size;
        for (uint32_t k = 0; k < shown; ++k) {
            nmo_objanim_bezier_key_t key;
            size_t size = nmo_objanim_bezier_key_decode(cursor, left, &key);
            if (size == 0) {
                return true;
            }
            const float values[4] = {key.time, key.position[0], key.position[1],
                                     key.position[2]};
            if (!animation_keys_add_text_line(item, values, 4, &key)) {
                return false;
            }
            cursor += size;
            left -= size;
        }
    } else if (animation_float_key_stride(ctrl) > 0) {
        const float *floats = (const float *)ctrl->data;
        uint32_t floats_per_key =
            animation_float_key_stride(ctrl) / (uint32_t)sizeof(float);
        for (uint32_t k = 0; k < shown; ++k) {
            if (!animation_keys_add_text_line(
                    item, floats + (size_t)k * floats_per_key, floats_per_key,
                    NULL)) {
                return false;
            }
        }
    } else {
        return true;
    }

    if (ctrl->key_count > ANIMATION_KEYS_TEXT_LIMIT) {
        return nmo_cli_record_raw_fmt(item, "    ... (%u more keys)\n",
                                      ctrl->key_count - ANIMATION_KEYS_TEXT_LIMIT);
    }
    return true;
}

static void add_real_array(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                           const char *key, const float *values, size_t count)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t v = 0; v < count; ++v)
        yyjson_mut_arr_add_real(doc, arr, (double)values[v]);
    yyjson_mut_obj_add_val(doc, obj, key, arr);
}

/** Add decoded keys to JSON array */
static void add_keys_json(yyjson_mut_doc *doc, yyjson_mut_val *keys_arr,
                          const nmo_objanim_controller_t *ctrl) {
    nmo_objanim_morph_info_t morph;
    if (nmo_objanim_morph_controller_info(ctrl, &morph)) {
        /* Positions and normals are per vertex; list the times and the counts. */
        for (uint32_t k = 0; k < morph.key_count; ++k) {
            float time = 0.0f;
            if (!nmo_objanim_morph_controller_key(ctrl, &morph, k, &time, NULL, NULL)) break;
            yyjson_mut_val *kobj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_real(doc, kobj, "time", (double)time);
            yyjson_mut_obj_add_uint(doc, kobj, "vertex_count", morph.vertex_count);
            yyjson_mut_obj_add_bool(doc, kobj, "has_normals", morph.has_normals);
            yyjson_mut_arr_add_val(keys_arr, kobj);
        }
        return;
    }
    if (!ctrl->data || ctrl->key_count == 0) return;

    if (nmo_objanim_controller_is_bezier(ctrl->type)) {
        /* Packed keys: time, position, flags and up to two tangents */
        const uint8_t *cursor = (const uint8_t *)ctrl->data;
        size_t left = ctrl->data_size;
        for (uint32_t k = 0; k < ctrl->key_count; ++k) {
            nmo_objanim_bezier_key_t key;
            size_t size = nmo_objanim_bezier_key_decode(cursor, left, &key);
            if (size == 0) break;
            yyjson_mut_val *kobj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_real(doc, kobj, "time", (double)key.time);
            add_real_array(doc, kobj, "values", key.position, 3);
            yyjson_mut_obj_add_uint(doc, kobj, "flags", key.flags);
            if (key.has_tangent[0])
                add_real_array(doc, kobj, "tangent0", key.tangent[0], 3);
            if (key.has_tangent[1])
                add_real_array(doc, kobj, "tangent1", key.tangent[1], 3);
            yyjson_mut_arr_add_val(keys_arr, kobj);
            cursor += size;
            left -= size;
        }
    } else if (animation_float_key_stride(ctrl) > 0) {
        /* Float layout: time followed by the value floats */
        uint32_t floats_per_key =
            animation_float_key_stride(ctrl) / (uint32_t)sizeof(float);
        const float *fp = (const float *)ctrl->data;
        for (uint32_t k = 0; k < ctrl->key_count; ++k) {
            const float *key = fp + (size_t)k * floats_per_key;
            yyjson_mut_val *kobj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_real(doc, kobj, "time", (double)key[0]);
            add_real_array(doc, kobj, "values", key + 1, floats_per_key - 1u);
            yyjson_mut_arr_add_val(keys_arr, kobj);
        }
    }
}

/* JSON splice: the "keys" array of one controller. */
static bool animation_keys_json(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                const void *data)
{
    const nmo_objanim_controller_t *ctrl = (const nmo_objanim_controller_t *)data;
    yyjson_mut_val *keys_arr = yyjson_mut_arr(doc);
    if (!keys_arr) {
        return false;
    }
    add_keys_json(doc, keys_arr, ctrl);
    return yyjson_mut_obj_add_val(doc, obj, "keys", keys_arr);
}

static nmo_cli_record_t *animation_keys_record_new(
    nmo_object_id_t obj_id,
    const char *name,
    const nmo_objectanimation_state_t *st)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "id", NULL, obj_id) &&
              nmo_cli_record_uint(rec, "controller_count", NULL,
                                  st->controller_count) &&
              nmo_cli_record_raw_fmt(rec, "Animation keys for #%u", obj_id) &&
              (name && name[0] ? nmo_cli_record_raw_fmt(rec, " (%s)", name) : true) &&
              nmo_cli_record_raw_fmt(rec, " - %u controllers\n\n",
                                     st->controller_count);

    nmo_cli_record_array_t *ctrls =
        ok ? nmo_cli_record_array(rec, "controllers", NULL) : NULL;
    ok = ok && ctrls != NULL;
    if (ok) {
        nmo_cli_record_array_omit_heading(ctrls);
        nmo_cli_record_array_inline_items(ctrls);
    }
    for (uint32_t ci = 0; ok && ci < st->controller_count; ++ci) {
        const nmo_objanim_controller_t *ctrl = &st->controllers[ci];
        uint32_t key_size =
            nmo_objanim_controller_format_key_size(ctrl->type, st->format);
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL &&
             nmo_cli_record_str_fmt(item, "type", NULL, "0x%08x", ctrl->type) &&
             nmo_cli_record_str(item, "type_name", NULL,
                                controller_type_name(ctrl->type)) &&
             nmo_cli_record_uint(item, "key_count", NULL, controller_key_count(ctrl)) &&
             nmo_cli_record_uint(item, "key_size", NULL, key_size) &&
             nmo_cli_record_uint(item, "data_size", NULL, ctrl->data_size) &&
             nmo_cli_record_json(item, animation_keys_json, ctrl) &&
             nmo_cli_record_raw_fmt(item, "Controller [%u]: type=0x%08x (%s), keys=%u, "
                                    "key_size=%u, data=%u bytes\n",
                                    ci, ctrl->type, controller_type_name(ctrl->type),
                                    controller_key_count(ctrl), key_size, ctrl->data_size) &&
             animation_keys_add_text(item, ctrl) &&
             nmo_cli_record_raw(item, "\n");
        if (!ok) {
            nmo_cli_record_free(item);
        } else {
            ok = nmo_cli_record_array_add(ctrls, item);
        }
    }

    if (ok && st->morph_key_parsed_count > 0) {
        ok = nmo_cli_record_raw_fmt(rec, "Morph keys: %u\n",
                                    st->morph_key_parsed_count);
        nmo_cli_record_array_t *morphs =
            ok ? nmo_cli_record_array(rec, "morph_keys", NULL) : NULL;
        ok = ok && morphs != NULL;
        if (ok) {
            nmo_cli_record_array_omit_heading(morphs);
            nmo_cli_record_array_inline_items(morphs);
        }
        for (uint32_t mi = 0; ok && mi < st->morph_key_parsed_count; ++mi) {
            const nmo_objanim_morph_key_t *mk = &st->morph_keys[mi];
            nmo_cli_record_t *item = nmo_cli_record_new();
            ok = item != NULL &&
                 nmo_cli_record_real(item, "time_step", NULL,
                                     (double)mk->time_step, NULL) &&
                 nmo_cli_record_uint(item, "data_size", NULL, mk->data_size) &&
                 (mi < 20
                      ? nmo_cli_record_raw_fmt(item, "  [%u] time=%.4f, data_size=%u\n",
                                               mi, (double)mk->time_step,
                                               mk->data_size)
                      : true);
            if (!ok) {
                nmo_cli_record_free(item);
            } else {
                ok = nmo_cli_record_array_add(morphs, item);
            }
        }
        if (ok && st->morph_key_parsed_count > 20) {
            ok = nmo_cli_record_raw_fmt(rec, "  ... (%u more)\n",
                                        st->morph_key_parsed_count - 20);
        }
    }

    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

int nmo_cmd_animation_keys(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--id",   "-i", NMO_OPT_UINT,   "Object animation ID"},
        {"--name", "-n", NMO_OPT_STRING, "Object animation name"},
    };
    enum { OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = (!has_selector_opt && r.pos_count >= 2) ? r.pos_args[0] : NULL;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_core_object_selector_t selector = {
        .has_id = vals[OPT_ID].present,
        .id = nmo_opt_uint_or(&vals[OPT_ID], 0),
        .positional_id = positional_id,
        .name = nmo_opt_str(&vals[OPT_NAME]),
        .allowed_class_ids = object_animation_class_ids,
        .allowed_class_count = sizeof(object_animation_class_ids) /
                               sizeof(object_animation_class_ids[0]),
        .selector_label = "Animation",
        .type_label = "CKObjectAnimation",
    };
    nmo_object_t *obj = NULL;
    nmo_object_id_t obj_id = 0;
    rc = nmo_core_resolve_one_object(&c, &selector, &obj, &obj_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo animation keys [--id <id> | --name <name> | <id>] <file>\n");
        return nmo_cmd_ctx_done(&c, rc);
    }

    nmo_objectanimation_state_t *st =
        (nmo_objectanimation_state_t *)nmo_object_get_state(obj);
    if (!st) {
        fprintf(stderr, "Error: No data for object %u\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_cli_record_t *rec =
        animation_keys_record_new(obj_id, nmo_object_get_name(obj), st);
    if (!rec) {
        fprintf(stderr, "Error: Out of memory while describing animation %u\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    rc = nmo_cmd_ctx_emit_record(&c, rec, "animation.keys", 0, c.colorize);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * animation export
 * ============================================================================ */

static int export_one_animation(nmo_objectanimation_state_t *st,
                                nmo_object_t *obj,
                                const char *out_dir) {
    const char *name = nmo_object_get_name(obj);
    uint32_t obj_id = nmo_object_get_id(obj);

    /* Build filename */
    char *safe_name = (name && name[0])
        ? nmo_tool_sanitize_filename_dup(name, obj_id)
        : nmo_tool_strdup_fmt("anim_%u", obj_id);
    char *path = safe_name
        ? nmo_tool_strdup_fmt("%s/%s_%u.anim.json", out_dir, safe_name, obj_id)
        : NULL;
    free(safe_name);
    if (!path) return -1;

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        free(path);
        return -1;
    }

    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_uint(doc, root, "id", obj_id);
    yyjson_mut_obj_add_str(doc, root, "class", "CKObjectAnimation");
    yyjson_mut_obj_add_str(doc, root, "format", animation_format_name(st->format));
    const nmo_object_id_t entity_id = nmo_ref_runtime_id(&st->entity);
    if (entity_id != NMO_OBJECT_ID_NONE)
        yyjson_mut_obj_add_uint(doc, root, "entity_id", entity_id);
    yyjson_mut_obj_add_real(doc, root, "length",
        st->has_length ? (double)st->length : 0.0);

    /* Get frame_rate from base if available */
    yyjson_mut_obj_add_uint(doc, root, "flags", st->flags);

    /* Controllers */
    yyjson_mut_val *ctrl_arr = yyjson_mut_arr(doc);
    for (uint32_t ci = 0; ci < st->controller_count; ++ci) {
        const nmo_objanim_controller_t *ctrl = &st->controllers[ci];
        yyjson_mut_val *cobj = yyjson_mut_obj(doc);

        nmo_cli_json_add_str_fmt_safe(doc, cobj, "type", "0x%08x", ctrl->type);
        yyjson_mut_obj_add_str(doc, cobj, "type_name",
                               controller_type_name(ctrl->type));
        yyjson_mut_obj_add_uint(doc, cobj, "key_count", ctrl->key_count);

        uint32_t key_size =
            nmo_objanim_controller_format_key_size(ctrl->type, st->format);
        yyjson_mut_obj_add_uint(doc, cobj, "key_size", key_size);

        yyjson_mut_val *keys_arr = yyjson_mut_arr(doc);
        add_keys_json(doc, keys_arr, ctrl);
        yyjson_mut_obj_add_val(doc, cobj, "keys", keys_arr);

        yyjson_mut_arr_add_val(ctrl_arr, cobj);
    }
    yyjson_mut_obj_add_val(doc, root, "controllers", ctrl_arr);

    /* Morph keys */
    if (st->morph_key_parsed_count > 0) {
        yyjson_mut_val *morph_arr = yyjson_mut_arr(doc);
        for (uint32_t mi = 0; mi < st->morph_key_parsed_count; ++mi) {
            const nmo_objanim_morph_key_t *mk = &st->morph_keys[mi];
            yyjson_mut_val *mobj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_real(doc, mobj, "time_step", (double)mk->time_step);
            yyjson_mut_obj_add_uint(doc, mobj, "data_size", mk->data_size);
            yyjson_mut_arr_add_val(morph_arr, mobj);
        }
        yyjson_mut_obj_add_val(doc, root, "morph_keys", morph_arr);
    }

    /* Write to file */
    yyjson_write_flag flg = YYJSON_WRITE_PRETTY | YYJSON_WRITE_ESCAPE_UNICODE;
    yyjson_write_err err;
    bool ok = yyjson_mut_write_file(path, doc, flg, NULL, &err);
    yyjson_mut_doc_free(doc);

    if (!ok) {
        fprintf(stderr, "Error: Failed to write %s: %s\n", path,
                err.msg ? err.msg : "unknown error");
        free(path);
        return -1;
    }

    free(path);
    return 0;
}

typedef struct animation_export_data {
    const char *out_dir;
    uint32_t exported;
} animation_export_data_t;

typedef struct animation_export_args {
    bool has_id;
    uint32_t id;
    const char *name;
    const char *out_dir;
    const char *positional_id;
    bool export_all;
} animation_export_args_t;

static int animation_export_parse(int argc, char **argv,
                                  bool expect_file_operand,
                                  animation_export_args_t *args,
                                  const char *usage) {
    memset(args, 0, sizeof(*args));

    static const nmo_opt_def_t opts[] = {
        {"--id",      "-i", NMO_OPT_UINT,   "Object animation ID"},
        {"--name",    "-n", NMO_OPT_STRING, "Object animation name"},
        {"--out-dir", "-d", NMO_OPT_STRING, "Output directory (required)"},
        {"--all",     NULL, NMO_OPT_FLAG,   "Export all CKObjectAnimation objects"},
    };
    enum { OPT_ID, OPT_NAME, OPT_OUT_DIR, OPT_ALL, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    args->has_id = vals[OPT_ID].present;
    args->id = nmo_opt_uint_or(&vals[OPT_ID], 0);
    args->name = nmo_opt_str(&vals[OPT_NAME]);
    args->out_dir = nmo_opt_str(&vals[OPT_OUT_DIR]);
    args->export_all = nmo_opt_flag(&vals[OPT_ALL]);

    bool has_selector_opt = args->has_id || args->name != NULL;
    if (!has_selector_opt && !args->export_all) {
        if (expect_file_operand) {
            args->positional_id = r.pos_count >= 2 ? r.pos_args[0] : NULL;
        } else {
            args->positional_id = r.pos_count == 1 ? r.pos_args[0] : NULL;
        }
    } else if (!expect_file_operand && r.pos_count != 0) {
        fprintf(stderr, "Error: Unexpected argument '%s'\n", r.pos_args[0]);
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (!args->out_dir || !*args->out_dir) {
        fprintf(stderr, "Error: --out-dir is required\n");
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!args->has_id && !args->name && !args->positional_id && !args->export_all) {
        fprintf(stderr, "Error: Specify --id <id>, --name <name>, <id>, or --all\n");
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static int animation_export_visitor(size_t index,
                                    nmo_object_t *obj,
                                    const nmo_cmd_ctx_t *c,
                                    void *user)
{
    (void)index;
    (void)c;

    animation_export_data_t *data = (animation_export_data_t *)user;
    nmo_objectanimation_state_t *st =
        (nmo_objectanimation_state_t *)nmo_object_get_state(obj);
    if (!st) return 0;

    if (export_one_animation(st, obj, data->out_dir) == 0) {
        data->exported++;
    }
    return 0;
}

static int anim_ensure_dir(const char *dir_path) {
    if (!dir_path || !*dir_path) return -1;
#ifdef _WIN32
    if (_mkdir(dir_path) == 0) return 0;
#else
    if (mkdir(dir_path, 0755) == 0) return 0;
#endif
    if (errno == EEXIST) return 0;
    return -1;
}

static int animation_export_run(nmo_cmd_ctx_t *ctx,
                                const animation_export_args_t *args,
                                bool close_ctx,
                                const char *usage) {
    nmo_cmd_ctx_t c = *ctx;

    if (anim_ensure_dir(args->out_dir) < 0) {
        fprintf(stderr, "Error: Cannot create directory '%s' (%s)\n",
                args->out_dir, strerror(errno));
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR)
                         : NMO_CLI_EXIT_IO_ERROR;
    }

    uint32_t exported = 0;
    if (args->export_all) {
        nmo_object_query_t query = {0};
        nmo_core_query_set_class_id(&query, NMO_CID_OBJECTANIMATION, false);

        animation_export_data_t export_data = { .out_dir = args->out_dir };
        int rc = nmo_core_object_query_run(&c, &query,
                                       animation_export_visitor, &export_data, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
        }
        exported = export_data.exported;
    } else {
        nmo_core_object_selector_t selector = {
            .has_id = args->has_id,
            .id = args->id,
            .positional_id = args->positional_id,
            .name = args->name,
            .allowed_class_ids = object_animation_class_ids,
            .allowed_class_count = sizeof(object_animation_class_ids) /
                                   sizeof(object_animation_class_ids[0]),
            .selector_label = "Animation",
            .type_label = "CKObjectAnimation",
        };
        nmo_object_t *obj = NULL;
        nmo_object_id_t obj_id = 0;
        int rc = nmo_core_resolve_one_object(&c, &selector, &obj, &obj_id);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            fprintf(stderr, "Usage: %s\n", usage);
            return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
        }

        nmo_objectanimation_state_t *st =
            (nmo_objectanimation_state_t *)nmo_object_get_state(obj);
        if (!st) {
            fprintf(stderr, "Error: No data for object %u\n", obj_id);
            return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        if (export_one_animation(st, obj, args->out_dir) != 0)
            return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        exported = 1;
    }

    /* Only --all reports in text; JSON always gets the summary. */
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_str(rec, "out_dir", NULL, args->out_dir) &&
              nmo_cli_record_uint(rec, "exported", NULL, exported) &&
              (!args->export_all ||
               nmo_cli_record_raw_fmt(rec, "Exported %u animations to %s\n",
                                      exported, args->out_dir));
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    int rc = nmo_cmd_ctx_emit_record(&c, rec, "animation.export", 0, c.colorize);
    return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
}

int nmo_cmd_animation_export(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    animation_export_args_t args;
    const char *usage = "nmo animation export [--all | --id <id> | --name <name> | <id>] --out-dir <dir> <file>";
    int rc = animation_export_parse(argc, argv, true, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    return animation_export_run(&c, &args, true, usage);
}

static int nmo_cmd_animation_export_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv) {
    animation_export_args_t args;
    const char *usage = "animation export [--all | --id <id> | --name <name> | <id>] --out-dir <dir>";
    int rc = animation_export_parse(argc, argv, false, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    return animation_export_run(ctx, &args, false, usage);
}

/* ============================================================================
 * animation import
 * ============================================================================ */

/** Parse a hex string like "0x637c4301" to uint32_t */
static bool parse_hex_u32(const char *str, uint32_t *out) {
    return nmo_parse_u32_range_base(str, 16, 0, UINT32_MAX, out) == NMO_OK;
}

/** Decode hex string to bytes. Returns number of bytes decoded. */
static size_t hex_decode(const char *hex, uint8_t *out, size_t out_size) {
    size_t count = 0;
    if (nmo_parse_hex_bytes(hex, out, out_size, &count) != NMO_OK) {
        return count;
    }
    return count;
}

static double animation_json_get_number(yyjson_val *val) {
    if (!val || !yyjson_is_num(val)) {
        return 0.0;
    }
    if (yyjson_is_real(val)) {
        return yyjson_get_real(val);
    }
    if (yyjson_is_sint(val)) {
        return (double)yyjson_get_sint(val);
    }
    return (double)yyjson_get_uint(val);
}

int nmo_cmd_animation_import(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_WRITE_OUTPUT,
        {"--replace", NULL, NMO_OPT_STRING, "Replace existing animation by ID"},
        {"--replace-name", NULL, NMO_OPT_STRING, "Replace existing animation by exact name"},
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_REPLACE, OPT_REPLACE_NAME, OPT_DRYRUN, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    const char *replace_str = nmo_opt_str(&vals[OPT_REPLACE]);
    const char *replace_name = nmo_opt_str(&vals[OPT_REPLACE_NAME]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);

    if (!dry_run && !output_path) {
        fprintf(stderr, "Error: --output/-o is required (or use --dry-run)\n");
        fprintf(stderr, "Usage: nmo animation import <json-file> <nmo-file> -o <output> [--dry-run]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 2) {
        fprintf(stderr, "Usage: nmo animation import <json-file> <nmo-file> -o <output> [--dry-run]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *json_path = r.pos_args[0];
    const char *nmo_path = r.pos_args[1];

    /* Parse replace ID if given */
    bool do_replace = false;
    if (replace_str || replace_name) {
        do_replace = true;
    }

    /* Read JSON file */
    yyjson_read_err read_err;
    yyjson_doc *jdoc = yyjson_read_file(json_path, 0, NULL, &read_err);
    if (!jdoc) {
        fprintf(stderr, "Error: Failed to read JSON '%s': %s\n",
                json_path, read_err.msg ? read_err.msg : "unknown error");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    yyjson_val *jroot = yyjson_doc_get_root(jdoc);
    if (!yyjson_is_obj(jroot)) {
        fprintf(stderr, "Error: JSON root must be an object\n");
        yyjson_doc_free(jdoc);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Extract metadata */
    const char *format_str = yyjson_get_str(yyjson_obj_get(jroot, "format"));
    uint32_t entity_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(jroot, "entity_id"));
    double length_val = animation_json_get_number(yyjson_obj_get(jroot, "length"));
    uint32_t flags_val = (uint32_t)yyjson_get_uint(yyjson_obj_get(jroot, "flags"));

    nmo_objectanimation_format_t format = CKOBJANIM_FORMAT_NEWDATA;
    if (format_str) {
        if (strcmp(format_str, "LEGACY") == 0) format = CKOBJANIM_FORMAT_LEGACY;
        else if (strcmp(format_str, "CONTROLLERS") == 0) format = CKOBJANIM_FORMAT_CONTROLLERS;
        else if (strcmp(format_str, "SHARED") == 0) format = CKOBJANIM_FORMAT_SHARED;
        else if (strcmp(format_str, "NONE") == 0) format = CKOBJANIM_FORMAT_NONE;
    }

    /* Parse controllers array */
    yyjson_val *jctrl_arr = yyjson_obj_get(jroot, "controllers");
    uint32_t ctrl_count = 0;
    if (yyjson_is_arr(jctrl_arr))
        ctrl_count = (uint32_t)yyjson_arr_size(jctrl_arr);

    /* Open NMO session */
    nmo_cmd_ctx_t c;
    int rc = nmo_cli_write_init_ctx(&c, nmo_path, global);
    if (rc) {
        yyjson_doc_free(jdoc);
        return rc;
    }

    nmo_arena_t *arena = nmo_tool_owner_arena(c.workspace);

    /* Build controller array */
    nmo_objanim_controller_t *controllers = NULL;
    if (ctrl_count > 0) {
        controllers = (nmo_objanim_controller_t *)nmo_arena_alloc(
            arena, sizeof(nmo_objanim_controller_t) * ctrl_count,
            _Alignof(nmo_objanim_controller_t));
        if (!controllers) {
            fprintf(stderr, "Error: Out of memory\n");
            yyjson_doc_free(jdoc);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        memset(controllers, 0, sizeof(nmo_objanim_controller_t) * ctrl_count);
    }

    bool parse_ok = true;
    for (uint32_t ci = 0; ci < ctrl_count && parse_ok; ++ci) {
        yyjson_val *jctrl = yyjson_arr_get(jctrl_arr, ci);
        if (!yyjson_is_obj(jctrl)) { parse_ok = false; break; }

        const char *type_str = yyjson_get_str(yyjson_obj_get(jctrl, "type"));
        uint32_t type = 0;
        if (!parse_hex_u32(type_str, &type)) {
            fprintf(stderr, "Error: Invalid controller type '%s' in controller %u\n",
                    type_str ? type_str : "(null)", ci);
            parse_ok = false; break;
        }

        uint32_t key_count = (uint32_t)yyjson_get_uint(yyjson_obj_get(jctrl, "key_count"));
        uint32_t key_size = nmo_objanim_controller_format_key_size(type, format);

        /* Validate against JSON key_size if present */
        yyjson_val *jks = yyjson_obj_get(jctrl, "key_size");
        if (jks && yyjson_is_uint(jks)) {
            uint32_t json_ks = (uint32_t)yyjson_get_uint(jks);
            if (key_size != 0 && json_ks != key_size) {
                fprintf(stderr, "Error: key_size mismatch for controller %u: "
                        "expected %u, JSON says %u\n", ci, key_size, json_ks);
                parse_ok = false; break;
            }
            if (key_size == 0) key_size = json_ks;
        }

        if (nmo_objanim_controller_is_bezier(type)) {
            fprintf(stderr, "Error: Bezier controller %u has variable-size keys "
                    "and cannot be imported\n", ci);
            parse_ok = false; break;
        }

        if (key_size == 0) {
            fprintf(stderr, "Error: Cannot determine key_size for controller %u\n", ci);
            parse_ok = false; break;
        }

        uint32_t data_size = key_count * key_size;
        void *data = NULL;
        if (data_size > 0) {
            data = nmo_arena_alloc(arena, data_size, _Alignof(float));
            if (!data) {
                fprintf(stderr, "Error: Out of memory allocating key data\n");
                parse_ok = false; break;
            }
            memset(data, 0, data_size);
        }

        /* Parse keys */
        yyjson_val *jkeys = yyjson_obj_get(jctrl, "keys");
        if (yyjson_is_arr(jkeys)) {
            size_t nkeys = yyjson_arr_size(jkeys);
            if (nkeys != key_count) {
                fprintf(stderr, "Warning: controller %u key_count=%u but %u keys in JSON\n",
                        ci, key_count, (uint32_t)nkeys);
                if (nkeys < key_count) key_count = (uint32_t)nkeys;
            }

            uint32_t floats_per_key = key_size / (uint32_t)sizeof(float);
            bool is_float_key = (key_size % sizeof(float) == 0);

            for (uint32_t ki = 0; ki < key_count; ++ki) {
                yyjson_val *jkey = yyjson_arr_get(jkeys, ki);

                /* Check for hex-encoded key */
                yyjson_val *jhex = yyjson_obj_get(jkey, "hex");
                if (jhex && yyjson_is_str(jhex)) {
                    const char *hex = yyjson_get_str(jhex);
                    uint8_t *dst = (uint8_t *)data + ki * key_size;
                    hex_decode(hex, dst, key_size);
                    continue;
                }

                if (!is_float_key) {
                    fprintf(stderr, "Error: Non-float key without hex data "
                            "in controller %u key %u\n", ci, ki);
                    parse_ok = false; break;
                }

                /* Float-based key: time + values array */
                float *fp = (float *)data + ki * floats_per_key;
                yyjson_val *jtime = yyjson_obj_get(jkey, "time");
                if (jtime)
                    fp[0] = (float)animation_json_get_number(jtime);

                yyjson_val *jvals = yyjson_obj_get(jkey, "values");
                if (yyjson_is_arr(jvals)) {
                    size_t nvals = yyjson_arr_size(jvals);
                    for (size_t vi = 0; vi < nvals && vi + 1 < floats_per_key; ++vi) {
                        yyjson_val *jv = yyjson_arr_get(jvals, vi);
                        fp[vi + 1] = (float)animation_json_get_number(jv);
                    }
                }
            }
        }

        controllers[ci].type = type;
        controllers[ci].key_count = key_count;
        controllers[ci].data_size = data_size;
        controllers[ci].data = data;
    }

    uint8_t has_morph_counts = 0;
    int32_t morph_vertex_count = 0;
    int32_t morph_key_count = 0;
    uint32_t morph_key_parsed_count = 0;
    nmo_objanim_morph_key_t *morph_keys = NULL;

    yyjson_val *jmorph_vertex_count = yyjson_obj_get(jroot, "morph_vertex_count");
    yyjson_val *jmorph_key_count = yyjson_obj_get(jroot, "morph_key_count");
    yyjson_val *jmorph_arr = yyjson_obj_get(jroot, "morph_keys");

    if (jmorph_vertex_count && yyjson_is_num(jmorph_vertex_count)) {
        morph_vertex_count = (int32_t)yyjson_get_sint(jmorph_vertex_count);
        has_morph_counts = 1;
    }
    if (jmorph_key_count && yyjson_is_num(jmorph_key_count)) {
        morph_key_count = (int32_t)yyjson_get_sint(jmorph_key_count);
        has_morph_counts = 1;
    }

    if (yyjson_is_arr(jmorph_arr)) {
        size_t morph_count = yyjson_arr_size(jmorph_arr);
        if (morph_count > UINT32_MAX) {
            fprintf(stderr, "Error: Too many morph keys\n");
            parse_ok = false;
        } else if (morph_count > 0) {
            morph_keys = (nmo_objanim_morph_key_t *)nmo_arena_alloc(
                arena, sizeof(nmo_objanim_morph_key_t) * morph_count,
                _Alignof(nmo_objanim_morph_key_t));
            if (!morph_keys) {
                fprintf(stderr, "Error: Out of memory allocating morph keys\n");
                parse_ok = false;
            } else {
                memset(morph_keys, 0, sizeof(nmo_objanim_morph_key_t) * morph_count);
                morph_key_parsed_count = (uint32_t)morph_count;
                has_morph_counts = 1;
                if (morph_key_count == 0) {
                    morph_key_count = (int32_t)morph_count;
                }

                for (uint32_t mi = 0; mi < morph_key_parsed_count && parse_ok; ++mi) {
                    yyjson_val *jmorph = yyjson_arr_get(jmorph_arr, mi);
                    if (!yyjson_is_obj(jmorph)) {
                        fprintf(stderr, "Error: morph key %u must be an object\n", mi);
                        parse_ok = false;
                        break;
                    }

                    yyjson_val *jtime = yyjson_obj_get(jmorph, "time_step");
                    if (jtime) {
                        morph_keys[mi].time_step = (float)animation_json_get_number(jtime);
                    }
                    uint32_t data_size =
                        (uint32_t)yyjson_get_uint(yyjson_obj_get(jmorph, "data_size"));
                    const char *hex = yyjson_get_str(yyjson_obj_get(jmorph, "hex"));
                    if (hex) {
                        size_t hex_len = strlen(hex);
                        if ((hex_len % 2u) != 0u || hex_len / 2u > UINT32_MAX) {
                            fprintf(stderr, "Error: Invalid morph key hex data in key %u\n", mi);
                            parse_ok = false;
                            break;
                        }
                        if (data_size == 0) {
                            data_size = (uint32_t)(hex_len / 2u);
                        } else if (hex_len / 2u != data_size) {
                            fprintf(stderr, "Error: morph key %u data_size=%u but hex has %zu bytes\n",
                                    mi, data_size, hex_len / 2u);
                            parse_ok = false;
                            break;
                        }
                    }

                    morph_keys[mi].data_size = data_size;
                    if (data_size > 0) {
                        uint8_t *data = (uint8_t *)nmo_arena_alloc(arena, data_size, 4);
                        if (!data) {
                            fprintf(stderr, "Error: Out of memory allocating morph key data\n");
                            parse_ok = false;
                            break;
                        }
                        memset(data, 0, data_size);
                        if (hex) {
                            size_t decoded = hex_decode(hex, data, data_size);
                            if (decoded != data_size) {
                                fprintf(stderr, "Error: Failed to decode morph key %u hex data\n", mi);
                                parse_ok = false;
                                break;
                            }
                        }
                        morph_keys[mi].data = data;
                    }
                }
            }
        } else {
            has_morph_counts = 1;
        }
    }

    yyjson_doc_free(jdoc);

    if (!parse_ok) {
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    /* Find or create the target object */
    nmo_object_t *target = NULL;
    if (do_replace) {
        nmo_core_object_selector_t selector = {
            .positional_id = replace_str,
            .name = replace_name,
            .allowed_class_ids = object_animation_class_ids,
            .allowed_class_count = sizeof(object_animation_class_ids) /
                                   sizeof(object_animation_class_ids[0]),
            .selector_label = "Animation",
            .type_label = "CKObjectAnimation",
        };
        nmo_object_id_t replace_id = 0;
        int resolve_rc = nmo_core_resolve_one_object(&c, &selector, &target, &replace_id);
        if (resolve_rc != NMO_CLI_EXIT_SUCCESS) {
            fprintf(stderr, "Usage: nmo animation import <json-file> <nmo-file> -o <output> [--replace <id> | --replace-name <name>] [--dry-run]\n");
            return nmo_cmd_ctx_done(&c, resolve_rc);
        }
    }

    nmo_workspace_edit_t *edit = NULL;
    nmo_status_t edit_rc = nmo_workspace_edit_begin(c.workspace, "animation.import", &edit);
    if (edit_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin animation edit: %s\n",
                nmo_error_string(edit_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    if (!do_replace) {
        nmo_object_id_t new_id = 0;
        const nmo_object_create_desc_t desc = {
            .class_id = NMO_CID_OBJECTANIMATION,
            .name = "imported_anim",
            .type_guid = NMO_GUID_NULL,
        };
        edit_rc = nmo_object_edit_create(edit, &desc, &new_id);
        if (edit_rc != NMO_OK) {
            nmo_workspace_edit_rollback(edit);
            fprintf(stderr, "Error: Failed to create animation object: %s\n",
                    nmo_error_string(edit_rc));
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        target = nmo_core_find_by_id(&c, new_id);
        if (!target) {
            nmo_workspace_edit_rollback(edit);
            fprintf(stderr, "Error: Created object %u not found\n", new_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        fprintf(stderr, "Created CKObjectAnimation #%u\n", new_id);
    }

    /* Update state */
    nmo_objectanimation_state_t *st =
        (nmo_objectanimation_state_t *)nmo_object_get_state(target);
    if (!st) {
        nmo_status_t alloc_rc =
            nmo_object_alloc_state(target, sizeof(nmo_objectanimation_state_t));
        if (alloc_rc != NMO_OK) {
            nmo_workspace_edit_rollback(edit);
            fprintf(stderr, "Error: Failed to allocate animation state\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        st = (nmo_objectanimation_state_t *)nmo_object_get_state(target);
        if (!st) {
            nmo_workspace_edit_rollback(edit);
            fprintf(stderr, "Error: Animation state allocation failed\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        memset(st, 0, sizeof(*st));
    }

    edit_rc = nmo_workspace_edit_snapshot_bytes(edit, st, sizeof(*st));
    if (edit_rc != NMO_OK) {
        nmo_workspace_edit_rollback(edit);
        fprintf(stderr, "Error: Failed to snapshot animation state: %s\n",
                nmo_error_string(edit_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    st->format = format;
    st->entity = nmo_ref_from_id(entity_id);
    st->flags = flags_val;
    st->has_length = 1;
    st->length = (float)length_val;
    st->has_morph_counts = has_morph_counts;
    st->morph_vertex_count = morph_vertex_count;
    st->morph_key_count = morph_key_count;
    st->morph_key_parsed_count = morph_key_parsed_count;
    st->morph_keys = morph_keys;
    st->controller_count = ctrl_count;
    st->controllers = controllers;
    nmo_workspace_edit_mark(
        edit, NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    edit_rc = nmo_workspace_edit_commit(edit);
    if (edit_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to commit animation edit: %s\n",
                nmo_error_string(edit_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    if (!dry_run) {
        int save_rc = nmo_cli_save_document(c.document, output_path, NULL);
        if (save_rc != NMO_CLI_EXIT_SUCCESS) {
            return nmo_cmd_ctx_done(&c, save_rc);
        }
    }

    if (!dry_run) {
        fprintf(stderr, "Saved: %s\n", output_path);
    }
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "id", NULL, nmo_object_get_id(target)) &&
              nmo_cli_record_bool(rec, "created", NULL, !do_replace) &&
              nmo_cli_record_uint(rec, "controllers", NULL, ctrl_count) &&
              nmo_cli_record_bool(rec, "dry_run", NULL, dry_run) &&
              (dry_run
                   ? nmo_cli_record_raw_fmt(
                         rec, "[dry-run] Imported animation data; no output written\n")
                   : nmo_cli_record_str(rec, "output", NULL, output_path));
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    rc = nmo_cmd_ctx_emit_record(&c, rec, "animation.import", 0, c.colorize);
    return nmo_cmd_ctx_done(&c, rc);
}

