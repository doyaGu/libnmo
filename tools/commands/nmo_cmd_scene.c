/**
 * @file nmo_cmd_scene.c
 * @brief CLI scene/level command group implementation
 */

#include "nmo_cmd_scene.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_write.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/builtin/nmo_level_schemas.h"

#include <stdio.h>
#include <string.h>

int nmo_cmd_scene_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: scene list|show ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_public_handler_t handler = NULL;
    if (strcmp(argv[0], "list") == 0 || strcmp(argv[0], "ls") == 0) {
        handler = nmo_cmd_scene_list;
    } else if (strcmp(argv[0], "show") == 0 || strcmp(argv[0], "s") == 0) {
        handler = nmo_cmd_scene_show;
    } else {
        fprintf(stderr, "Unsupported scene read action in session: %s\n", argv[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    return nmo_cmd_ctx_dispatch_from_source(ctx, argc, argv, handler);
}

/* ============================================================================
 * Helpers
 * ============================================================================ */

static const char *resolve_name(const nmo_cmd_ctx_t *c, nmo_object_id_t id) {
    if (id == 0) return NULL;
    nmo_object_t *obj = nmo_core_find_by_id(c, id);
    if (!obj) return NULL;
    return nmo_object_get_name(obj);
}

static const char *fog_mode_str(uint32_t mode) {
    switch (mode) {
    case 0: return "none";
    case 1: return "linear";
    case 2: return "exp";
    case 3: return "exp2";
    default: return "unknown";
    }
}

typedef struct scene_list_json_data {
    yyjson_mut_doc *doc;
    yyjson_mut_val *arr;
    uint32_t found;
} scene_list_json_data_t;

typedef struct scene_list_table_data {
    nmo_cli_table_t *table;
    uint32_t found;
} scene_list_table_data_t;

static bool scene_list_query_predicate(const nmo_object_t *object, void *user_data) {
    (void)user_data;
    if (object == NULL) {
        return false;
    }

    nmo_class_id_t cid = nmo_object_get_class_id(object);
    return cid == NMO_CID_SCENE || cid == NMO_CID_LEVEL;
}

static int scene_list_json_visitor(size_t index,
                                   nmo_object_t *obj,
                                   const nmo_cmd_ctx_t *c,
                                   void *user)
{
    (void)index;
    scene_list_json_data_t *data = (scene_list_json_data_t *)user;
    if (obj == NULL || data == NULL || data->doc == NULL || data->arr == NULL) {
        return 0;
    }

    yyjson_mut_doc *doc = data->doc;
    yyjson_mut_val *item = yyjson_mut_obj(doc);
    nmo_object_id_t id = nmo_object_get_id(obj);
    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    yyjson_mut_obj_add_uint(doc, item, "id", id);

    const char *class_name = nmo_core_class_name(c, cid);
    if (class_name) {
        yyjson_mut_obj_add_str(doc, item, "class", class_name);
    }

    const char *name = nmo_object_get_name(obj);
    nmo_cli_json_add_str_safe(doc, item, "name",
                              (name && name[0]) ? name : "");

    if (cid == NMO_CID_SCENE) {
        const nmo_scene_state_t *ss =
            (const nmo_scene_state_t *)nmo_object_get_state(obj);
        if (ss) {
            yyjson_mut_obj_add_uint(doc, item, "object_count",
                                    (uint64_t)ss->object_descs.count);
            if (ss->starting_camera_id) {
                yyjson_mut_obj_add_uint(doc, item, "starting_camera_id",
                                        ss->starting_camera_id);
                const char *cam_name = resolve_name(c, ss->starting_camera_id);
                if (cam_name && cam_name[0]) {
                    nmo_cli_json_add_str_safe(doc, item, "starting_camera", cam_name);
                }
            }
        }
    } else {
        const nmo_level_state_t *ls =
            (const nmo_level_state_t *)nmo_object_get_state(obj);
        if (ls) {
            yyjson_mut_obj_add_uint(doc, item, "scene_count",
                                    (uint64_t)ls->scene_ids.count);
        }
    }

    yyjson_mut_arr_add_val(data->arr, item);
    data->found++;
    return 0;
}

static int scene_list_table_visitor(size_t index,
                                    nmo_object_t *obj,
                                    const nmo_cmd_ctx_t *c,
                                    void *user)
{
    (void)index;
    scene_list_table_data_t *data = (scene_list_table_data_t *)user;
    if (obj == NULL || data == NULL || data->table == NULL) {
        return 0;
    }

    char id_buf[16];
    snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    const char *class_name = nmo_core_class_name(c, cid);
    if (!class_name) class_name = "-";

    const char *name = nmo_object_get_name(obj);
    if (!name || !name[0]) name = "-";

    char obj_count_buf[16];
    char camera_buf[64];
    snprintf(camera_buf, sizeof(camera_buf), "-");

    if (cid == NMO_CID_SCENE) {
        const nmo_scene_state_t *ss =
            (const nmo_scene_state_t *)nmo_object_get_state(obj);
        if (ss) {
            snprintf(obj_count_buf, sizeof(obj_count_buf), "%zu",
                     ss->object_descs.count);
            if (ss->starting_camera_id) {
                const char *cam_name = resolve_name(c, ss->starting_camera_id);
                if (cam_name && cam_name[0]) {
                    snprintf(camera_buf, sizeof(camera_buf), "#%u (%s)",
                             ss->starting_camera_id, cam_name);
                } else {
                    snprintf(camera_buf, sizeof(camera_buf), "#%u",
                             ss->starting_camera_id);
                }
            }
        } else {
            snprintf(obj_count_buf, sizeof(obj_count_buf), "-");
        }
    } else {
        const nmo_level_state_t *ls =
            (const nmo_level_state_t *)nmo_object_get_state(obj);
        if (ls) {
            snprintf(obj_count_buf, sizeof(obj_count_buf), "%zu",
                     ls->scene_ids.count);
        } else {
            snprintf(obj_count_buf, sizeof(obj_count_buf), "-");
        }
    }

    const char *cells[] = {id_buf, class_name, name, obj_count_buf, camera_buf};
    nmo_cli_table_add_row(data->table, cells, 5);
    data->found++;
    return 0;
}

/* ============================================================================
 * scene list
 * ============================================================================ */

int nmo_cmd_scene_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_query_t query = {
        .predicate = scene_list_query_predicate,
    };

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        scene_list_json_data_t jd = { .doc = doc, .arr = arr };
        rc = nmo_core_object_query_run(&c, &query,
                                       scene_list_json_visitor, &jd, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return nmo_cmd_ctx_done(&c, rc);
        }

        yyjson_mut_obj_add_uint(doc, data, "count", jd.found);
        yyjson_mut_obj_add_val(doc, data, "scenes", arr);
        nmo_cmd_ctx_json_end(&c, doc, data, "scene.list");
    } else {
        static const nmo_cli_table_col_t columns[] = {
            {"ID",      NMO_CLI_ALIGN_RIGHT, 6,  0},
            {"CLASS",   NMO_CLI_ALIGN_LEFT,  10, 0},
            {"NAME",    NMO_CLI_ALIGN_LEFT,  20, 50},
            {"OBJECTS", NMO_CLI_ALIGN_RIGHT, 7,  0},
            {"CAMERA",  NMO_CLI_ALIGN_LEFT,  20, 40},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));
        scene_list_table_data_t td = { .table = &table };
        rc = nmo_core_object_query_run(&c, &query,
                                       scene_list_table_visitor, &td, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_cli_table_free(&table);
            return nmo_cmd_ctx_done(&c, rc);
        }

        fprintf(c.out, "Scenes/Levels: %u\n\n", td.found);
        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * scene show
 * ============================================================================ */

int nmo_cmd_scene_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--id",   "-i", NMO_OPT_UINT,   "Scene object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Scene object name"},
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

    static const nmo_class_id_t scene_classes[] = {
        NMO_CID_SCENE,
        NMO_CID_LEVEL,
    };
    nmo_core_object_selector_t selector = {
        .has_id = vals[OPT_ID].present,
        .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
        .positional_id = positional_id,
        .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
        .allowed_class_ids = scene_classes,
        .allowed_class_count = sizeof(scene_classes) / sizeof(scene_classes[0]),
        .selector_label = "Scene",
        .type_label = "CKScene or CKLevel",
    };
    nmo_object_t *obj = NULL;
    nmo_object_id_t obj_id = 0;
    rc = nmo_core_resolve_one_object(&c, &selector, &obj, &obj_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo scene show [--id <id> | --name <name> | <id>] <file>\n");
        return nmo_cmd_ctx_done(&c, rc);
    }

    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    if (class_id != NMO_CID_SCENE && class_id != NMO_CID_LEVEL) {
        fprintf(stderr, "Error: Object %u is not a CKScene or CKLevel (class %u)\n",
                obj_id, class_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const char *name = nmo_object_get_name(obj);
    const char *class_name = nmo_core_class_name(&c, class_id);

    if (class_id == NMO_CID_SCENE) {
        const nmo_scene_state_t *ss =
            (const nmo_scene_state_t *)nmo_object_get_state(obj);

        if (c.is_json) {
            yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
            yyjson_mut_val *data = yyjson_mut_obj(doc);

            yyjson_mut_obj_add_uint(doc, data, "id", obj_id);
            nmo_cli_json_add_str_safe(doc, data, "name",
                                      (name && name[0]) ? name : "");
            if (class_name)
                yyjson_mut_obj_add_str(doc, data, "class", class_name);

            if (ss) {
                char color_buf[16];

                snprintf(color_buf, sizeof(color_buf), "0x%08X", ss->background_color);
                yyjson_mut_obj_add_strcpy(doc, data, "background_color", color_buf);

                snprintf(color_buf, sizeof(color_buf), "0x%08X", ss->ambient_light_color);
                yyjson_mut_obj_add_strcpy(doc, data, "ambient_light_color", color_buf);

                yyjson_mut_obj_add_str(doc, data, "fog_mode",
                                       fog_mode_str(ss->fog_mode));
                snprintf(color_buf, sizeof(color_buf), "0x%08X", ss->fog_color);
                yyjson_mut_obj_add_strcpy(doc, data, "fog_color", color_buf);
                yyjson_mut_obj_add_real(doc, data, "fog_start", (double)ss->fog_start);
                yyjson_mut_obj_add_real(doc, data, "fog_end", (double)ss->fog_end);
                yyjson_mut_obj_add_real(doc, data, "fog_density", (double)ss->fog_density);

                yyjson_mut_obj_add_uint(doc, data, "starting_camera_id",
                                        ss->starting_camera_id);
                const char *cam_name = resolve_name(&c, ss->starting_camera_id);
                if (cam_name && cam_name[0]) {
                    nmo_cli_json_add_str_safe(doc, data, "starting_camera", cam_name);
                }

                yyjson_mut_obj_add_uint(doc, data, "environment_settings",
                                        ss->environment_settings);
                yyjson_mut_obj_add_uint(doc, data, "object_count",
                                        (uint64_t)ss->object_descs.count);
            } else {
                yyjson_mut_obj_add_null(doc, data, "state");
            }

            nmo_cmd_ctx_json_end(&c, doc, data, "scene.show");
        } else {
            nmo_cli_print_heading(c.out, "Scene Details", c.colorize);

            char buf[128];
            snprintf(buf, sizeof(buf), "#%u (%s)", obj_id,
                     (name && name[0]) ? name : "(unnamed)");
            nmo_cli_print_kv(c.out, "ID / Name", buf, 20, c.colorize);

            snprintf(buf, sizeof(buf), "%s", class_name ? class_name : "-");
            nmo_cli_print_kv(c.out, "Class", buf, 20, c.colorize);

            if (!ss) {
                fprintf(c.out, "\n  (no deserialized state)\n");
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
            }

            snprintf(buf, sizeof(buf), "0x%08X", ss->background_color);
            nmo_cli_print_kv(c.out, "Background Color", buf, 20, c.colorize);

            snprintf(buf, sizeof(buf), "0x%08X", ss->ambient_light_color);
            nmo_cli_print_kv(c.out, "Ambient Light", buf, 20, c.colorize);

            snprintf(buf, sizeof(buf), "%s", fog_mode_str(ss->fog_mode));
            nmo_cli_print_kv(c.out, "Fog Mode", buf, 20, c.colorize);

            snprintf(buf, sizeof(buf), "0x%08X", ss->fog_color);
            nmo_cli_print_kv(c.out, "Fog Color", buf, 20, c.colorize);

            snprintf(buf, sizeof(buf), "%.3f", (double)ss->fog_start);
            nmo_cli_print_kv(c.out, "Fog Start", buf, 20, c.colorize);

            snprintf(buf, sizeof(buf), "%.3f", (double)ss->fog_end);
            nmo_cli_print_kv(c.out, "Fog End", buf, 20, c.colorize);

            snprintf(buf, sizeof(buf), "%.6g", (double)ss->fog_density);
            nmo_cli_print_kv(c.out, "Fog Density", buf, 20, c.colorize);

            if (ss->starting_camera_id) {
                const char *cam_name = resolve_name(&c, ss->starting_camera_id);
                if (cam_name && cam_name[0]) {
                    snprintf(buf, sizeof(buf), "#%u (%s)",
                             ss->starting_camera_id, cam_name);
                } else {
                    snprintf(buf, sizeof(buf), "#%u", ss->starting_camera_id);
                }
            } else {
                snprintf(buf, sizeof(buf), "(none)");
            }
            nmo_cli_print_kv(c.out, "Starting Camera", buf, 20, c.colorize);

            snprintf(buf, sizeof(buf), "0x%08X", ss->environment_settings);
            nmo_cli_print_kv(c.out, "Env Settings", buf, 20, c.colorize);

            snprintf(buf, sizeof(buf), "%zu", ss->object_descs.count);
            nmo_cli_print_kv(c.out, "Objects", buf, 20, c.colorize);
        }
    } else { /* NMO_CID_LEVEL */
        const nmo_level_state_t *ls =
            (const nmo_level_state_t *)nmo_object_get_state(obj);

        if (c.is_json) {
            yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
            yyjson_mut_val *data = yyjson_mut_obj(doc);

            yyjson_mut_obj_add_uint(doc, data, "id", obj_id);
            nmo_cli_json_add_str_safe(doc, data, "name",
                                      (name && name[0]) ? name : "");
            if (class_name)
                yyjson_mut_obj_add_str(doc, data, "class", class_name);

            if (ls) {
                yyjson_mut_obj_add_uint(doc, data, "current_scene_id",
                                        ls->current_scene_id);
                const char *cur_name = resolve_name(&c, ls->current_scene_id);
                if (cur_name && cur_name[0]) {
                    nmo_cli_json_add_str_safe(doc, data, "current_scene", cur_name);
                }

                yyjson_mut_obj_add_uint(doc, data, "level_scene_id",
                                        ls->level_scene_id);
                const char *lvl_name = resolve_name(&c, ls->level_scene_id);
                if (lvl_name && lvl_name[0]) {
                    nmo_cli_json_add_str_safe(doc, data, "level_scene", lvl_name);
                }

                yyjson_mut_val *scene_arr = yyjson_mut_arr(doc);
                const nmo_object_id_t *ids =
                    (const nmo_object_id_t *)ls->scene_ids.data;
                for (size_t i = 0; i < ls->scene_ids.count; ++i) {
                    yyjson_mut_val *entry = yyjson_mut_obj(doc);
                    yyjson_mut_obj_add_uint(doc, entry, "id", ids[i]);
                    const char *sn = resolve_name(&c, ids[i]);
                    if (sn && sn[0]) {
                        nmo_cli_json_add_str_safe(doc, entry, "name", sn);
                    }
                    yyjson_mut_arr_add_val(scene_arr, entry);
                }
                yyjson_mut_obj_add_val(doc, data, "scenes", scene_arr);
            } else {
                yyjson_mut_obj_add_null(doc, data, "state");
            }

            nmo_cmd_ctx_json_end(&c, doc, data, "scene.show");
        } else {
            nmo_cli_print_heading(c.out, "Level Details", c.colorize);

            char buf[128];
            snprintf(buf, sizeof(buf), "#%u (%s)", obj_id,
                     (name && name[0]) ? name : "(unnamed)");
            nmo_cli_print_kv(c.out, "ID / Name", buf, 20, c.colorize);

            snprintf(buf, sizeof(buf), "%s", class_name ? class_name : "-");
            nmo_cli_print_kv(c.out, "Class", buf, 20, c.colorize);

            if (!ls) {
                fprintf(c.out, "\n  (no deserialized state)\n");
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
            }

            if (ls->current_scene_id) {
                const char *sn = resolve_name(&c, ls->current_scene_id);
                if (sn && sn[0]) {
                    snprintf(buf, sizeof(buf), "#%u (%s)",
                             ls->current_scene_id, sn);
                } else {
                    snprintf(buf, sizeof(buf), "#%u", ls->current_scene_id);
                }
            } else {
                snprintf(buf, sizeof(buf), "(none)");
            }
            nmo_cli_print_kv(c.out, "Current Scene", buf, 20, c.colorize);

            if (ls->level_scene_id) {
                const char *sn = resolve_name(&c, ls->level_scene_id);
                if (sn && sn[0]) {
                    snprintf(buf, sizeof(buf), "#%u (%s)",
                             ls->level_scene_id, sn);
                } else {
                    snprintf(buf, sizeof(buf), "#%u", ls->level_scene_id);
                }
            } else {
                snprintf(buf, sizeof(buf), "(none)");
            }
            nmo_cli_print_kv(c.out, "Level Scene", buf, 20, c.colorize);

            /* Scene list */
            fprintf(c.out, "\nScenes (%zu):\n", ls->scene_ids.count);
            const nmo_object_id_t *ids =
                (const nmo_object_id_t *)ls->scene_ids.data;
            for (size_t i = 0; i < ls->scene_ids.count; ++i) {
                const char *sn = resolve_name(&c, ids[i]);
                if (sn && sn[0]) {
                    fprintf(c.out, "  [%zu] #%u (%s)\n", i, ids[i], sn);
                } else {
                    fprintf(c.out, "  [%zu] #%u\n", i, ids[i]);
                }
            }
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * scene set - Set scene properties
 * ============================================================================ */

typedef struct scene_set_args {
    nmo_core_object_selector_t selector;
    nmo_object_id_t object_id;
    nmo_field_set_entry_t entries[4];
    size_t entry_count;
} scene_set_args_t;

static int scene_set_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    scene_set_args_t *args = (scene_set_args_t *)user_data;
    if (c == NULL || args == NULL || args->entry_count == 0) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_object_t *obj = NULL;
    nmo_object_id_t object_id = 0;
    int rc = nmo_core_resolve_one_object(c, &args->selector, &obj, &object_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo scene set [--id <id> | --name <name> | <id>] [options] <file> -o <output>\n");
        return rc;
    }
    args->object_id = object_id;

    fprintf(c->out, "Scene #%u:\n", args->object_id);

    nmo_field_set_result_t result;
    return nmo_core_set_fields(
        c,
        args->object_id,
        args->entries,
        args->entry_count,
        dry_run,
        &result);
}

static int scene_set_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)user_data;
    if (c == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!dry_run && output_path != NULL) {
        fprintf(c->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_scene_set(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output",    "-o", NMO_OPT_STRING, "Output file"},
        {"--bg-color",  NULL, NMO_OPT_STRING, "Background color (ARGB hex)"},
        {"--ambient",   NULL, NMO_OPT_STRING, "Ambient light color (ARGB hex)"},
        {"--fog-color", NULL, NMO_OPT_STRING, "Fog color (ARGB hex)"},
        {"--camera",    NULL, NMO_OPT_STRING, "Starting camera ID"},
        {"--dry-run",   NULL, NMO_OPT_FLAG,   "Preview without saving"},
        {"--id",        NULL, NMO_OPT_UINT,   "Scene object ID"},
        {"--name",      "-n", NMO_OPT_STRING, "Scene object name"},
    };
    enum { OPT_OUTPUT, OPT_BG, OPT_AMBIENT, OPT_FOG, OPT_CAMERA,
           OPT_DRYRUN, OPT_ID, OPT_NAME, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = NULL;
    const char *file_path = NULL;
    if (has_selector_opt) {
        if (r.pos_count >= 1) {
            file_path = r.pos_args[r.pos_count - 1];
        }
    } else if (r.pos_count >= 2) {
        positional_id = r.pos_args[0];
        file_path = r.pos_args[r.pos_count - 1];
    }
    if (file_path == NULL) {
        fprintf(stderr, "Usage: nmo scene set [--id <id> | --name <name> | <id>] [options] <file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Build field entries */
    scene_set_args_t args = {
        .selector = {
            .has_id = vals[OPT_ID].present,
            .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
            .positional_id = positional_id,
            .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .required_base_class = NMO_CID_SCENE,
            .selector_label = "Scene",
            .type_label = "CKScene",
        },
        .entry_count = 0,
    };

    if (vals[OPT_BG].present)
        args.entries[args.entry_count++] =
            (nmo_field_set_entry_t){"background_color", vals[OPT_BG].val.str};
    if (vals[OPT_AMBIENT].present)
        args.entries[args.entry_count++] =
            (nmo_field_set_entry_t){"ambient_light_color", vals[OPT_AMBIENT].val.str};
    if (vals[OPT_FOG].present)
        args.entries[args.entry_count++] =
            (nmo_field_set_entry_t){"fog_color", vals[OPT_FOG].val.str};
    if (vals[OPT_CAMERA].present)
        args.entries[args.entry_count++] =
            (nmo_field_set_entry_t){"starting_camera_id", vals[OPT_CAMERA].val.str};

    if (args.entry_count == 0) {
        fprintf(stderr, "Error: No scene properties specified. Use --bg-color, --ambient, --fog-color, or --camera\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const nmo_cli_write_spec_t spec = {
        .command_name = "scene.set",
        .output_required_unless_dry_run = true,
    };
    return nmo_cli_run_write_command(
        file_path,
        output,
        dry_run,
        global,
        &spec,
        scene_set_mutate,
        scene_set_report,
        &args);
}
