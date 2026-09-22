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
    uint32_t key_size = nmo_objanim_controller_key_size(type);
    switch (key_size) {
        case 16: return "position/scale";
        case 20: return "rotation";
        case 36: return "tcb-pos/scl";
        case 40: return "tcb-rot";
        case 44: return "bezier";
        default: return "unknown";
    }
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
    yyjson_mut_doc *doc;    /* JSON sink when non-NULL */
    yyjson_mut_val *arr;
    nmo_cli_table_t *table; /* text sink otherwise */
    uint32_t found;
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
    if (!rec || !animation_list_build_record(c, obj, rec)) {
        nmo_cli_record_free(rec);
        return 0;
    }
    if (data->doc) {
        yyjson_mut_val *item = yyjson_mut_obj(data->doc);
        if (item && nmo_cli_record_to_json(rec, data->doc, item)) {
            yyjson_mut_arr_add_val(data->arr, item);
        }
    } else if (data->table) {
        nmo_cli_record_add_table_row(rec, data->table);
    }
    nmo_cli_record_free(rec);
    data->found++;
    return 0;
}

int nmo_cmd_animation_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_query_t query = {
        .predicate = animation_query_predicate,
        .predicate_user_data = &c,
    };

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        animation_list_data_t ld = { .doc = doc, .arr = arr };
        rc = nmo_core_object_query_run(&c, &query,
                                       animation_list_visitor, &ld, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return nmo_cmd_ctx_done(&c, rc);
        }

        yyjson_mut_obj_add_uint(doc, data, "count", ld.found);
        yyjson_mut_obj_add_val(doc, data, "animations", arr);
        nmo_cmd_ctx_json_end(&c, doc, data, "animation.list");
    } else {
        static const nmo_cli_table_col_t columns[] = {
            {"ID",     NMO_CLI_ALIGN_RIGHT, 6,  0},
            {"Class",  NMO_CLI_ALIGN_LEFT,  18, 0},
            {"Name",   NMO_CLI_ALIGN_LEFT,  20, 50},
            {"Length",  NMO_CLI_ALIGN_RIGHT, 8,  0},
            {"FPS",    NMO_CLI_ALIGN_RIGHT, 6,  0},
            {"Target", NMO_CLI_ALIGN_RIGHT, 8,  0},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));
        animation_list_data_t ld = { .table = &table };
        rc = nmo_core_object_query_run(&c, &query,
                                       animation_list_visitor, &ld, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_cli_table_free(&table);
            return nmo_cmd_ctx_done(&c, rc);
        }

        fprintf(c.out, "Animations: %u\n\n", ld.found);
        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
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
                                         ctrl->key_count, ctrl->data_size) &&
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
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = (!has_selector_opt && r.pos_count >= 2) ? r.pos_args[0] : NULL;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_core_object_selector_t selector = {
        .has_id = vals[OPT_ID].present,
        .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
        .positional_id = positional_id,
        .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
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
    if (!rec || !animation_show_build_record(&c, rec, obj, obj_id, cid, cls, name)) {
        nmo_cli_record_free(rec);
        fprintf(stderr, "Error: Out of memory while describing animation %u\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_record_to_json(rec, doc, data);
        nmo_cli_record_free(rec);
        nmo_cmd_ctx_json_end(&c, doc, data, "animation.show");
    } else {
        nmo_cli_print_heading(c.out, "Animation", c.colorize);
        nmo_cli_record_print_kv(rec, c.out, 18, c.colorize);
        nmo_cli_record_free(rec);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * animation keys
 * ============================================================================ */

/** Print decoded float keys to text output */
static void print_keys_text(FILE *out, const nmo_objanim_controller_t *ctrl,
                            uint32_t key_size, uint32_t floats_per_key,
                            bool colorize) {
    (void)colorize;
    if (!ctrl->data || ctrl->key_count == 0) return;

    const float *fp = (const float *)ctrl->data;
    uint32_t show = ctrl->key_count > 20 ? 20 : ctrl->key_count;

    for (uint32_t k = 0; k < show; ++k) {
        const float *key = fp + k * (key_size / sizeof(float));
        char line[256];
        int off = snprintf(line, sizeof(line), "    t=%.4f", (double)key[0]);
        for (uint32_t v = 1; v < floats_per_key && v < key_size / sizeof(float); ++v) {
            off += snprintf(line + off, sizeof(line) - (size_t)off,
                            " %.6g", (double)key[v]);
        }
        fprintf(out, "%s\n", line);
    }

    if (ctrl->key_count > 20)
        fprintf(out, "    ... (%u more keys)\n", ctrl->key_count - 20);
}

/** Print raw hex for unknown key types */
static void print_keys_hex(FILE *out, const nmo_objanim_controller_t *ctrl,
                           uint32_t key_size) {
    if (!ctrl->data || ctrl->key_count == 0) return;

    const uint8_t *bp = (const uint8_t *)ctrl->data;
    uint32_t show = ctrl->key_count > 5 ? 5 : ctrl->key_count;

    for (uint32_t k = 0; k < show; ++k) {
        const uint8_t *key = bp + k * key_size;
        char line[512];
        int off = snprintf(line, sizeof(line), "    [%u] ", k);
        uint32_t bytes = key_size > 32 ? 32 : key_size;
        for (uint32_t b = 0; b < bytes; ++b)
            off += snprintf(line + off, sizeof(line) - (size_t)off, "%02x", key[b]);
        if (key_size > 32)
            off += snprintf(line + off, sizeof(line) - (size_t)off, "...");
        fprintf(out, "%s\n", line);
    }

    if (ctrl->key_count > 5)
        fprintf(out, "    ... (%u more keys)\n", ctrl->key_count - 5);
}

/** Add decoded keys to JSON array */
static void add_keys_json(yyjson_mut_doc *doc, yyjson_mut_val *keys_arr,
                          const nmo_objanim_controller_t *ctrl,
                          uint32_t key_size) {
    if (!ctrl->data || ctrl->key_count == 0) return;

    uint32_t floats_per_key = key_size / sizeof(float);

    if (key_size == 16 || key_size == 20) {
        /* Known float layout */
        const float *fp = (const float *)ctrl->data;
        for (uint32_t k = 0; k < ctrl->key_count; ++k) {
            const float *key = fp + k * floats_per_key;
            yyjson_mut_val *kobj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_real(doc, kobj, "time", (double)key[0]);
            yyjson_mut_val *vals = yyjson_mut_arr(doc);
            for (uint32_t v = 1; v < floats_per_key; ++v)
                yyjson_mut_arr_add_real(doc, vals, (double)key[v]);
            yyjson_mut_obj_add_val(doc, kobj, "values", vals);
            yyjson_mut_arr_add_val(keys_arr, kobj);
        }
    } else if (key_size > 0 && key_size % sizeof(float) == 0) {
        /* Float-aligned but not standard - still decode as floats */
        const float *fp = (const float *)ctrl->data;
        for (uint32_t k = 0; k < ctrl->key_count; ++k) {
            const float *key = fp + k * floats_per_key;
            yyjson_mut_val *kobj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_real(doc, kobj, "time", (double)key[0]);
            yyjson_mut_val *vals = yyjson_mut_arr(doc);
            for (uint32_t v = 1; v < floats_per_key; ++v)
                yyjson_mut_arr_add_real(doc, vals, (double)key[v]);
            yyjson_mut_obj_add_val(doc, kobj, "values", vals);
            yyjson_mut_arr_add_val(keys_arr, kobj);
        }
    } else {
        /* Unknown layout - hex encode */
        const uint8_t *bp = (const uint8_t *)ctrl->data;
        for (uint32_t k = 0; k < ctrl->key_count; ++k) {
            const uint8_t *key_data = bp + k * key_size;
            yyjson_mut_val *kobj = yyjson_mut_obj(doc);
            /* Build hex string */
            char *hex = (char *)malloc(key_size * 2 + 1);
            if (hex) {
                for (uint32_t b = 0; b < key_size; ++b)
                    snprintf(hex + b * 2, 3, "%02x", key_data[b]);
                yyjson_mut_obj_add_strcpy(doc, kobj, "hex", hex);
                free(hex);
            }
            yyjson_mut_arr_add_val(keys_arr, kobj);
        }
    }
}

int nmo_cmd_animation_keys(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--id",   "-i", NMO_OPT_UINT,   "Object animation ID"},
        {"--name", "-n", NMO_OPT_STRING, "Object animation name"},
    };
    enum { OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = (!has_selector_opt && r.pos_count >= 2) ? r.pos_args[0] : NULL;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_core_object_selector_t selector = {
        .has_id = vals[OPT_ID].present,
        .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
        .positional_id = positional_id,
        .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
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

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "id", obj_id);
        yyjson_mut_obj_add_uint(doc, data, "controller_count", st->controller_count);

        yyjson_mut_val *ctrl_arr = yyjson_mut_arr(doc);
        for (uint32_t ci = 0; ci < st->controller_count; ++ci) {
            const nmo_objanim_controller_t *ctrl = &st->controllers[ci];
            yyjson_mut_val *cobj = yyjson_mut_obj(doc);

            char type_hex[16];
            snprintf(type_hex, sizeof(type_hex), "0x%08x", ctrl->type);
            yyjson_mut_obj_add_strcpy(doc, cobj, "type", type_hex);
            yyjson_mut_obj_add_str(doc, cobj, "type_name",
                                   controller_type_name(ctrl->type));
            yyjson_mut_obj_add_uint(doc, cobj, "key_count", ctrl->key_count);

            uint32_t key_size = nmo_objanim_controller_key_size(ctrl->type);
            yyjson_mut_obj_add_uint(doc, cobj, "key_size", key_size);
            yyjson_mut_obj_add_uint(doc, cobj, "data_size", ctrl->data_size);

            yyjson_mut_val *keys_arr = yyjson_mut_arr(doc);
            add_keys_json(doc, keys_arr, ctrl, key_size);
            yyjson_mut_obj_add_val(doc, cobj, "keys", keys_arr);

            yyjson_mut_arr_add_val(ctrl_arr, cobj);
        }
        yyjson_mut_obj_add_val(doc, data, "controllers", ctrl_arr);

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
            yyjson_mut_obj_add_val(doc, data, "morph_keys", morph_arr);
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "animation.keys");
    } else {
        const char *name = nmo_object_get_name(obj);
        fprintf(c.out, "Animation keys for #%u", obj_id);
        if (name && name[0])
            fprintf(c.out, " (%s)", name);
        fprintf(c.out, " - %u controllers\n\n", st->controller_count);

        for (uint32_t ci = 0; ci < st->controller_count; ++ci) {
            const nmo_objanim_controller_t *ctrl = &st->controllers[ci];
            uint32_t key_size = nmo_objanim_controller_key_size(ctrl->type);

            fprintf(c.out, "Controller [%u]: type=0x%08x (%s), keys=%u, "
                    "key_size=%u, data=%u bytes\n",
                    ci, ctrl->type, controller_type_name(ctrl->type),
                    ctrl->key_count, key_size, ctrl->data_size);

            if (key_size == 16) {
                print_keys_text(c.out, ctrl, key_size, 4, c.colorize);
            } else if (key_size == 20) {
                print_keys_text(c.out, ctrl, key_size, 5, c.colorize);
            } else if (key_size > 0 && key_size % sizeof(float) == 0) {
                print_keys_text(c.out, ctrl, key_size,
                                key_size / (uint32_t)sizeof(float), c.colorize);
            } else if (key_size > 0) {
                print_keys_hex(c.out, ctrl, key_size);
            }
            fprintf(c.out, "\n");
        }

        /* Morph keys */
        if (st->morph_key_parsed_count > 0) {
            fprintf(c.out, "Morph keys: %u\n", st->morph_key_parsed_count);
            uint32_t show = st->morph_key_parsed_count > 20 ? 20 : st->morph_key_parsed_count;
            for (uint32_t mi = 0; mi < show; ++mi) {
                const nmo_objanim_morph_key_t *mk = &st->morph_keys[mi];
                fprintf(c.out, "  [%u] time=%.4f, data_size=%u\n",
                        mi, (double)mk->time_step, mk->data_size);
            }
            if (st->morph_key_parsed_count > 20)
                fprintf(c.out, "  ... (%u more)\n", st->morph_key_parsed_count - 20);
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
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

        char type_hex[16];
        snprintf(type_hex, sizeof(type_hex), "0x%08x", ctrl->type);
        yyjson_mut_obj_add_strcpy(doc, cobj, "type", type_hex);
        yyjson_mut_obj_add_str(doc, cobj, "type_name",
                               controller_type_name(ctrl->type));
        yyjson_mut_obj_add_uint(doc, cobj, "key_count", ctrl->key_count);

        uint32_t key_size = nmo_objanim_controller_key_size(ctrl->type);
        yyjson_mut_obj_add_uint(doc, cobj, "key_size", key_size);

        yyjson_mut_val *keys_arr = yyjson_mut_arr(doc);
        add_keys_json(doc, keys_arr, ctrl, key_size);
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
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    args->has_id = vals[OPT_ID].present;
    args->id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0;
    args->name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL;
    args->out_dir = vals[OPT_OUT_DIR].present ? vals[OPT_OUT_DIR].val.str : NULL;
    args->export_all = vals[OPT_ALL].present && vals[OPT_ALL].val.flag;

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

    if (args->export_all) {
        nmo_object_query_t query = {0};
        nmo_core_query_set_class_id(&query, NMO_CID_OBJECTANIMATION, false);

        animation_export_data_t export_data = { .out_dir = args->out_dir };
        int rc = nmo_core_object_query_run(&c, &query,
                                       animation_export_visitor, &export_data, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
        }
        fprintf(c.out, "Exported %u animations to %s\n",
                export_data.exported, args->out_dir);
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
    }

    return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
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
        {"--output",  "-o", NMO_OPT_STRING, "Output file (required unless --dry-run)"},
        {"--replace", NULL, NMO_OPT_STRING, "Replace existing animation by ID"},
        {"--replace-name", NULL, NMO_OPT_STRING, "Replace existing animation by exact name"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_REPLACE, OPT_REPLACE_NAME, OPT_DRYRUN, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    const char *replace_str = vals[OPT_REPLACE].present ? vals[OPT_REPLACE].val.str : NULL;
    const char *replace_name = vals[OPT_REPLACE_NAME].present ? vals[OPT_REPLACE_NAME].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;

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
        uint32_t key_size = nmo_objanim_controller_key_size(type);

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

    if (dry_run) {
        fprintf(c.out, "[dry-run] Imported animation data; no output written\n");
    } else {
        fprintf(stderr, "Saved: %s\n", output_path);
    }
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

