/**
 * @file nmo_cmd_material.c
 * @brief CLI material command group implementation
 */

#include "nmo_cmd_material.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
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

static void format_argb(char *buf, size_t buf_size, uint32_t color) {
    snprintf(buf, buf_size, "0x%08X", color);
}

static void format_color_components(char *buf, size_t buf_size, uint32_t argb) {
    uint8_t a = (uint8_t)((argb >> 24) & 0xFF);
    uint8_t r_c = (uint8_t)((argb >> 16) & 0xFF);
    uint8_t g = (uint8_t)((argb >> 8) & 0xFF);
    uint8_t b = (uint8_t)(argb & 0xFF);
    snprintf(buf, buf_size, "(%u, %u, %u, %u)", r_c, g, b, a);
}

static uint32_t count_texture_refs(const nmo_object_id_t *ids, size_t count) {
    uint32_t n = 0;
    for (size_t i = 0; i < count; ++i) {
        if (ids[i] != 0) ++n;
    }
    return n;
}

typedef struct material_list_json_data {
    yyjson_mut_doc *doc;
    yyjson_mut_val *arr;
    uint32_t found;
} material_list_json_data_t;

typedef struct material_list_table_data {
    nmo_cli_table_t *table;
    uint32_t found;
} material_list_table_data_t;

static int material_list_json_visitor(size_t index,
                                      nmo_object_t *obj,
                                      const nmo_cmd_ctx_t *c,
                                      void *user)
{
    (void)index;
    (void)c;
    material_list_json_data_t *data = (material_list_json_data_t *)user;
    if (obj == NULL || data == NULL || data->doc == NULL || data->arr == NULL) {
        return 0;
    }

    yyjson_mut_doc *doc = data->doc;
    yyjson_mut_val *item = yyjson_mut_obj(doc);
    nmo_object_id_t id = nmo_object_get_id(obj);
    yyjson_mut_obj_add_uint(doc, item, "id", id);

    const char *name = nmo_object_get_name(obj);
    nmo_cli_json_add_str_safe(doc, item, "name",
                              (name && name[0]) ? name : "");

    const nmo_material_state_t *ms =
        (const nmo_material_state_t *)nmo_object_get_state(obj);
    if (ms) {
        char color_buf[16];
        format_argb(color_buf, sizeof(color_buf), ms->diffuse_color);
        yyjson_mut_obj_add_strcpy(doc, item, "diffuse", color_buf);
        yyjson_mut_obj_add_uint(doc, item, "texture_count",
                                count_texture_refs(ms->texture_ids, 4));
    }

    yyjson_mut_arr_add_val(data->arr, item);
    data->found++;
    return 0;
}

static int material_list_table_visitor(size_t index,
                                       nmo_object_t *obj,
                                       const nmo_cmd_ctx_t *c,
                                       void *user)
{
    (void)index;
    (void)c;
    material_list_table_data_t *data = (material_list_table_data_t *)user;
    if (obj == NULL || data == NULL || data->table == NULL) {
        return 0;
    }

    char id_buf[16];
    snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

    const char *name = nmo_object_get_name(obj);
    if (!name || !name[0]) name = "-";

    char diffuse_buf[16];
    char tex_buf[16];

    const nmo_material_state_t *ms =
        (const nmo_material_state_t *)nmo_object_get_state(obj);
    if (ms) {
        format_argb(diffuse_buf, sizeof(diffuse_buf), ms->diffuse_color);
        snprintf(tex_buf, sizeof(tex_buf), "%u",
                 count_texture_refs(ms->texture_ids, 4));
    } else {
        snprintf(diffuse_buf, sizeof(diffuse_buf), "-");
        snprintf(tex_buf, sizeof(tex_buf), "-");
    }

    const char *cells[] = {id_buf, name, diffuse_buf, tex_buf};
    nmo_cli_table_add_row(data->table, cells, 4);
    data->found++;
    return 0;
}

/* ============================================================================
 * material list
 * ============================================================================ */

int nmo_cmd_material_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_query_t query = {0};
    nmo_core_query_set_class_id(&query, NMO_CID_MATERIAL, false);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        material_list_json_data_t jd = { .doc = doc, .arr = arr };
        rc = nmo_core_object_query_run(&c, &query,
                                       material_list_json_visitor, &jd, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return nmo_cmd_ctx_done(&c, rc);
        }

        yyjson_mut_obj_add_uint(doc, data, "count", jd.found);
        yyjson_mut_obj_add_val(doc, data, "materials", arr);
        nmo_cmd_ctx_json_end(&c, doc, data, "material.list");
    } else {
        static const nmo_cli_table_col_t columns[] = {
            {"ID",       NMO_CLI_ALIGN_RIGHT, 6,  0},
            {"NAME",     NMO_CLI_ALIGN_LEFT,  24, 50},
            {"DIFFUSE",  NMO_CLI_ALIGN_LEFT,  12, 0},
            {"TEXTURES", NMO_CLI_ALIGN_RIGHT, 8,  0},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));
        material_list_table_data_t td = { .table = &table };
        rc = nmo_core_object_query_run(&c, &query,
                                       material_list_table_visitor, &td, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_cli_table_free(&table);
            return nmo_cmd_ctx_done(&c, rc);
        }

        fprintf(c.out, "Materials: %u\n\n", td.found);
        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * material show
 * ============================================================================ */

int nmo_cmd_material_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--id", "-i", NMO_OPT_STRING, "Object ID"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *id_str = vals[0].present ? vals[0].val.str : NULL;
    if (!id_str && r.pos_count >= 2) {
        id_str = r.pos_args[0];
    }
    if (!id_str) {
        fprintf(stderr, "Error: No object ID specified\n");
        fprintf(stderr, "Usage: nmo material show <id> <file>\n");
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

    if (nmo_object_get_class_id(obj) != NMO_CID_MATERIAL) {
        fprintf(stderr, "Error: Object %u is not a CKMaterial\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const char *name = nmo_object_get_name(obj);
    const nmo_material_state_t *ms =
        (const nmo_material_state_t *)nmo_object_get_state(obj);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "id", obj_id);
        nmo_cli_json_add_str_safe(doc, data, "name",
                                  (name && name[0]) ? name : "");

        if (ms) {
            char color_buf[16];

            format_argb(color_buf, sizeof(color_buf), ms->diffuse_color);
            yyjson_mut_obj_add_strcpy(doc, data, "diffuse_color", color_buf);
            format_argb(color_buf, sizeof(color_buf), ms->ambient_color);
            yyjson_mut_obj_add_strcpy(doc, data, "ambient_color", color_buf);
            format_argb(color_buf, sizeof(color_buf), ms->specular_color);
            yyjson_mut_obj_add_strcpy(doc, data, "specular_color", color_buf);
            format_argb(color_buf, sizeof(color_buf), ms->emissive_color);
            yyjson_mut_obj_add_strcpy(doc, data, "emissive_color", color_buf);

            yyjson_mut_obj_add_real(doc, data, "specular_power",
                                   (double)ms->specular_power);

            /* Texture references */
            yyjson_mut_val *tex_arr = yyjson_mut_arr(doc);
            for (int ti = 0; ti < 4; ++ti) {
                if (ms->texture_ids[ti]) {
                    yyjson_mut_val *tref = yyjson_mut_obj(doc);
                    yyjson_mut_obj_add_uint(doc, tref, "slot", (uint64_t)ti);
                    yyjson_mut_obj_add_uint(doc, tref, "id", ms->texture_ids[ti]);
                    const char *tn = resolve_name(&c, ms->texture_ids[ti]);
                    if (tn && tn[0]) {
                        nmo_cli_json_add_str_safe(doc, tref, "name", tn);
                    }
                    yyjson_mut_arr_add_val(tex_arr, tref);
                }
            }
            yyjson_mut_obj_add_val(doc, data, "textures", tex_arr);

            /* Packed render settings */
            format_argb(color_buf, sizeof(color_buf), ms->packed_modes);
            yyjson_mut_obj_add_strcpy(doc, data, "packed_modes", color_buf);
            format_argb(color_buf, sizeof(color_buf), ms->packed_flags);
            yyjson_mut_obj_add_strcpy(doc, data, "packed_flags", color_buf);

            if (ms->has_effect) {
                yyjson_mut_obj_add_uint(doc, data, "effect", ms->effect);
            }
        } else {
            yyjson_mut_obj_add_null(doc, data, "state");
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "material.show");
    } else {
        nmo_cli_print_heading(c.out, "Material Details", c.colorize);

        char buf[128];
        snprintf(buf, sizeof(buf), "#%u (%s)", obj_id,
                 (name && name[0]) ? name : "(unnamed)");
        nmo_cli_print_kv(c.out, "ID / Name", buf, 20, c.colorize);

        if (!ms) {
            fprintf(c.out, "\n  (no deserialized state)\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
        }

        /* Colors */
        char hex_buf[16];
        char comp_buf[32];

        format_argb(hex_buf, sizeof(hex_buf), ms->diffuse_color);
        format_color_components(comp_buf, sizeof(comp_buf), ms->diffuse_color);
        snprintf(buf, sizeof(buf), "%s  %s", hex_buf, comp_buf);
        nmo_cli_print_kv(c.out, "Diffuse", buf, 20, c.colorize);

        format_argb(hex_buf, sizeof(hex_buf), ms->ambient_color);
        format_color_components(comp_buf, sizeof(comp_buf), ms->ambient_color);
        snprintf(buf, sizeof(buf), "%s  %s", hex_buf, comp_buf);
        nmo_cli_print_kv(c.out, "Ambient", buf, 20, c.colorize);

        format_argb(hex_buf, sizeof(hex_buf), ms->specular_color);
        format_color_components(comp_buf, sizeof(comp_buf), ms->specular_color);
        snprintf(buf, sizeof(buf), "%s  %s", hex_buf, comp_buf);
        nmo_cli_print_kv(c.out, "Specular", buf, 20, c.colorize);

        format_argb(hex_buf, sizeof(hex_buf), ms->emissive_color);
        format_color_components(comp_buf, sizeof(comp_buf), ms->emissive_color);
        snprintf(buf, sizeof(buf), "%s  %s", hex_buf, comp_buf);
        nmo_cli_print_kv(c.out, "Emissive", buf, 20, c.colorize);

        snprintf(buf, sizeof(buf), "%.4f", (double)ms->specular_power);
        nmo_cli_print_kv(c.out, "Specular Power", buf, 20, c.colorize);

        /* Texture references */
        fprintf(c.out, "\nTextures:\n");
        bool any_tex = false;
        for (int ti = 0; ti < 4; ++ti) {
            if (ms->texture_ids[ti]) {
                const char *tn = resolve_name(&c, ms->texture_ids[ti]);
                if (tn && tn[0]) {
                    fprintf(c.out, "  [%d] #%u (%s)\n", ti, ms->texture_ids[ti], tn);
                } else {
                    fprintf(c.out, "  [%d] #%u\n", ti, ms->texture_ids[ti]);
                }
                any_tex = true;
            }
        }
        if (!any_tex) {
            fprintf(c.out, "  (none)\n");
        }

        /* Packed render settings */
        snprintf(buf, sizeof(buf), "0x%08X", ms->packed_modes);
        nmo_cli_print_kv(c.out, "Packed Modes", buf, 20, c.colorize);

        snprintf(buf, sizeof(buf), "0x%08X", ms->packed_flags);
        nmo_cli_print_kv(c.out, "Packed Flags", buf, 20, c.colorize);

        if (ms->has_effect) {
            snprintf(buf, sizeof(buf), "%u", ms->effect);
            nmo_cli_print_kv(c.out, "Effect", buf, 20, c.colorize);
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * material set
 * ============================================================================ */

typedef struct material_set_args {
    uint32_t object_id;
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

    nmo_object_t *obj = nmo_core_find_by_id(c, args->object_id);
    if (!obj) {
        fprintf(stderr, "Error: Object #%u not found\n", args->object_id);
        return NMO_CLI_EXIT_NOT_FOUND;
    }
    if (nmo_object_get_class_id(obj) != NMO_CID_MATERIAL) {
        fprintf(stderr, "Error: Object #%u is not a CKMaterial\n", args->object_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

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
    };
    enum { OPT_OUTPUT, OPT_DIFFUSE, OPT_AMBIENT, OPT_SPECULAR,
           OPT_EMISSIVE, OPT_POWER, OPT_DRYRUN, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;

    if (r.pos_count < 2) {
        fprintf(stderr, "Usage: nmo material set <id> [options] <file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t object_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &object_id)) {
        fprintf(stderr, "Error: Invalid object ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    /* Build field entries from provided options */
    material_set_args_t args = {
        .object_id = object_id,
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
