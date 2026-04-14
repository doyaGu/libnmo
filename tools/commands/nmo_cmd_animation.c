/**
 * @file nmo_cmd_animation.c
 * @brief CLI animation command group implementation
 */

#include "nmo_cmd_animation.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "session/nmo_session.h"
#include "session/nmo_context.h"
#include "app/nmo_save.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "core/nmo_arena.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

/** Check if an object is an animation-related class */
static bool is_animation_class(const nmo_cmd_ctx_t *c, nmo_class_id_t cid) {
    if (cid == NMO_CID_OBJECTANIMATION || cid == NMO_CID_KEYEDANIMATION)
        return true;
    if (c->registry &&
        nmo_type_registry_is_class_derived_from(c->registry, cid, NMO_CID_ANIMATION))
        return true;
    return false;
}

/* ============================================================================
 * animation list
 * ============================================================================ */

int nmo_cmd_animation_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    (void)nmo_session_get_objects(c.session, &objects, &object_count);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        uint32_t found = 0;

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            if (!obj) continue;
            nmo_class_id_t cid = nmo_object_get_class_id(obj);
            if (!is_animation_class(&c, cid)) continue;

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "id", nmo_object_get_id(obj));

            char cls_buf[32];
            const char *cls = nmo_core_class_name_or(&c, cid, cls_buf, sizeof(cls_buf));
            yyjson_mut_obj_add_str(doc, item, "class", cls);

            const char *name = nmo_object_get_name(obj);
            nmo_cli_json_add_str_safe(doc, item, "name",
                                      (name && name[0]) ? name : "");

            /* Extract common fields based on class */
            if (cid == NMO_CID_OBJECTANIMATION) {
                nmo_objectanimation_state_t *st =
                    (nmo_objectanimation_state_t *)nmo_object_get_data(obj);
                if (st) {
                    yyjson_mut_obj_add_real(doc, item, "length",
                        st->has_length ? (double)st->length : 0.0);
                    yyjson_mut_obj_add_uint(doc, item, "entity_id", st->entity_id);
                }
            } else {
                /* CKAnimation / CKKeyedAnimation base */
                nmo_animation_state_t *st =
                    (nmo_animation_state_t *)nmo_object_get_data(obj);
                if (st) {
                    yyjson_mut_obj_add_real(doc, item, "length",
                        st->has_length ? (double)st->length : 0.0);
                    yyjson_mut_obj_add_real(doc, item, "frame_rate",
                        st->has_data ? (double)st->frame_rate : 0.0);
                }
            }

            yyjson_mut_arr_add_val(arr, item);
            found++;
        }

        yyjson_mut_obj_add_uint(doc, data, "count", found);
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
        uint32_t found = 0;

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            if (!obj) continue;
            nmo_class_id_t cid = nmo_object_get_class_id(obj);
            if (!is_animation_class(&c, cid)) continue;

            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

            char cls_buf[32];
            const char *cls = nmo_core_class_name_or(&c, cid, cls_buf, sizeof(cls_buf));

            const char *name = nmo_object_get_name(obj);
            if (!name || !name[0]) name = "-";

            char len_buf[16] = "-";
            char fps_buf[16] = "-";
            char target_buf[16] = "-";

            if (cid == NMO_CID_OBJECTANIMATION) {
                nmo_objectanimation_state_t *st =
                    (nmo_objectanimation_state_t *)nmo_object_get_data(obj);
                if (st) {
                    if (st->has_length)
                        snprintf(len_buf, sizeof(len_buf), "%.1f", (double)st->length);
                    snprintf(target_buf, sizeof(target_buf), "%u", st->entity_id);
                }
            } else {
                nmo_animation_state_t *st =
                    (nmo_animation_state_t *)nmo_object_get_data(obj);
                if (st) {
                    if (st->has_length)
                        snprintf(len_buf, sizeof(len_buf), "%.1f", (double)st->length);
                    if (st->has_data)
                        snprintf(fps_buf, sizeof(fps_buf), "%.1f", (double)st->frame_rate);
                    if (st->has_root_entity)
                        snprintf(target_buf, sizeof(target_buf), "%u", st->root_entity_id);
                }
            }

            const char *cells[] = {id_buf, cls, name, len_buf, fps_buf, target_buf};
            nmo_cli_table_add_row(&table, cells, 6);
            found++;
        }

        fprintf(c.out, "Animations: %u\n\n", found);
        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * animation show
 * ============================================================================ */

int nmo_cmd_animation_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--id", "-i", NMO_OPT_STRING, "Object ID"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *id_str = vals[0].present ? vals[0].val.str : NULL;
    if (!id_str && r.pos_count >= 2)
        id_str = r.pos_args[0];

    if (!id_str) {
        fprintf(stderr, "Error: No object ID specified\n");
        fprintf(stderr, "Usage: nmo animation show <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t obj_id;
    if (!nmo_tool_parse_u32_dec(id_str, &obj_id)) {
        fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_t *obj = nmo_core_find_by_id(&c, obj_id);
    if (!obj) {
        fprintf(stderr, "Error: Object %u not found\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_NOT_FOUND);
    }

    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    if (!is_animation_class(&c, cid)) {
        fprintf(stderr, "Error: Object %u is not an animation class\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const char *name = nmo_object_get_name(obj);
    char cls_buf[32];
    const char *cls = nmo_core_class_name_or(&c, cid, cls_buf, sizeof(cls_buf));

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "id", obj_id);
        yyjson_mut_obj_add_str(doc, data, "class", cls);
        nmo_cli_json_add_str_safe(doc, data, "name",
                                  (name && name[0]) ? name : "");

        if (cid == NMO_CID_OBJECTANIMATION) {
            nmo_objectanimation_state_t *st =
                (nmo_objectanimation_state_t *)nmo_object_get_data(obj);
            if (st) {
                yyjson_mut_obj_add_str(doc, data, "format",
                                       animation_format_name(st->format));
                yyjson_mut_obj_add_uint(doc, data, "flags", st->flags);
                yyjson_mut_obj_add_uint(doc, data, "entity_id", st->entity_id);
                if (st->has_length)
                    yyjson_mut_obj_add_real(doc, data, "length", (double)st->length);
                yyjson_mut_obj_add_uint(doc, data, "controller_count", st->controller_count);
                if (st->has_morph_counts) {
                    yyjson_mut_obj_add_int(doc, data, "morph_vertex_count", st->morph_vertex_count);
                    yyjson_mut_obj_add_int(doc, data, "morph_key_count", st->morph_key_count);
                }
                if (st->has_merge) {
                    yyjson_mut_obj_add_real(doc, data, "merge_factor", (double)st->merge_factor);
                    yyjson_mut_obj_add_uint(doc, data, "anim1_id", st->anim1_id);
                    yyjson_mut_obj_add_uint(doc, data, "anim2_id", st->anim2_id);
                }
            }
        } else if (cid == NMO_CID_KEYEDANIMATION) {
            nmo_keyedanimation_state_t *st =
                (nmo_keyedanimation_state_t *)nmo_object_get_data(obj);
            if (st) {
                /* Base animation fields */
                if (st->base.has_data) {
                    yyjson_mut_obj_add_uint(doc, data, "flags", st->base.flags);
                    yyjson_mut_obj_add_real(doc, data, "frame_rate", (double)st->base.frame_rate);
                }
                if (st->base.has_length)
                    yyjson_mut_obj_add_real(doc, data, "length", (double)st->base.length);

                yyjson_mut_obj_add_uint(doc, data, "animation_count", st->animation_count);
                yyjson_mut_val *anim_arr = yyjson_mut_arr(doc);
                for (uint32_t ai = 0; ai < st->animation_count; ++ai) {
                    yyjson_mut_val *entry = yyjson_mut_obj(doc);
                    yyjson_mut_obj_add_uint(doc, entry, "id", st->animation_ids[ai]);
                    nmo_object_t *aobj = nmo_core_find_by_id(&c, st->animation_ids[ai]);
                    if (aobj) {
                        const char *aname = nmo_object_get_name(aobj);
                        nmo_cli_json_add_str_safe(doc, entry, "name",
                            (aname && aname[0]) ? aname : "");
                    }
                    yyjson_mut_arr_add_val(anim_arr, entry);
                }
                yyjson_mut_obj_add_val(doc, data, "animations", anim_arr);

                yyjson_mut_obj_add_uint(doc, data, "subanim_count", st->subanim_count);
                if (st->has_merge) {
                    yyjson_mut_obj_add_int(doc, data, "merged", st->merged);
                    yyjson_mut_obj_add_real(doc, data, "merge_factor", (double)st->merge_factor);
                }
            }
        } else {
            /* CKAnimation base */
            nmo_animation_state_t *st =
                (nmo_animation_state_t *)nmo_object_get_data(obj);
            if (st) {
                if (st->has_data) {
                    yyjson_mut_obj_add_uint(doc, data, "flags", st->flags);
                    yyjson_mut_obj_add_real(doc, data, "frame_rate", (double)st->frame_rate);
                }
                if (st->has_length)
                    yyjson_mut_obj_add_real(doc, data, "length", (double)st->length);
                if (st->has_root_entity)
                    yyjson_mut_obj_add_uint(doc, data, "root_entity_id", st->root_entity_id);
                if (st->has_character)
                    yyjson_mut_obj_add_uint(doc, data, "character_id", st->character_id);
                if (st->has_current_step)
                    yyjson_mut_obj_add_real(doc, data, "current_step", (double)st->current_step);
            }
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "animation.show");
    } else {
        nmo_cli_print_heading(c.out, "Animation", c.colorize);

        char id_buf2[16];
        snprintf(id_buf2, sizeof(id_buf2), "%u", obj_id);
        nmo_cli_print_kv(c.out, "ID", id_buf2, 18, c.colorize);
        nmo_cli_print_kv(c.out, "Class", cls, 18, c.colorize);
        nmo_cli_print_kv(c.out, "Name",
                         (name && name[0]) ? name : "-", 18, c.colorize);

        if (cid == NMO_CID_OBJECTANIMATION) {
            nmo_objectanimation_state_t *st =
                (nmo_objectanimation_state_t *)nmo_object_get_data(obj);
            if (st) {
                nmo_cli_print_kv(c.out, "Format",
                                 animation_format_name(st->format), 18, c.colorize);

                char flags_buf[16];
                snprintf(flags_buf, sizeof(flags_buf), "0x%08x", st->flags);
                nmo_cli_print_kv(c.out, "Flags", flags_buf, 18, c.colorize);

                char eid_buf[16];
                snprintf(eid_buf, sizeof(eid_buf), "%u", st->entity_id);
                nmo_cli_print_kv(c.out, "Entity ID", eid_buf, 18, c.colorize);

                if (st->has_length) {
                    char len_buf[16];
                    snprintf(len_buf, sizeof(len_buf), "%.2f", (double)st->length);
                    nmo_cli_print_kv(c.out, "Length", len_buf, 18, c.colorize);
                }

                char ctrl_buf[16];
                snprintf(ctrl_buf, sizeof(ctrl_buf), "%u", st->controller_count);
                nmo_cli_print_kv(c.out, "Controllers", ctrl_buf, 18, c.colorize);

                /* Controller summary */
                for (uint32_t ci = 0; ci < st->controller_count; ++ci) {
                    const nmo_objanim_controller_t *ctrl = &st->controllers[ci];
                    char line[128];
                    snprintf(line, sizeof(line),
                             "type=0x%08x (%s), keys=%u, data=%u bytes",
                             ctrl->type, controller_type_name(ctrl->type),
                             ctrl->key_count, ctrl->data_size);
                    char label[16];
                    snprintf(label, sizeof(label), "  [%u]", ci);
                    nmo_cli_print_kv(c.out, label, line, 18, c.colorize);
                }

                if (st->has_morph_counts) {
                    char morph_buf[32];
                    snprintf(morph_buf, sizeof(morph_buf), "%d vertices, %d keys",
                             st->morph_vertex_count, st->morph_key_count);
                    nmo_cli_print_kv(c.out, "Morph", morph_buf, 18, c.colorize);
                }

                if (st->has_merge) {
                    char merge_buf[64];
                    snprintf(merge_buf, sizeof(merge_buf),
                             "factor=%.2f, anim1=%u, anim2=%u",
                             (double)st->merge_factor, st->anim1_id, st->anim2_id);
                    nmo_cli_print_kv(c.out, "Merge", merge_buf, 18, c.colorize);
                }
            }
        } else if (cid == NMO_CID_KEYEDANIMATION) {
            nmo_keyedanimation_state_t *st =
                (nmo_keyedanimation_state_t *)nmo_object_get_data(obj);
            if (st) {
                if (st->base.has_data) {
                    char flags_buf[16];
                    snprintf(flags_buf, sizeof(flags_buf), "0x%08x", st->base.flags);
                    nmo_cli_print_kv(c.out, "Flags", flags_buf, 18, c.colorize);

                    char fps_buf[16];
                    snprintf(fps_buf, sizeof(fps_buf), "%.2f", (double)st->base.frame_rate);
                    nmo_cli_print_kv(c.out, "Frame Rate", fps_buf, 18, c.colorize);
                }

                if (st->base.has_length) {
                    char len_buf[16];
                    snprintf(len_buf, sizeof(len_buf), "%.2f", (double)st->base.length);
                    nmo_cli_print_kv(c.out, "Length", len_buf, 18, c.colorize);
                }

                char ac_buf[16];
                snprintf(ac_buf, sizeof(ac_buf), "%u", st->animation_count);
                nmo_cli_print_kv(c.out, "Animations", ac_buf, 18, c.colorize);

                for (uint32_t ai = 0; ai < st->animation_count; ++ai) {
                    nmo_object_t *aobj = nmo_core_find_by_id(&c, st->animation_ids[ai]);
                    const char *aname = aobj ? nmo_object_get_name(aobj) : NULL;
                    char line[128];
                    if (aname && aname[0])
                        snprintf(line, sizeof(line), "#%u (%s)", st->animation_ids[ai], aname);
                    else
                        snprintf(line, sizeof(line), "#%u", st->animation_ids[ai]);
                    char label[16];
                    snprintf(label, sizeof(label), "  [%u]", ai);
                    nmo_cli_print_kv(c.out, label, line, 18, c.colorize);
                }

                char sa_buf[16];
                snprintf(sa_buf, sizeof(sa_buf), "%u", st->subanim_count);
                nmo_cli_print_kv(c.out, "Subanims", sa_buf, 18, c.colorize);

                if (st->has_merge) {
                    char merge_buf[64];
                    snprintf(merge_buf, sizeof(merge_buf),
                             "merged=%d, factor=%.2f",
                             st->merged, (double)st->merge_factor);
                    nmo_cli_print_kv(c.out, "Merge", merge_buf, 18, c.colorize);
                }
            }
        } else {
            /* CKAnimation base */
            nmo_animation_state_t *st =
                (nmo_animation_state_t *)nmo_object_get_data(obj);
            if (st) {
                if (st->has_data) {
                    char flags_buf[16];
                    snprintf(flags_buf, sizeof(flags_buf), "0x%08x", st->flags);
                    nmo_cli_print_kv(c.out, "Flags", flags_buf, 18, c.colorize);

                    char fps_buf[16];
                    snprintf(fps_buf, sizeof(fps_buf), "%.2f", (double)st->frame_rate);
                    nmo_cli_print_kv(c.out, "Frame Rate", fps_buf, 18, c.colorize);
                }
                if (st->has_length) {
                    char len_buf[16];
                    snprintf(len_buf, sizeof(len_buf), "%.2f", (double)st->length);
                    nmo_cli_print_kv(c.out, "Length", len_buf, 18, c.colorize);
                }
                if (st->has_root_entity) {
                    char re_buf[16];
                    snprintf(re_buf, sizeof(re_buf), "%u", st->root_entity_id);
                    nmo_cli_print_kv(c.out, "Root Entity", re_buf, 18, c.colorize);
                }
                if (st->has_character) {
                    char ch_buf[16];
                    snprintf(ch_buf, sizeof(ch_buf), "%u", st->character_id);
                    nmo_cli_print_kv(c.out, "Character", ch_buf, 18, c.colorize);
                }
                if (st->has_current_step) {
                    char cs_buf[16];
                    snprintf(cs_buf, sizeof(cs_buf), "%.2f", (double)st->current_step);
                    nmo_cli_print_kv(c.out, "Current Step", cs_buf, 18, c.colorize);
                }
            }
        }
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
        {"--id", "-i", NMO_OPT_STRING, "Object ID"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *id_str = vals[0].present ? vals[0].val.str : NULL;
    if (!id_str && r.pos_count >= 2)
        id_str = r.pos_args[0];

    if (!id_str) {
        fprintf(stderr, "Error: No object ID specified\n");
        fprintf(stderr, "Usage: nmo animation keys <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t obj_id;
    if (!nmo_tool_parse_u32_dec(id_str, &obj_id)) {
        fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_t *obj = nmo_core_find_by_id(&c, obj_id);
    if (!obj) {
        fprintf(stderr, "Error: Object %u not found\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_NOT_FOUND);
    }
    if (nmo_object_get_class_id(obj) != NMO_CID_OBJECTANIMATION) {
        fprintf(stderr, "Error: Object %u is not a CKObjectAnimation (class 15 required)\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_objectanimation_state_t *st =
        (nmo_objectanimation_state_t *)nmo_object_get_data(obj);
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
    char safe_name[256];
    if (name && name[0]) {
        nmo_tool_sanitize_filename(safe_name, sizeof(safe_name), name, obj_id);
    } else {
        snprintf(safe_name, sizeof(safe_name), "anim_%u", obj_id);
    }

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s_%u.anim.json", out_dir, safe_name, obj_id);

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) return -1;

    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_uint(doc, root, "id", obj_id);
    yyjson_mut_obj_add_str(doc, root, "class", "CKObjectAnimation");
    yyjson_mut_obj_add_str(doc, root, "format", animation_format_name(st->format));
    yyjson_mut_obj_add_uint(doc, root, "entity_id", st->entity_id);
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
        return -1;
    }

    fprintf(stderr, "Exported: %s\n", path);
    return 0;
}

int nmo_cmd_animation_export(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--id",      "-i", NMO_OPT_STRING, "Object ID"},
        {"--out-dir", "-d", NMO_OPT_STRING, "Output directory (required)"},
        {"--all",     NULL, NMO_OPT_FLAG,   "Export all CKObjectAnimation objects"},
    };
    nmo_opt_val_t vals[3];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 3, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *id_str = vals[0].present ? vals[0].val.str : NULL;
    const char *out_dir = vals[1].present ? vals[1].val.str : NULL;
    bool export_all = vals[2].present && vals[2].val.flag;

    /* Positional: <id> <file> or just <file> with --all */
    if (!id_str && !export_all && r.pos_count >= 2)
        id_str = r.pos_args[0];

    if (!out_dir) {
        fprintf(stderr, "Error: --out-dir is required\n");
        fprintf(stderr, "Usage: nmo animation export [--all | <id>] --out-dir <dir> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!id_str && !export_all) {
        fprintf(stderr, "Error: Specify --id <n> or --all\n");
        fprintf(stderr, "Usage: nmo animation export [--all | <id>] --out-dir <dir> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    if (export_all) {
        nmo_object_t **objects = NULL;
        size_t object_count = 0;
        (void)nmo_session_get_objects(c.session, &objects, &object_count);

        uint32_t exported = 0;
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            if (!obj || nmo_object_get_class_id(obj) != NMO_CID_OBJECTANIMATION)
                continue;

            nmo_objectanimation_state_t *st =
                (nmo_objectanimation_state_t *)nmo_object_get_data(obj);
            if (!st) continue;

            if (export_one_animation(st, obj, out_dir) == 0)
                exported++;
        }
        fprintf(stderr, "Exported %u animations to %s\n", exported, out_dir);
    } else {
        uint32_t obj_id;
        if (!nmo_tool_parse_u32_dec(id_str, &obj_id)) {
            fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }

        nmo_object_t *obj = nmo_core_find_by_id(&c, obj_id);
        if (!obj) {
            fprintf(stderr, "Error: Object %u not found\n", obj_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_NOT_FOUND);
        }
        if (nmo_object_get_class_id(obj) != NMO_CID_OBJECTANIMATION) {
            fprintf(stderr, "Error: Object %u is not a CKObjectAnimation\n", obj_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }

        nmo_objectanimation_state_t *st =
            (nmo_objectanimation_state_t *)nmo_object_get_data(obj);
        if (!st) {
            fprintf(stderr, "Error: No data for object %u\n", obj_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        if (export_one_animation(st, obj, out_dir) != 0)
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * animation import
 * ============================================================================ */

/** Parse a hex string like "0x637c4301" to uint32_t */
static bool parse_hex_u32(const char *str, uint32_t *out) {
    if (!str) return false;
    char *end = NULL;
    unsigned long val = strtoul(str, &end, 16);
    if (end == str || *end != '\0') {
        /* Try with 0x prefix stripped */
        if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
            val = strtoul(str + 2, &end, 16);
            if (end == str + 2 || *end != '\0') return false;
        } else {
            return false;
        }
    }
    *out = (uint32_t)val;
    return true;
}

/** Decode hex string to bytes. Returns number of bytes decoded. */
static size_t hex_decode(const char *hex, uint8_t *out, size_t out_size) {
    size_t len = strlen(hex);
    size_t bytes = len / 2;
    if (bytes > out_size) bytes = out_size;
    for (size_t i = 0; i < bytes; ++i) {
        unsigned int b;
        if (sscanf(hex + i * 2, "%2x", &b) != 1) return i;
        out[i] = (uint8_t)b;
    }
    return bytes;
}

int nmo_cmd_animation_import(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output file (required)"},
        {"--replace", NULL, NMO_OPT_STRING, "Replace existing animation by ID"},
    };
    nmo_opt_val_t vals[2];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 2, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[0].present ? vals[0].val.str : NULL;
    const char *replace_str = vals[1].present ? vals[1].val.str : NULL;

    if (!output_path) {
        fprintf(stderr, "Error: --output/-o is required\n");
        fprintf(stderr, "Usage: nmo animation import <json-file> <nmo-file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 2) {
        fprintf(stderr, "Usage: nmo animation import <json-file> <nmo-file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *json_path = r.pos_args[0];
    const char *nmo_path = r.pos_args[1];

    /* Parse replace ID if given */
    uint32_t replace_id = 0;
    bool do_replace = false;
    if (replace_str) {
        if (!nmo_tool_parse_u32_dec(replace_str, &replace_id)) {
            fprintf(stderr, "Error: Invalid replace ID '%s'\n", replace_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
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
    double length_val = yyjson_get_real(yyjson_obj_get(jroot, "length"));
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
    int rc = nmo_cmd_ctx_init_with_file(&c, nmo_path, global);
    if (rc) {
        yyjson_doc_free(jdoc);
        return rc;
    }

    nmo_arena_t *arena = nmo_session_get_arena(c.session);

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
                    fp[0] = (float)yyjson_get_real(jtime);

                yyjson_val *jvals = yyjson_obj_get(jkey, "values");
                if (yyjson_is_arr(jvals)) {
                    size_t nvals = yyjson_arr_size(jvals);
                    for (size_t vi = 0; vi < nvals && vi + 1 < floats_per_key; ++vi) {
                        yyjson_val *jv = yyjson_arr_get(jvals, vi);
                        fp[vi + 1] = (float)yyjson_get_real(jv);
                    }
                }
            }
        }

        controllers[ci].type = type;
        controllers[ci].key_count = key_count;
        controllers[ci].data_size = data_size;
        controllers[ci].data = data;
    }

    yyjson_doc_free(jdoc);

    if (!parse_ok) {
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    /* Find or create the target object */
    nmo_object_t *target = NULL;
    if (do_replace) {
        target = nmo_core_find_by_id(&c, replace_id);
        if (!target) {
            fprintf(stderr, "Error: Object %u not found for replace\n", replace_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_NOT_FOUND);
        }
        if (nmo_object_get_class_id(target) != NMO_CID_OBJECTANIMATION) {
            fprintf(stderr, "Error: Object %u is not a CKObjectAnimation\n", replace_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    } else {
        nmo_object_id_t new_id = 0;
        nmo_runtime_report_t report;
        memset(&report, 0, sizeof(report));
        nmo_guid_t zero_guid;
        memset(&zero_guid, 0, sizeof(zero_guid));
        int cr = nmo_session_create_object(c.session, NMO_CID_OBJECTANIMATION,
                                           "imported_anim", zero_guid,
                                           &new_id, &report);
        if (cr != 0) {
            fprintf(stderr, "Error: Failed to create animation object: %d\n", cr);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        target = nmo_core_find_by_id(&c, new_id);
        if (!target) {
            fprintf(stderr, "Error: Created object %u not found\n", new_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        fprintf(stderr, "Created CKObjectAnimation #%u\n", new_id);
    }

    /* Update state */
    nmo_objectanimation_state_t *st =
        (nmo_objectanimation_state_t *)nmo_object_get_data(target);
    if (!st) {
        /* Allocate fresh state */
        st = (nmo_objectanimation_state_t *)nmo_arena_alloc(
            arena, sizeof(nmo_objectanimation_state_t),
            _Alignof(nmo_objectanimation_state_t));
        if (!st) {
            fprintf(stderr, "Error: Out of memory\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        memset(st, 0, sizeof(*st));
        nmo_object_set_data(target, st);
    }

    st->format = format;
    st->entity_id = entity_id;
    st->flags = flags_val;
    st->has_length = 1;
    st->length = (float)length_val;
    st->controller_count = ctrl_count;
    st->controllers = controllers;

    /* Save */
    int save_rc = nmo_save_file(c.session, output_path, NULL);
    if (save_rc != 0) {
        fprintf(stderr, "Error: Failed to save '%s': %d\n", output_path, save_rc);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    fprintf(stderr, "Saved: %s\n", output_path);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}
