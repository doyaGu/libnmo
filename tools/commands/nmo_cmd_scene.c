/**
 * @file nmo_cmd_scene.c
 * @brief CLI scene/level command group implementation
 */

#include "nmo_cmd_scene.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_record.h"
#include "../nmo_cli_write.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/builtin/nmo_level_schemas.h"

#include <stdio.h>
#include <string.h>

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

typedef struct scene_list_data {
    /* JSON sink (when doc is non-NULL) */
    yyjson_mut_doc *doc;
    yyjson_mut_val *arr;
    /* Text sink */
    nmo_cli_table_t *table;
    uint32_t found;
} scene_list_data_t;

static bool scene_list_query_predicate(const nmo_object_t *object, void *user_data) {
    (void)user_data;
    if (object == NULL) {
        return false;
    }

    nmo_class_id_t cid = nmo_object_get_class_id(object);
    return cid == NMO_CID_SCENE || cid == NMO_CID_LEVEL;
}

/* One description of a scene/level row, rendered as JSON or as table cells. */
static bool scene_list_build_record(const nmo_cmd_ctx_t *c,
                                    nmo_object_t *obj,
                                    nmo_cli_record_t *rec)
{
    nmo_object_id_t id = nmo_object_get_id(obj);
    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    const char *class_name = nmo_core_class_name(c, cid);
    const char *name = nmo_object_get_name(obj);

    bool ok = nmo_cli_record_uint(rec, "id", "ID", id);
    ok = ok && nmo_cli_record_str_opt(rec, "class", "CLASS", class_name, "-");
    ok = ok && nmo_cli_record_str(rec, "name", "NAME", name);
    if (ok && (!name || !name[0])) {
        ok = nmo_cli_record_set_text(rec, "-");
    }

    if (cid == NMO_CID_SCENE) {
        const nmo_scene_state_t *ss =
            (const nmo_scene_state_t *)nmo_object_get_state(obj);
        if (ss) {
            ok = ok && nmo_cli_record_uint(rec, "object_count", "OBJECTS",
                                           (uint64_t)ss->object_descs.count);
            const nmo_object_id_t starting_camera_id =
                nmo_ref_runtime_id(&ss->starting_camera);
            ok = ok && nmo_cli_record_ref_opt(
                rec, "starting_camera_id", "starting_camera", "CAMERA",
                starting_camera_id, resolve_name(c, starting_camera_id), "-");
        } else {
            ok = ok && nmo_cli_record_text(rec, "OBJECTS", "-");
            ok = ok && nmo_cli_record_text(rec, "CAMERA", "-");
        }
    } else {
        const nmo_level_state_t *ls =
            (const nmo_level_state_t *)nmo_object_get_state(obj);
        if (ls) {
            ok = ok && nmo_cli_record_uint(rec, "scene_count", "OBJECTS",
                                           (uint64_t)ls->scene_ids.count);
        } else {
            ok = ok && nmo_cli_record_text(rec, "OBJECTS", "-");
        }
        ok = ok && nmo_cli_record_text(rec, "CAMERA", "-");
    }
    return ok;
}

static int scene_list_visitor(size_t index,
                              nmo_object_t *obj,
                              const nmo_cmd_ctx_t *c,
                              void *user)
{
    (void)index;
    scene_list_data_t *data = (scene_list_data_t *)user;
    if (obj == NULL || data == NULL) {
        return 0;
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec || !scene_list_build_record(c, obj, rec)) {
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

static int scene_list_run(nmo_cmd_ctx_t *c) {
    nmo_object_query_t query = {
        .predicate = scene_list_query_predicate,
    };

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        scene_list_data_t ld = { .doc = doc, .arr = arr };
        int rc = nmo_core_object_query_run(c, &query,
                                           scene_list_visitor, &ld, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return rc;
        }

        yyjson_mut_obj_add_uint(doc, data, "count", ld.found);
        yyjson_mut_obj_add_val(doc, data, "scenes", arr);
        nmo_cmd_ctx_json_end(c, doc, data, "scene.list");
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
        scene_list_data_t ld = { .table = &table };
        int rc = nmo_core_object_query_run(c, &query,
                                           scene_list_visitor, &ld, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_cli_table_free(&table);
            return rc;
        }

        fprintf(c->out, "Scenes/Levels: %u\n\n", ld.found);
        nmo_cli_table_print(&table, c->out, c->colorize);
        nmo_cli_table_free(&table);
    }

    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * scene list
 * ============================================================================ */

int nmo_cmd_scene_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    rc = scene_list_run(&c);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * scene show
 * ============================================================================ */

typedef struct scene_show_args {
    nmo_core_object_selector_t selector;
} scene_show_args_t;

static int scene_show_parse(int argc,
                            char **argv,
                            bool in_session,
                            scene_show_args_t *args) {
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
    static const nmo_class_id_t scene_classes[] = {
        NMO_CID_SCENE,
        NMO_CID_LEVEL,
    };
    const char *positional_id =
        (!has_selector_opt && r.pos_count >= (in_session ? 1u : 2u)) ? r.pos_args[0] : NULL;
    if (in_session && !has_selector_opt && r.pos_count != 1) {
        fprintf(stderr, "Usage: scene show [--id <id> | --name <name> | <id>]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    *args = (scene_show_args_t){
        .selector = {
            .has_id = vals[OPT_ID].present,
            .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
            .positional_id = positional_id,
            .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .allowed_class_ids = scene_classes,
            .allowed_class_count = sizeof(scene_classes) / sizeof(scene_classes[0]),
            .selector_label = "Scene",
            .type_label = "CKScene or CKLevel",
        },
    };
    return NMO_CLI_EXIT_SUCCESS;
}

/* Fields shared by scene and level detail views. */
static bool scene_show_build_header(const nmo_cmd_ctx_t *c,
                                    nmo_cli_record_t *rec,
                                    nmo_object_id_t obj_id,
                                    const char *name,
                                    const char *class_name)
{
    (void)c;
    bool ok = nmo_cli_record_uint(rec, "id", NULL, obj_id);
    ok = ok && nmo_cli_record_str(rec, "name", NULL, name);
    ok = ok && nmo_cli_record_text_fmt(rec, "ID / Name", "#%u (%s)", obj_id,
                                       (name && name[0]) ? name : "(unnamed)");
    ok = ok && nmo_cli_record_str_opt(rec, "class", "Class", class_name, "-");
    return ok;
}

static bool scene_show_build_scene(const nmo_cmd_ctx_t *c,
                                   nmo_cli_record_t *rec,
                                   const nmo_scene_state_t *ss)
{
    bool ok = nmo_cli_record_hex32(rec, "background_color", "Background Color",
                                   ss->background_color);
    ok = ok && nmo_cli_record_hex32(rec, "ambient_light_color", "Ambient Light",
                                    ss->ambient_light_color);
    ok = ok && nmo_cli_record_str(rec, "fog_mode", "Fog Mode",
                                  fog_mode_str(ss->fog_mode));
    ok = ok && nmo_cli_record_hex32(rec, "fog_color", "Fog Color", ss->fog_color);
    ok = ok && nmo_cli_record_real(rec, "fog_start", "Fog Start",
                                   (double)ss->fog_start, "%.3f");
    ok = ok && nmo_cli_record_real(rec, "fog_end", "Fog End",
                                   (double)ss->fog_end, "%.3f");
    ok = ok && nmo_cli_record_real(rec, "fog_density", "Fog Density",
                                   (double)ss->fog_density, "%.6g");

    const nmo_object_id_t starting_camera_id =
        nmo_ref_runtime_id(&ss->starting_camera);
    ok = ok && nmo_cli_record_ref(rec, "starting_camera_id", "starting_camera",
                                  "Starting Camera", starting_camera_id,
                                  resolve_name(c, starting_camera_id), "(none)");

    ok = ok && nmo_cli_record_uint(rec, "environment_settings", "Env Settings",
                                   ss->environment_settings);
    ok = ok && nmo_cli_record_set_text_fmt(rec, "0x%08X", ss->environment_settings);
    ok = ok && nmo_cli_record_uint(rec, "object_count", "Objects",
                                   (uint64_t)ss->object_descs.count);
    return ok;
}

static bool scene_show_build_level(const nmo_cmd_ctx_t *c,
                                   nmo_cli_record_t *rec,
                                   const nmo_level_state_t *ls)
{
    const nmo_object_id_t current_scene_id =
        nmo_ref_runtime_id(&ls->current_scene);
    bool ok = nmo_cli_record_ref(rec, "current_scene_id", "current_scene",
                                 "Current Scene", current_scene_id,
                                 resolve_name(c, current_scene_id), "(none)");

    const nmo_object_id_t level_scene_id =
        nmo_ref_runtime_id(&ls->level_scene);
    ok = ok && nmo_cli_record_ref(rec, "level_scene_id", "level_scene",
                                  "Level Scene", level_scene_id,
                                  resolve_name(c, level_scene_id), "(none)");

    nmo_cli_record_array_t *scenes = nmo_cli_record_array(rec, "scenes", "Scenes");
    ok = ok && scenes != NULL;
    const nmo_ref_t *refs = NMO_ARRAY_DATA(nmo_ref_t, &ls->scene_ids);
    for (size_t i = 0; ok && i < ls->scene_ids.count; ++i) {
        const nmo_object_id_t id = nmo_ref_runtime_id(&refs[i]);
        if (id == NMO_OBJECT_ID_NONE) continue;
        nmo_cli_record_t *entry = nmo_cli_record_new();
        const char *sn = resolve_name(c, id);
        ok = entry != NULL &&
             nmo_cli_record_uint(entry, "id", NULL, id) &&
             nmo_cli_record_str_opt(entry, "name", NULL, sn, NULL) &&
             ((sn && sn[0])
                  ? nmo_cli_record_set_summary_fmt(entry, "  [%zu] #%u (%s)", i, id, sn)
                  : nmo_cli_record_set_summary_fmt(entry, "  [%zu] #%u", i, id)) &&
             nmo_cli_record_array_add(scenes, entry);
    }
    return ok;
}

static int scene_show_run(nmo_cmd_ctx_t *c, const scene_show_args_t *args) {
    nmo_object_t *obj = NULL;
    nmo_object_id_t obj_id = 0;
    int rc = nmo_core_resolve_one_object(c, &args->selector, &obj, &obj_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo scene show [--id <id> | --name <name> | <id>] <file>\n");
        return rc;
    }

    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    if (class_id != NMO_CID_SCENE && class_id != NMO_CID_LEVEL) {
        fprintf(stderr, "Error: Object %u is not a CKScene or CKLevel (class %u)\n",
                obj_id, class_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *name = nmo_object_get_name(obj);
    const char *class_name = nmo_core_class_name(c, class_id);
    const void *state = nmo_object_get_state(obj);
    const bool is_scene = class_id == NMO_CID_SCENE;

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec && scene_show_build_header(c, rec, obj_id, name, class_name);
    if (ok) {
        if (!state) {
            ok = nmo_cli_record_null(rec, "state", NULL, NULL);
        } else if (is_scene) {
            ok = scene_show_build_scene(c, rec, (const nmo_scene_state_t *)state);
        } else {
            ok = scene_show_build_level(c, rec, (const nmo_level_state_t *)state);
        }
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        fprintf(stderr, "Error: Out of memory while describing object %u\n", obj_id);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_record_to_json(rec, doc, data);
        nmo_cli_record_free(rec);
        return nmo_cmd_ctx_json_end(c, doc, data, "scene.show");
    }

    nmo_cli_print_heading(c->out, is_scene ? "Scene Details" : "Level Details",
                          c->colorize);
    nmo_cli_record_print_kv(rec, c->out, 20, c->colorize);
    if (!state) {
        fprintf(c->out, "\n  (no deserialized state)\n");
    }
    nmo_cli_record_free(rec);
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_scene_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    scene_show_args_t args;
    int rc = scene_show_parse(argc, argv, false, &args);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    rc = scene_show_run(&c, &args);
    return nmo_cmd_ctx_done(&c, rc);
}

int nmo_cmd_scene_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: scene list|show ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "list") == 0 || strcmp(argv[0], "ls") == 0) {
        return scene_list_run(ctx);
    }
    if (strcmp(argv[0], "show") == 0 || strcmp(argv[0], "s") == 0) {
        scene_show_args_t args;
        int rc = scene_show_parse(argc, argv, true, &args);
        if (rc != NMO_CLI_EXIT_SUCCESS) return rc;
        return scene_show_run(ctx, &args);
    }

    fprintf(stderr, "Unsupported scene read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
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
            (nmo_field_set_entry_t){"starting_camera", vals[OPT_CAMERA].val.str};

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
