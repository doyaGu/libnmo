/**
 * @file nmo_cmd_material.c
 * @brief CLI material command group implementation
 */

#include "nmo_cmd_material.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
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

/* ============================================================================
 * material list
 * ============================================================================ */

int nmo_cmd_material_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
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
            if (!obj || nmo_object_get_class_id(obj) != NMO_CID_MATERIAL) continue;

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            nmo_object_id_t id = nmo_object_get_id(obj);
            yyjson_mut_obj_add_uint(doc, item, "id", id);

            const char *name = nmo_object_get_name(obj);
            nmo_cli_json_add_str_safe(doc, item, "name",
                                      (name && name[0]) ? name : "");

            const nmo_material_state_t *ms =
                (const nmo_material_state_t *)nmo_object_get_data(obj);
            if (ms) {
                char color_buf[16];
                format_argb(color_buf, sizeof(color_buf), ms->diffuse_color);
                yyjson_mut_obj_add_str(doc, item, "diffuse", color_buf);
                yyjson_mut_obj_add_uint(doc, item, "texture_count",
                                        count_texture_refs(ms->texture_ids, 4));
            }

            yyjson_mut_arr_add_val(arr, item);
            found++;
        }

        yyjson_mut_obj_add_uint(doc, data, "count", found);
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
        uint32_t found = 0;

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            if (!obj || nmo_object_get_class_id(obj) != NMO_CID_MATERIAL) continue;

            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

            const char *name = nmo_object_get_name(obj);
            if (!name || !name[0]) name = "-";

            char diffuse_buf[16];
            char tex_buf[16];

            const nmo_material_state_t *ms =
                (const nmo_material_state_t *)nmo_object_get_data(obj);
            if (ms) {
                format_argb(diffuse_buf, sizeof(diffuse_buf), ms->diffuse_color);
                snprintf(tex_buf, sizeof(tex_buf), "%u",
                         count_texture_refs(ms->texture_ids, 4));
            } else {
                snprintf(diffuse_buf, sizeof(diffuse_buf), "-");
                snprintf(tex_buf, sizeof(tex_buf), "-");
            }

            const char *cells[] = {id_buf, name, diffuse_buf, tex_buf};
            nmo_cli_table_add_row(&table, cells, 4);
            found++;
        }

        fprintf(c.out, "Materials: %u\n\n", found);
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
        (const nmo_material_state_t *)nmo_object_get_data(obj);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "id", obj_id);
        nmo_cli_json_add_str_safe(doc, data, "name",
                                  (name && name[0]) ? name : "");

        if (ms) {
            char color_buf[16];

            format_argb(color_buf, sizeof(color_buf), ms->diffuse_color);
            yyjson_mut_obj_add_str(doc, data, "diffuse_color", color_buf);
            format_argb(color_buf, sizeof(color_buf), ms->ambient_color);
            yyjson_mut_obj_add_str(doc, data, "ambient_color", color_buf);
            format_argb(color_buf, sizeof(color_buf), ms->specular_color);
            yyjson_mut_obj_add_str(doc, data, "specular_color", color_buf);
            format_argb(color_buf, sizeof(color_buf), ms->emissive_color);
            yyjson_mut_obj_add_str(doc, data, "emissive_color", color_buf);

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
            yyjson_mut_obj_add_str(doc, data, "packed_modes", color_buf);
            format_argb(color_buf, sizeof(color_buf), ms->packed_flags);
            yyjson_mut_obj_add_str(doc, data, "packed_flags", color_buf);

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
