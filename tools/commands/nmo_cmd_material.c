/**
 * @file nmo_cmd_material.c
 * @brief CLI material command group implementation
 */

#include "nmo_cmd_material.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_record.h"
#include "../nmo_cli_write.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_material_schemas.h"

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

static uint32_t count_texture_refs(const nmo_material_state_t *state) {
    uint32_t n = 0;
    for (size_t i = 0; i < 4; ++i) {
        if (nmo_material_texture_id(state, i) != 0) ++n;
    }
    return n;
}

typedef struct material_list_data {
    yyjson_mut_doc *doc;    /* JSON sink when non-NULL */
    yyjson_mut_val *arr;
    nmo_cli_table_t *table; /* text sink otherwise */
    uint32_t found;
} material_list_data_t;

static bool material_list_build_record(nmo_object_t *obj, nmo_cli_record_t *rec)
{
    const char *name = nmo_object_get_name(obj);
    bool ok = nmo_cli_record_uint(rec, "id", "ID", nmo_object_get_id(obj));
    ok = ok && nmo_cli_record_str(rec, "name", "NAME", name);
    if (ok && (!name || !name[0])) {
        ok = nmo_cli_record_set_text(rec, "-");
    }

    const nmo_material_state_t *ms =
        (const nmo_material_state_t *)nmo_object_get_state(obj);
    if (ms) {
        ok = ok && nmo_cli_record_hex32(rec, "diffuse", "DIFFUSE", ms->diffuse_color);
        ok = ok && nmo_cli_record_uint(rec, "texture_count", "TEXTURES",
                                       count_texture_refs(ms));
    } else {
        ok = ok && nmo_cli_record_text(rec, "DIFFUSE", "-");
        ok = ok && nmo_cli_record_text(rec, "TEXTURES", "-");
    }
    return ok;
}

static int material_list_visitor(size_t index,
                                 nmo_object_t *obj,
                                 const nmo_cmd_ctx_t *c,
                                 void *user)
{
    (void)index;
    (void)c;
    material_list_data_t *data = (material_list_data_t *)user;
    if (obj == NULL || data == NULL) {
        return 0;
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec || !material_list_build_record(obj, rec)) {
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

static int material_list_run(nmo_cmd_ctx_t *c) {
    nmo_object_query_t query = {0};
    nmo_core_query_set_class_id(&query, NMO_CID_MATERIAL, false);

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        material_list_data_t ld = { .doc = doc, .arr = arr };
        int rc = nmo_core_object_query_run(c, &query,
                                           material_list_visitor, &ld, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return rc;
        }

        yyjson_mut_obj_add_uint(doc, data, "count", ld.found);
        yyjson_mut_obj_add_val(doc, data, "materials", arr);
        nmo_cmd_ctx_json_end(c, doc, data, "material.list");
    } else {
        static const nmo_cli_table_col_t columns[] = {
            {"ID",       NMO_CLI_ALIGN_RIGHT, 6,  0},
            {"NAME",     NMO_CLI_ALIGN_LEFT,  24, 50},
            {"DIFFUSE",  NMO_CLI_ALIGN_LEFT,  12, 0},
            {"TEXTURES", NMO_CLI_ALIGN_RIGHT, 8,  0},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));
        material_list_data_t ld = { .table = &table };
        int rc = nmo_core_object_query_run(c, &query,
                                           material_list_visitor, &ld, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_cli_table_free(&table);
            return rc;
        }

        fprintf(c->out, "Materials: %u\n\n", ld.found);
        nmo_cli_table_print(&table, c->out, c->colorize);
        nmo_cli_table_free(&table);
    }

    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * material list
 * ============================================================================ */

int nmo_cmd_material_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    rc = material_list_run(&c);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * material show
 * ============================================================================ */

typedef struct material_show_args {
    nmo_core_object_selector_t selector;
} material_show_args_t;

static int material_show_parse(int argc,
                               char **argv,
                               bool in_session,
                               material_show_args_t *args) {
    static const nmo_opt_def_t opts[] = {
        {"--id",   "-i", NMO_OPT_UINT,   "Material object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Material object name"},
    };
    enum { OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id =
        (!has_selector_opt && r.pos_count >= (in_session ? 1u : 2u)) ? r.pos_args[0] : NULL;
    if (in_session && !has_selector_opt && r.pos_count != 1) {
        fprintf(stderr, "Usage: material show [--id <id> | --name <name> | <id>]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    *args = (material_show_args_t){
        .selector = {
            .has_id = vals[OPT_ID].present,
            .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
            .positional_id = positional_id,
            .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .required_base_class = NMO_CID_MATERIAL,
            .selector_label = "Material",
            .type_label = "CKMaterial",
        },
    };
    return NMO_CLI_EXIT_SUCCESS;
}

/* Color as "0x%08X" in JSON and "0x%08X  (r, g, b, a)" in text. */
static bool material_record_color(nmo_cli_record_t *rec, const char *key,
                                  const char *label, uint32_t argb)
{
    unsigned a = (argb >> 24) & 0xFFu;
    unsigned r = (argb >> 16) & 0xFFu;
    unsigned g = (argb >> 8) & 0xFFu;
    unsigned b = argb & 0xFFu;
    return nmo_cli_record_hex32(rec, key, label, argb) &&
           nmo_cli_record_set_text_fmt(rec, "0x%08X  (%u, %u, %u, %u)", argb, r, g, b, a);
}

static bool material_show_build_record(const nmo_cmd_ctx_t *c,
                                       nmo_cli_record_t *rec,
                                       nmo_object_id_t obj_id,
                                       const char *name,
                                       const nmo_material_state_t *ms)
{
    bool ok = nmo_cli_record_uint(rec, "id", NULL, obj_id);
    ok = ok && nmo_cli_record_str(rec, "name", NULL, name);
    ok = ok && nmo_cli_record_text_fmt(rec, "ID / Name", "#%u (%s)", obj_id,
                                       (name && name[0]) ? name : "(unnamed)");
    if (!ok) {
        return false;
    }
    if (!ms) {
        return nmo_cli_record_null(rec, "state", NULL, NULL);
    }

    ok = material_record_color(rec, "diffuse_color", "Diffuse", ms->diffuse_color);
    ok = ok && material_record_color(rec, "ambient_color", "Ambient", ms->ambient_color);
    ok = ok && material_record_color(rec, "specular_color", "Specular", ms->specular_color);
    ok = ok && material_record_color(rec, "emissive_color", "Emissive", ms->emissive_color);
    ok = ok && nmo_cli_record_real(rec, "specular_power", "Specular Power",
                                   (double)ms->specular_power, "%.4f");

    nmo_cli_record_array_t *textures = nmo_cli_record_array(rec, "textures", NULL);
    ok = ok && textures != NULL &&
         nmo_cli_record_array_set_heading(textures, "Textures:") &&
         nmo_cli_record_array_set_empty_text(textures, "  (none)");
    for (int ti = 0; ok && ti < 4; ++ti) {
        const nmo_object_id_t texture_id = nmo_material_texture_id(ms, (size_t)ti);
        if (!texture_id) {
            continue;
        }
        const char *tn = resolve_name(c, texture_id);
        nmo_cli_record_t *entry = nmo_cli_record_new();
        ok = entry != NULL &&
             nmo_cli_record_uint(entry, "slot", NULL, (uint64_t)ti) &&
             nmo_cli_record_ref(entry, "id", "name", NULL, texture_id, tn, NULL) &&
             ((tn && tn[0])
                  ? nmo_cli_record_set_summary_fmt(entry, "  [%d] #%u (%s)", ti, texture_id, tn)
                  : nmo_cli_record_set_summary_fmt(entry, "  [%d] #%u", ti, texture_id)) &&
             nmo_cli_record_array_add(textures, entry);
    }

    ok = ok && nmo_cli_record_hex32(rec, "packed_modes", "Packed Modes", ms->packed_modes);
    ok = ok && nmo_cli_record_hex32(rec, "packed_flags", "Packed Flags", ms->packed_flags);
    if (ms->has_effect) {
        ok = ok && nmo_cli_record_uint(rec, "effect", "Effect", ms->effect);
    }
    return ok;
}

static int material_show_run(nmo_cmd_ctx_t *c, const material_show_args_t *args) {
    nmo_object_t *obj = NULL;
    nmo_object_id_t obj_id = 0;
    int rc = nmo_core_resolve_one_object(c, &args->selector, &obj, &obj_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo material show [--id <id> | --name <name> | <id>] <file>\n");
        return rc;
    }

    const char *name = nmo_object_get_name(obj);
    const nmo_material_state_t *ms =
        (const nmo_material_state_t *)nmo_object_get_state(obj);

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec || !material_show_build_record(c, rec, obj_id, name, ms)) {
        nmo_cli_record_free(rec);
        fprintf(stderr, "Error: Out of memory while describing material %u\n", obj_id);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_record_to_json(rec, doc, data);
        nmo_cli_record_free(rec);
        return nmo_cmd_ctx_json_end(c, doc, data, "material.show");
    }

    nmo_cli_print_heading(c->out, "Material Details", c->colorize);
    nmo_cli_record_print_kv(rec, c->out, 20, c->colorize);
    if (!ms) {
        fprintf(c->out, "\n  (no deserialized state)\n");
    }
    nmo_cli_record_free(rec);
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_material_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    material_show_args_t args;
    int rc = material_show_parse(argc, argv, false, &args);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    rc = material_show_run(&c, &args);
    return nmo_cmd_ctx_done(&c, rc);
}

int nmo_cmd_material_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: material list|show ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "list") == 0 || strcmp(argv[0], "ls") == 0) {
        return material_list_run(ctx);
    }
    if (strcmp(argv[0], "show") == 0 || strcmp(argv[0], "s") == 0) {
        material_show_args_t args;
        int rc = material_show_parse(argc, argv, true, &args);
        if (rc != NMO_CLI_EXIT_SUCCESS) return rc;
        return material_show_run(ctx, &args);
    }

    fprintf(stderr, "Unsupported material read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}

/* ============================================================================
 * material set
 * ============================================================================ */

typedef struct material_set_args {
    nmo_core_object_selector_t selector;
    nmo_object_id_t object_id;
    nmo_field_set_entry_t entries[5];
    size_t entry_count;
} material_set_args_t;

static int material_set_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    material_set_args_t *args = (material_set_args_t *)user_data;
    if (c == NULL || args == NULL || args->entry_count == 0) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_object_t *obj = NULL;
    nmo_object_id_t object_id = 0;
    int rc = nmo_core_resolve_one_object(c, &args->selector, &obj, &object_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo material set [--id <id> | --name <name> | <id>] [options] <file> -o <output>\n");
        return rc;
    }
    args->object_id = object_id;

    fprintf(c->out, "Material #%u:\n", args->object_id);

    nmo_field_set_result_t result;
    return nmo_core_set_fields(
        c,
        args->object_id,
        args->entries,
        args->entry_count,
        dry_run,
        &result);
}

static int material_set_report(
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

int nmo_cmd_material_set(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output",   "-o", NMO_OPT_STRING, "Output file"},
        {"--diffuse",  NULL, NMO_OPT_STRING, "Diffuse color (ARGB hex)"},
        {"--ambient",  NULL, NMO_OPT_STRING, "Ambient color (ARGB hex)"},
        {"--specular", NULL, NMO_OPT_STRING, "Specular color (ARGB hex)"},
        {"--emissive", NULL, NMO_OPT_STRING, "Emissive color (ARGB hex)"},
        {"--power",    NULL, NMO_OPT_STRING, "Specular power (float)"},
        {"--dry-run",  NULL, NMO_OPT_FLAG,   "Preview without saving"},
        {"--id",       NULL, NMO_OPT_UINT,   "Material object ID"},
        {"--name",     "-n", NMO_OPT_STRING, "Material object name"},
    };
    enum { OPT_OUTPUT, OPT_DIFFUSE, OPT_AMBIENT, OPT_SPECULAR,
           OPT_EMISSIVE, OPT_POWER, OPT_DRYRUN, OPT_ID, OPT_NAME, OPT_COUNT };

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
        fprintf(stderr, "Usage: nmo material set [--id <id> | --name <name> | <id>] [options] <file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Build field entries from provided options */
    material_set_args_t args = {
        .selector = {
            .has_id = vals[OPT_ID].present,
            .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
            .positional_id = positional_id,
            .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .required_base_class = NMO_CID_MATERIAL,
            .selector_label = "Material",
            .type_label = "CKMaterial",
        },
        .entry_count = 0,
    };

    if (vals[OPT_DIFFUSE].present)
        args.entries[args.entry_count++] =
            (nmo_field_set_entry_t){"diffuse_color", vals[OPT_DIFFUSE].val.str};
    if (vals[OPT_AMBIENT].present)
        args.entries[args.entry_count++] =
            (nmo_field_set_entry_t){"ambient_color", vals[OPT_AMBIENT].val.str};
    if (vals[OPT_SPECULAR].present)
        args.entries[args.entry_count++] =
            (nmo_field_set_entry_t){"specular_color", vals[OPT_SPECULAR].val.str};
    if (vals[OPT_EMISSIVE].present)
        args.entries[args.entry_count++] =
            (nmo_field_set_entry_t){"emissive_color", vals[OPT_EMISSIVE].val.str};
    if (vals[OPT_POWER].present)
        args.entries[args.entry_count++] =
            (nmo_field_set_entry_t){"specular_power", vals[OPT_POWER].val.str};

    if (args.entry_count == 0) {
        fprintf(stderr, "Error: No properties specified. Use --diffuse, --ambient, --specular, --emissive, or --power\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const nmo_cli_write_spec_t spec = {
        .command_name = "material.set",
        .output_required_unless_dry_run = true,
    };
    return nmo_cli_run_write_command(
        file_path,
        output,
        dry_run,
        global,
        &spec,
        material_set_mutate,
        material_set_report,
        &args);
}
