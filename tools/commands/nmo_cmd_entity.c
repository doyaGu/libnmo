/**
 * @file nmo_cmd_entity.c
 * @brief CLI 3D entity command group implementation
 */

#include "nmo_cmd_entity.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_light_schemas.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* ============================================================================
 * Helpers
 * ============================================================================ */

static const char *resolve_name(const nmo_cmd_ctx_t *c, nmo_object_id_t id) {
    if (id == 0) return NULL;
    nmo_object_t *obj = nmo_core_find_by_id(c, id);
    if (!obj) return NULL;
    return nmo_object_get_name(obj);
}

static const char *light_type_str(VXLIGHT_TYPE type) {
    switch (type) {
    case VX_LIGHTPOINT: return "point";
    case VX_LIGHTSPOT:  return "spot";
    case VX_LIGHTDIREC: return "directional";
    case VX_LIGHTPARA:  return "parallel";
    default:            return "unknown";
    }
}

static const char *projection_type_str(uint32_t type) {
    switch (type) {
    case 1:  return "perspective";
    case 2:  return "orthographic";
    default: return "unknown";
    }
}

static void format_position(char *buf, size_t buf_size, const float *matrix) {
    snprintf(buf, buf_size, "(%.2f, %.2f, %.2f)",
             (double)matrix[12], (double)matrix[13], (double)matrix[14]);
}

static void format_color_rgba(char *buf, size_t buf_size, const nmo_color_t *color) {
    snprintf(buf, buf_size, "(%.3f, %.3f, %.3f, %.3f)",
             (double)color->r, (double)color->g,
             (double)color->b, (double)color->a);
}

/* ============================================================================
 * entity list
 * ============================================================================ */

int nmo_cmd_entity_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--class", "-c", NMO_OPT_STRING, "Filter by class name (e.g. CKCamera, CKLight)"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *class_filter = vals[0].present ? vals[0].val.str : NULL;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Resolve optional class filter to class ID */
    nmo_class_id_t filter_cid = 0;
    if (class_filter) {
        filter_cid = nmo_core_class_id(&c, class_filter);
        if (filter_cid == 0) {
            fprintf(stderr, "Error: Unknown class '%s'\n", class_filter);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

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

            if (!nmo_core_class_derives(&c, cid, NMO_CID_3DENTITY)) continue;
            if (filter_cid && !nmo_core_class_derives(&c, cid, filter_cid)) continue;

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            nmo_object_id_t id = nmo_object_get_id(obj);
            yyjson_mut_obj_add_uint(doc, item, "id", id);

            const char *cn = nmo_core_class_name(&c, cid);
            if (cn) yyjson_mut_obj_add_str(doc, item, "class", cn);

            const char *name = nmo_object_get_name(obj);
            nmo_cli_json_add_str_safe(doc, item, "name",
                                      (name && name[0]) ? name : "");

            const nmo_3dentity_state_t *es =
                (const nmo_3dentity_state_t *)nmo_object_get_data(obj);
            if (es) {
                yyjson_mut_val *pos_arr = yyjson_mut_arr(doc);
                yyjson_mut_arr_add_real(doc, pos_arr, (double)es->world_matrix[12]);
                yyjson_mut_arr_add_real(doc, pos_arr, (double)es->world_matrix[13]);
                yyjson_mut_arr_add_real(doc, pos_arr, (double)es->world_matrix[14]);
                yyjson_mut_obj_add_val(doc, item, "position", pos_arr);

                if (es->current_mesh_id) {
                    yyjson_mut_obj_add_uint(doc, item, "mesh_id", es->current_mesh_id);
                    const char *mn = resolve_name(&c, es->current_mesh_id);
                    if (mn && mn[0]) {
                        nmo_cli_json_add_str_safe(doc, item, "mesh", mn);
                    }
                }
            }

            yyjson_mut_arr_add_val(arr, item);
            found++;
        }

        yyjson_mut_obj_add_uint(doc, data, "count", found);
        yyjson_mut_obj_add_val(doc, data, "entities", arr);
        nmo_cmd_ctx_json_end(&c, doc, data, "entity.list");
    } else {
        static const nmo_cli_table_col_t columns[] = {
            {"ID",       NMO_CLI_ALIGN_RIGHT, 6,  0},
            {"CLASS",    NMO_CLI_ALIGN_LEFT,  14, 0},
            {"NAME",     NMO_CLI_ALIGN_LEFT,  20, 40},
            {"POSITION", NMO_CLI_ALIGN_LEFT,  24, 0},
            {"MESH",     NMO_CLI_ALIGN_LEFT,  20, 40},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));
        uint32_t found = 0;

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            if (!obj) continue;
            nmo_class_id_t cid = nmo_object_get_class_id(obj);

            if (!nmo_core_class_derives(&c, cid, NMO_CID_3DENTITY)) continue;
            if (filter_cid && !nmo_core_class_derives(&c, cid, filter_cid)) continue;

            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

            const char *cn = nmo_core_class_name(&c, cid);
            if (!cn) cn = "-";

            const char *name = nmo_object_get_name(obj);
            if (!name || !name[0]) name = "-";

            char pos_buf[64];
            char mesh_buf[64];
            snprintf(pos_buf, sizeof(pos_buf), "-");
            snprintf(mesh_buf, sizeof(mesh_buf), "-");

            const nmo_3dentity_state_t *es =
                (const nmo_3dentity_state_t *)nmo_object_get_data(obj);
            if (es) {
                format_position(pos_buf, sizeof(pos_buf), es->world_matrix);
                if (es->current_mesh_id) {
                    const char *mn = resolve_name(&c, es->current_mesh_id);
                    if (mn && mn[0]) {
                        snprintf(mesh_buf, sizeof(mesh_buf), "#%u (%s)",
                                 es->current_mesh_id, mn);
                    } else {
                        snprintf(mesh_buf, sizeof(mesh_buf), "#%u",
                                 es->current_mesh_id);
                    }
                }
            }

            const char *cells[] = {id_buf, cn, name, pos_buf, mesh_buf};
            nmo_cli_table_add_row(&table, cells, 5);
            found++;
        }

        fprintf(c.out, "3D Entities: %u\n\n", found);
        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * entity show
 * ============================================================================ */

int nmo_cmd_entity_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
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
        fprintf(stderr, "Usage: nmo entity show <id> <file>\n");
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

    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    if (!nmo_core_class_derives(&c, class_id, NMO_CID_3DENTITY)) {
        fprintf(stderr, "Error: Object %u is not a CK3dEntity (class %u)\n",
                obj_id, class_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const char *name = nmo_object_get_name(obj);
    const char *class_name = nmo_core_class_name(&c, class_id);

    /* Get base 3D entity state -- always present for any derived type */
    const nmo_3dentity_state_t *es =
        (const nmo_3dentity_state_t *)nmo_object_get_data(obj);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "id", obj_id);
        nmo_cli_json_add_str_safe(doc, data, "name",
                                  (name && name[0]) ? name : "");
        if (class_name)
            yyjson_mut_obj_add_str(doc, data, "class", class_name);

        if (es) {
            /* Position */
            yyjson_mut_val *pos_arr = yyjson_mut_arr(doc);
            yyjson_mut_arr_add_real(doc, pos_arr, (double)es->world_matrix[12]);
            yyjson_mut_arr_add_real(doc, pos_arr, (double)es->world_matrix[13]);
            yyjson_mut_arr_add_real(doc, pos_arr, (double)es->world_matrix[14]);
            yyjson_mut_obj_add_val(doc, data, "position", pos_arr);

            /* Full matrix */
            yyjson_mut_val *mat = yyjson_mut_arr(doc);
            for (int mi = 0; mi < 16; ++mi) {
                yyjson_mut_arr_add_real(doc, mat, (double)es->world_matrix[mi]);
            }
            yyjson_mut_obj_add_val(doc, data, "world_matrix", mat);

            yyjson_mut_obj_add_uint(doc, data, "entity_flags", es->entity_flags);
            yyjson_mut_obj_add_uint(doc, data, "moveable_flags", es->moveable_flags);

            yyjson_mut_obj_add_uint(doc, data, "current_mesh_id", es->current_mesh_id);
            if (es->current_mesh_id) {
                const char *mn = resolve_name(&c, es->current_mesh_id);
                if (mn && mn[0]) {
                    nmo_cli_json_add_str_safe(doc, data, "current_mesh", mn);
                }
            }

            /* Mesh IDs */
            if (es->mesh_count > 0 && es->mesh_ids) {
                yyjson_mut_val *mesh_arr = yyjson_mut_arr(doc);
                for (uint32_t mi = 0; mi < es->mesh_count; ++mi) {
                    yyjson_mut_arr_add_uint(doc, mesh_arr, es->mesh_ids[mi]);
                }
                yyjson_mut_obj_add_val(doc, data, "mesh_ids", mesh_arr);
            }

            /* Animation IDs */
            if (es->animation_count > 0 && es->animation_ids) {
                yyjson_mut_val *anim_arr = yyjson_mut_arr(doc);
                for (uint32_t ai = 0; ai < es->animation_count; ++ai) {
                    yyjson_mut_arr_add_uint(doc, anim_arr, es->animation_ids[ai]);
                }
                yyjson_mut_obj_add_val(doc, data, "animation_ids", anim_arr);
            }

            yyjson_mut_obj_add_uint(doc, data, "parent_id", es->parent_id);
            if (es->parent_id) {
                const char *pn = resolve_name(&c, es->parent_id);
                if (pn && pn[0]) {
                    nmo_cli_json_add_str_safe(doc, data, "parent", pn);
                }
            }

            /* Camera-specific */
            if (class_id == NMO_CID_CAMERA || class_id == NMO_CID_TARGETCAMERA) {
                const nmo_camera_state_t *cs =
                    (const nmo_camera_state_t *)nmo_object_get_data(obj);
                if (cs) {
                    yyjson_mut_obj_add_str(doc, data, "projection_type",
                                           projection_type_str(cs->projection_type));
                    yyjson_mut_obj_add_real(doc, data, "fov", (double)cs->fov);
                    yyjson_mut_obj_add_real(doc, data, "near_plane", (double)cs->near_plane);
                    yyjson_mut_obj_add_real(doc, data, "far_plane", (double)cs->far_plane);
                    yyjson_mut_obj_add_int(doc, data, "width", cs->width);
                    yyjson_mut_obj_add_int(doc, data, "height", cs->height);
                }
            }

            /* Light-specific */
            if (class_id == NMO_CID_LIGHT || class_id == NMO_CID_TARGETLIGHT) {
                const nmo_light_state_t *ls =
                    (const nmo_light_state_t *)nmo_object_get_data(obj);
                if (ls) {
                    yyjson_mut_obj_add_str(doc, data, "light_type",
                                           light_type_str(ls->light_data.type));
                    char cbuf[64];
                    format_color_rgba(cbuf, sizeof(cbuf), &ls->light_data.diffuse);
                    yyjson_mut_obj_add_str(doc, data, "light_diffuse", cbuf);
                    format_color_rgba(cbuf, sizeof(cbuf), &ls->light_data.specular);
                    yyjson_mut_obj_add_str(doc, data, "light_specular", cbuf);
                    format_color_rgba(cbuf, sizeof(cbuf), &ls->light_data.ambient);
                    yyjson_mut_obj_add_str(doc, data, "light_ambient", cbuf);
                    yyjson_mut_obj_add_real(doc, data, "light_range",
                                           (double)ls->light_data.range);
                    yyjson_mut_obj_add_real(doc, data, "attenuation0",
                                           (double)ls->light_data.attenuation0);
                    yyjson_mut_obj_add_real(doc, data, "attenuation1",
                                           (double)ls->light_data.attenuation1);
                    yyjson_mut_obj_add_real(doc, data, "attenuation2",
                                           (double)ls->light_data.attenuation2);
                    yyjson_mut_obj_add_real(doc, data, "light_power",
                                           (double)ls->light_power);
                }
            }
        } else {
            yyjson_mut_obj_add_null(doc, data, "state");
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "entity.show");
    } else {
        /* Text output */
        nmo_cli_print_heading(c.out, "3D Entity Details", c.colorize);

        char buf[128];
        snprintf(buf, sizeof(buf), "#%u (%s)", obj_id,
                 (name && name[0]) ? name : "(unnamed)");
        nmo_cli_print_kv(c.out, "ID / Name", buf, 20, c.colorize);

        snprintf(buf, sizeof(buf), "#%u (%s)", class_id,
                 class_name ? class_name : "-");
        nmo_cli_print_kv(c.out, "Class", buf, 20, c.colorize);

        if (!es) {
            fprintf(c.out, "\n  (no deserialized state)\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
        }

        format_position(buf, sizeof(buf), es->world_matrix);
        nmo_cli_print_kv(c.out, "Position", buf, 20, c.colorize);

        snprintf(buf, sizeof(buf), "0x%08X", es->entity_flags);
        nmo_cli_print_kv(c.out, "Entity Flags", buf, 20, c.colorize);

        snprintf(buf, sizeof(buf), "0x%08X", es->moveable_flags);
        nmo_cli_print_kv(c.out, "Moveable Flags", buf, 20, c.colorize);

        /* Current mesh */
        if (es->current_mesh_id) {
            const char *mn = resolve_name(&c, es->current_mesh_id);
            if (mn && mn[0]) {
                snprintf(buf, sizeof(buf), "#%u (%s)", es->current_mesh_id, mn);
            } else {
                snprintf(buf, sizeof(buf), "#%u", es->current_mesh_id);
            }
        } else {
            snprintf(buf, sizeof(buf), "(none)");
        }
        nmo_cli_print_kv(c.out, "Current Mesh", buf, 20, c.colorize);

        /* Mesh list */
        if (es->mesh_count > 0 && es->mesh_ids) {
            fprintf(c.out, "\nMeshes (%u):\n", es->mesh_count);
            for (uint32_t mi = 0; mi < es->mesh_count; ++mi) {
                const char *mn = resolve_name(&c, es->mesh_ids[mi]);
                if (mn && mn[0]) {
                    fprintf(c.out, "  [%u] #%u (%s)\n", mi, es->mesh_ids[mi], mn);
                } else {
                    fprintf(c.out, "  [%u] #%u\n", mi, es->mesh_ids[mi]);
                }
            }
        }

        /* Animation list */
        if (es->animation_count > 0 && es->animation_ids) {
            fprintf(c.out, "\nAnimations (%u):\n", es->animation_count);
            for (uint32_t ai = 0; ai < es->animation_count; ++ai) {
                const char *an = resolve_name(&c, es->animation_ids[ai]);
                if (an && an[0]) {
                    fprintf(c.out, "  [%u] #%u (%s)\n", ai, es->animation_ids[ai], an);
                } else {
                    fprintf(c.out, "  [%u] #%u\n", ai, es->animation_ids[ai]);
                }
            }
        }

        /* Parent */
        if (es->parent_id) {
            const char *pn = resolve_name(&c, es->parent_id);
            if (pn && pn[0]) {
                snprintf(buf, sizeof(buf), "#%u (%s)", es->parent_id, pn);
            } else {
                snprintf(buf, sizeof(buf), "#%u", es->parent_id);
            }
        } else {
            snprintf(buf, sizeof(buf), "(none)");
        }
        nmo_cli_print_kv(c.out, "Parent", buf, 20, c.colorize);

        /* World matrix */
        fprintf(c.out, "\nWorld Matrix:\n");
        for (int row = 0; row < 4; ++row) {
            fprintf(c.out, "  [%8.4f %8.4f %8.4f %8.4f]\n",
                    (double)es->world_matrix[row * 4 + 0],
                    (double)es->world_matrix[row * 4 + 1],
                    (double)es->world_matrix[row * 4 + 2],
                    (double)es->world_matrix[row * 4 + 3]);
        }

        /* Camera-specific section */
        if (class_id == NMO_CID_CAMERA || class_id == NMO_CID_TARGETCAMERA) {
            const nmo_camera_state_t *cs =
                (const nmo_camera_state_t *)nmo_object_get_data(obj);
            if (cs) {
                fprintf(c.out, "\nCamera:\n");

                nmo_cli_print_kv(c.out, "  Projection",
                                 projection_type_str(cs->projection_type), 20, c.colorize);

                snprintf(buf, sizeof(buf), "%.4f rad (%.1f deg)",
                         (double)cs->fov, (double)(cs->fov * 180.0f / 3.14159265f));
                nmo_cli_print_kv(c.out, "  FOV", buf, 20, c.colorize);

                snprintf(buf, sizeof(buf), "%.4f", (double)cs->near_plane);
                nmo_cli_print_kv(c.out, "  Near Plane", buf, 20, c.colorize);

                snprintf(buf, sizeof(buf), "%.4f", (double)cs->far_plane);
                nmo_cli_print_kv(c.out, "  Far Plane", buf, 20, c.colorize);

                snprintf(buf, sizeof(buf), "%d x %d", cs->width, cs->height);
                nmo_cli_print_kv(c.out, "  Viewport", buf, 20, c.colorize);
            }
        }

        /* Light-specific section */
        if (class_id == NMO_CID_LIGHT || class_id == NMO_CID_TARGETLIGHT) {
            const nmo_light_state_t *ls =
                (const nmo_light_state_t *)nmo_object_get_data(obj);
            if (ls) {
                fprintf(c.out, "\nLight:\n");

                nmo_cli_print_kv(c.out, "  Type",
                                 light_type_str(ls->light_data.type), 20, c.colorize);

                format_color_rgba(buf, sizeof(buf), &ls->light_data.diffuse);
                nmo_cli_print_kv(c.out, "  Diffuse", buf, 20, c.colorize);

                format_color_rgba(buf, sizeof(buf), &ls->light_data.specular);
                nmo_cli_print_kv(c.out, "  Specular", buf, 20, c.colorize);

                format_color_rgba(buf, sizeof(buf), &ls->light_data.ambient);
                nmo_cli_print_kv(c.out, "  Ambient", buf, 20, c.colorize);

                snprintf(buf, sizeof(buf), "%.4f", (double)ls->light_data.range);
                nmo_cli_print_kv(c.out, "  Range", buf, 20, c.colorize);

                snprintf(buf, sizeof(buf), "(%.4f, %.4f, %.4f)",
                         (double)ls->light_data.attenuation0,
                         (double)ls->light_data.attenuation1,
                         (double)ls->light_data.attenuation2);
                nmo_cli_print_kv(c.out, "  Attenuation", buf, 20, c.colorize);

                snprintf(buf, sizeof(buf), "%.4f", (double)ls->light_power);
                nmo_cli_print_kv(c.out, "  Power", buf, 20, c.colorize);
            }
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}
