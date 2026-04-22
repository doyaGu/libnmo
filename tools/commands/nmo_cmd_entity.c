/**
 * @file nmo_cmd_entity.c
 * @brief CLI 3D entity command group implementation
 */

#include "nmo_cmd_entity.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_write.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "core/nmo_parse.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/builtin/nmo_targetlight_schemas.h"
#include "session/nmo_session_edit.h"
#include "type/nmo_type_string.h"

#include <stdio.h>
#include <stdlib.h>
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

typedef struct entity_list_json_data {
    yyjson_mut_doc *doc;
    yyjson_mut_val *arr;
    uint32_t found;
} entity_list_json_data_t;

typedef struct entity_list_table_data {
    nmo_cli_table_t *table;
    uint32_t found;
} entity_list_table_data_t;

static int entity_list_json_visitor(size_t index,
                                    nmo_object_t *obj,
                                    const nmo_cmd_ctx_t *c,
                                    void *user)
{
    (void)index;
    entity_list_json_data_t *data = (entity_list_json_data_t *)user;
    if (obj == NULL || data == NULL || data->doc == NULL || data->arr == NULL) {
        return 0;
    }

    yyjson_mut_doc *doc = data->doc;
    yyjson_mut_val *item = yyjson_mut_obj(doc);
    nmo_object_id_t id = nmo_object_get_id(obj);
    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    yyjson_mut_obj_add_uint(doc, item, "id", id);

    const char *cn = nmo_core_class_name(c, cid);
    if (cn) yyjson_mut_obj_add_str(doc, item, "class", cn);

    const char *name = nmo_object_get_name(obj);
    nmo_cli_json_add_str_safe(doc, item, "name",
                              (name && name[0]) ? name : "");

    const nmo_3dentity_state_t *es =
        (const nmo_3dentity_state_t *)nmo_object_get_state(obj);
    if (es) {
        yyjson_mut_val *pos_arr = yyjson_mut_arr(doc);
        yyjson_mut_arr_add_real(doc, pos_arr, (double)es->world_matrix[12]);
        yyjson_mut_arr_add_real(doc, pos_arr, (double)es->world_matrix[13]);
        yyjson_mut_arr_add_real(doc, pos_arr, (double)es->world_matrix[14]);
        yyjson_mut_obj_add_val(doc, item, "position", pos_arr);

        if (es->current_mesh_id) {
            yyjson_mut_obj_add_uint(doc, item, "mesh_id", es->current_mesh_id);
            const char *mn = resolve_name(c, es->current_mesh_id);
            if (mn && mn[0]) {
                nmo_cli_json_add_str_safe(doc, item, "mesh", mn);
            }
        }
    }

    yyjson_mut_arr_add_val(data->arr, item);
    data->found++;
    return 0;
}

static int entity_list_table_visitor(size_t index,
                                     nmo_object_t *obj,
                                     const nmo_cmd_ctx_t *c,
                                     void *user)
{
    (void)index;
    entity_list_table_data_t *data = (entity_list_table_data_t *)user;
    if (obj == NULL || data == NULL || data->table == NULL) {
        return 0;
    }

    char id_buf[16];
    snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    const char *cn = nmo_core_class_name(c, cid);
    if (!cn) cn = "-";

    const char *name = nmo_object_get_name(obj);
    if (!name || !name[0]) name = "-";

    char pos_buf[64];
    char mesh_buf[64];
    snprintf(pos_buf, sizeof(pos_buf), "-");
    snprintf(mesh_buf, sizeof(mesh_buf), "-");

    const nmo_3dentity_state_t *es =
        (const nmo_3dentity_state_t *)nmo_object_get_state(obj);
    if (es) {
        format_position(pos_buf, sizeof(pos_buf), es->world_matrix);
        if (es->current_mesh_id) {
            const char *mn = resolve_name(c, es->current_mesh_id);
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
    nmo_cli_table_add_row(data->table, cells, 5);
    data->found++;
    return 0;
}

/* ============================================================================
 * entity list
 * ============================================================================ */

static int entity_list_parse(int argc, char **argv, const char **class_filter) {
    static const nmo_opt_def_t opts[] = {
        {"--class", "-c", NMO_OPT_STRING, "Filter by class name (e.g. CKCamera, CKLight)"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    *class_filter = vals[0].present ? vals[0].val.str : NULL;
    return NMO_CLI_EXIT_SUCCESS;
}

static int entity_list_run(nmo_cmd_ctx_t *c, const char *class_filter) {
    nmo_object_query_t entity_query = {0};
    nmo_core_query_set_class_id(&entity_query, NMO_CID_3DENTITY, true);

    bool class_filter_is_entity = true;
    if (class_filter != NULL) {
        nmo_core_query_build_options_t query_opts = {
            .class_name = class_filter,
            .include_derived_classes = true,
        };
        int rc = nmo_core_query_build(c, &entity_query, &query_opts);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return rc;
        }
        class_filter_is_entity =
            nmo_core_class_derives(c, entity_query.class_id, NMO_CID_3DENTITY);
    }

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        entity_list_json_data_t jd = { .doc = doc, .arr = arr };
        if (class_filter_is_entity) {
            int rc = nmo_core_object_query_run(c, &entity_query,
                                               entity_list_json_visitor, &jd, NULL);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                return rc;
            }
        }

        yyjson_mut_obj_add_uint(doc, data, "count", jd.found);
        yyjson_mut_obj_add_val(doc, data, "entities", arr);
        nmo_cmd_ctx_json_end(c, doc, data, "entity.list");
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
        entity_list_table_data_t td = { .table = &table };
        if (class_filter_is_entity) {
            int rc = nmo_core_object_query_run(c, &entity_query,
                                               entity_list_table_visitor, &td, NULL);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                nmo_cli_table_free(&table);
                return rc;
            }
        }

        fprintf(c->out, "3D Entities: %u\n\n", td.found);
        nmo_cli_table_print(&table, c->out, c->colorize);
        nmo_cli_table_free(&table);
    }

    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_entity_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *class_filter = NULL;
    int rc = entity_list_parse(argc, argv, &class_filter);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    rc = entity_list_run(&c, class_filter);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * entity show
 * ============================================================================ */

typedef struct entity_show_args {
    nmo_core_object_selector_t selector;
} entity_show_args_t;

static int entity_show_parse(int argc,
                             char **argv,
                             bool in_session,
                             entity_show_args_t *args)
{
    static const nmo_opt_def_t opts[] = {
        {"--id",   "-i", NMO_OPT_UINT,   "Entity object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Entity object name"},
    };
    enum { OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    uint32_t min_pos = in_session ? 1u : 2u;
    const char *positional_id =
        (!has_selector_opt && r.pos_count >= min_pos) ? r.pos_args[0] : NULL;
    if (in_session && !has_selector_opt && r.pos_count != 1) {
        fprintf(stderr, "Usage: entity show [--id <id> | --name <name> | <id>]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    *args = (entity_show_args_t) {
        .selector = {
            .has_id = vals[OPT_ID].present,
            .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
            .positional_id = positional_id,
            .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .required_base_class = NMO_CID_3DENTITY,
            .selector_label = "Entity",
            .type_label = "CK3dEntity",
        },
    };
    return NMO_CLI_EXIT_SUCCESS;
}

static int entity_show_run(nmo_cmd_ctx_t *ctx, const entity_show_args_t *args)
{
    if (ctx == NULL || args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

#define c (*ctx)
    nmo_object_t *obj = NULL;
    nmo_object_id_t obj_id = 0;
    int rc = nmo_core_resolve_one_object(&c, &args->selector, &obj, &obj_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo entity show [--id <id> | --name <name> | <id>] <file>\n");
        return rc;
    }

    nmo_class_id_t class_id = nmo_object_get_class_id(obj);

    const char *name = nmo_object_get_name(obj);
    const char *class_name = nmo_core_class_name(&c, class_id);

    /* Get base 3D entity state -- always present for any derived type */
    const nmo_3dentity_state_t *es =
        (const nmo_3dentity_state_t *)nmo_object_get_state(obj);

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
                    (const nmo_camera_state_t *)nmo_object_get_state(obj);
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
                    (const nmo_light_state_t *)nmo_object_get_state(obj);
                if (ls) {
                    yyjson_mut_obj_add_str(doc, data, "light_type",
                                           light_type_str(ls->light_data.type));
                    char cbuf[64];
                    format_color_rgba(cbuf, sizeof(cbuf), &ls->light_data.diffuse);
                    yyjson_mut_obj_add_strcpy(doc, data, "light_diffuse", cbuf);
                    format_color_rgba(cbuf, sizeof(cbuf), &ls->light_data.specular);
                    yyjson_mut_obj_add_strcpy(doc, data, "light_specular", cbuf);
                    format_color_rgba(cbuf, sizeof(cbuf), &ls->light_data.ambient);
                    yyjson_mut_obj_add_strcpy(doc, data, "light_ambient", cbuf);
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
            return NMO_CLI_EXIT_SUCCESS;
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
                (const nmo_camera_state_t *)nmo_object_get_state(obj);
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
                (const nmo_light_state_t *)nmo_object_get_state(obj);
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

#undef c
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_entity_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    entity_show_args_t args;
    int rc = entity_show_parse(argc, argv, false, &args);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    rc = entity_show_run(&c, &args);
    return nmo_cmd_ctx_done(&c, rc);
}

int nmo_cmd_entity_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: entity list|show ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "list") == 0 || strcmp(argv[0], "ls") == 0) {
        const char *class_filter = NULL;
        int rc = entity_list_parse(argc, argv, &class_filter);
        if (rc != NMO_CLI_EXIT_SUCCESS) return rc;
        return entity_list_run(ctx, class_filter);
    }
    if (strcmp(argv[0], "show") == 0 || strcmp(argv[0], "s") == 0) {
        entity_show_args_t args;
        int rc = entity_show_parse(argc, argv, true, &args);
        if (rc != NMO_CLI_EXIT_SUCCESS) return rc;
        return entity_show_run(ctx, &args);
    }

    fprintf(stderr, "Unsupported entity read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}

/* ============================================================================
 * entity set-position - Set world_matrix translation through session edit
 * ============================================================================ */

typedef struct entity_set_position_args {
    nmo_core_object_selector_t selector;
    uint32_t object_id;
    float new_x;
    float new_y;
    float new_z;
    float old_x;
    float old_y;
    float old_z;
    char matrix_value[512];
} entity_set_position_args_t;

static int entity_set_position_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    entity_set_position_args_t *args = (entity_set_position_args_t *)user_data;
    if (c == NULL || args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_object_t *obj = NULL;
    nmo_object_id_t object_id = 0;
    int resolve_rc = nmo_core_resolve_one_object(c, &args->selector, &obj, &object_id);
    if (resolve_rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo entity set-position [--id <id> | --name <name> | <id>] <x> <y> <z> <file> -o <output>\n");
        return resolve_rc;
    }
    args->object_id = object_id;

    nmo_3dentity_state_t *estate =
        (nmo_3dentity_state_t *)nmo_object_get_state(obj);
    if (!estate) {
        fprintf(stderr, "Error: Object #%u has no deserialized state\n", args->object_id);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_3dentity_get_position(estate, &args->old_x, &args->old_y, &args->old_z);

    float matrix[16];
    memcpy(matrix, estate->world_matrix, sizeof(matrix));
    matrix[12] = args->new_x;
    matrix[13] = args->new_y;
    matrix[14] = args->new_z;
    int wrote = snprintf(
        args->matrix_value,
        sizeof(args->matrix_value),
        "(%.9g, %.9g, %.9g, %.9g; %.9g, %.9g, %.9g, %.9g; %.9g, %.9g, %.9g, %.9g; %.9g, %.9g, %.9g, %.9g)",
        matrix[0], matrix[1], matrix[2], matrix[3],
        matrix[4], matrix[5], matrix[6], matrix[7],
        matrix[8], matrix[9], matrix[10], matrix[11],
        matrix[12], matrix[13], matrix[14], matrix[15]);
    if (wrote < 0 || (size_t)wrote >= sizeof(args->matrix_value)) {
        fprintf(stderr, "Error: Position matrix string overflow\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_session_edit_t *edit = NULL;
    nmo_status_t rc = nmo_session_edit_begin(c->session, "entity set-position", &edit);
    if (rc == NMO_OK) {
        nmo_session_field_edit_t field = {
            .field_name = "world_matrix",
            .value_str = args->matrix_value,
        };
        rc = nmo_session_edit_set_object_fields(edit, args->object_id, &field, 1, NULL);
    }
    if (rc != NMO_OK) {
        if (edit) {
            nmo_session_edit_rollback(edit);
        }
        fprintf(stderr, "Error: Failed to set position: %s\n", nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (dry_run) {
        nmo_session_edit_rollback(edit);
    } else {
        rc = nmo_session_edit_commit(edit);
        if (rc != NMO_OK) {
            fprintf(stderr, "Error: Failed to commit edit: %s\n", nmo_error_string(rc));
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static int entity_set_position_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    entity_set_position_args_t *args = (entity_set_position_args_t *)user_data;
    if (c == NULL || args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    fprintf(c->out, "Entity #%u:\n", args->object_id);
    fprintf(c->out, "  position: (%.4f, %.4f, %.4f) -> (%.4f, %.4f, %.4f)%s\n",
            (double)args->old_x, (double)args->old_y, (double)args->old_z,
            (double)args->new_x, (double)args->new_y, (double)args->new_z,
            dry_run ? " (dry-run)" : "");
    if (!dry_run && output_path) {
        fprintf(c->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_entity_set_position(int argc, char **argv,
                                const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output file"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without saving"},
        {"--id",      NULL, NMO_OPT_UINT,   "Entity object ID"},
        {"--name",    "-n", NMO_OPT_STRING, "Entity object name"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_ID, OPT_NAME, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = NULL;
    int value_offset = 0;
    if (has_selector_opt) {
        if (r.pos_count < 4) {
            fprintf(stderr, "Usage: nmo entity set-position [--id <id> | --name <name> | <id>] <x> <y> <z> <file> -o <output>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    } else {
        if (r.pos_count < 5) {
            fprintf(stderr, "Usage: nmo entity set-position [--id <id> | --name <name> | <id>] <x> <y> <z> <file> -o <output>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        positional_id = r.pos_args[0];
        value_offset = 1;
    }

    if (!has_selector_opt && positional_id == NULL) {
        fprintf(stderr, "Error: No entity selector specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    float new_x = 0.0f;
    float new_y = 0.0f;
    float new_z = 0.0f;
    if (nmo_parse_f32(r.pos_args[value_offset], &new_x) != NMO_OK ||
        nmo_parse_f32(r.pos_args[value_offset + 1], &new_y) != NMO_OK ||
        nmo_parse_f32(r.pos_args[value_offset + 2], &new_z) != NMO_OK) {
        fprintf(stderr, "Error: Invalid position coordinates\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];
    entity_set_position_args_t args = {
        .selector = {
            .has_id = vals[OPT_ID].present,
            .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
            .positional_id = positional_id,
            .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .required_base_class = NMO_CID_3DENTITY,
            .selector_label = "Entity",
            .type_label = "CK3dEntity",
        },
        .new_x = new_x,
        .new_y = new_y,
        .new_z = new_z,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "entity.set-position",
        .output_required_unless_dry_run = true,
    };
    return nmo_cli_run_write_command(
        file_path,
        output,
        dry_run,
        global,
        &spec,
        entity_set_position_mutate,
        entity_set_position_report,
        &args);
}

/* ============================================================================
 * entity set-parent - Set parent entity via generic field setter
 * ============================================================================ */

typedef struct entity_set_parent_args {
    nmo_core_object_selector_t selector;
    uint32_t object_id;
    uint32_t parent_id;
    const char *parent_id_str;
} entity_set_parent_args_t;

static int entity_set_parent_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    entity_set_parent_args_t *args = (entity_set_parent_args_t *)user_data;
    if (c == NULL || args == NULL || args->parent_id_str == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_object_t *obj = NULL;
    nmo_object_id_t object_id = 0;
    int resolve_rc = nmo_core_resolve_one_object(c, &args->selector, &obj, &object_id);
    if (resolve_rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo entity set-parent [--id <id> | --name <name> | <id>] <parent-id> <file> -o <output>\n");
        return resolve_rc;
    }
    args->object_id = object_id;

    if (args->parent_id != 0) {
        if (args->parent_id == args->object_id) {
            fprintf(stderr, "Error: Cannot parent entity to itself\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        nmo_object_t *parent = nmo_core_find_by_id(c, args->parent_id);
        if (!parent) {
            fprintf(stderr, "Error: Parent object #%u not found\n", args->parent_id);
            return NMO_CLI_EXIT_NOT_FOUND;
        }
    }

    fprintf(c->out, "Entity #%u:\n", args->object_id);

    nmo_field_set_entry_t entry = { "parent_id", args->parent_id_str };
    nmo_field_set_result_t result;
    return nmo_core_set_fields(c, args->object_id, &entry, 1, dry_run, &result);
}

static int entity_set_parent_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)user_data;
    if (c == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!dry_run && output_path) {
        fprintf(c->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_entity_set_parent(int argc, char **argv,
                              const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output file"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without saving"},
        {"--id",      NULL, NMO_OPT_UINT,   "Entity object ID"},
        {"--name",    "-n", NMO_OPT_STRING, "Entity object name"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_ID, OPT_NAME, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = NULL;
    int parent_index = 0;
    if (has_selector_opt) {
        if (r.pos_count < 2) {
            fprintf(stderr, "Usage: nmo entity set-parent [--id <id> | --name <name> | <id>] <parent-id> <file> -o <output>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    } else {
        if (r.pos_count < 3) {
            fprintf(stderr, "Usage: nmo entity set-parent [--id <id> | --name <name> | <id>] <parent-id> <file> -o <output>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        positional_id = r.pos_args[0];
        parent_index = 1;
    }

    const char *parent_id_str = r.pos_args[parent_index];
    uint32_t parent_id;
    if (!nmo_tool_parse_u32(parent_id_str, &parent_id)) {
        fprintf(stderr, "Error: Invalid parent ID '%s'\n", parent_id_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    entity_set_parent_args_t args = {
        .selector = {
            .has_id = vals[OPT_ID].present,
            .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
            .positional_id = positional_id,
            .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .required_base_class = NMO_CID_3DENTITY,
            .selector_label = "Entity",
            .type_label = "CK3dEntity",
        },
        .parent_id = parent_id,
        .parent_id_str = parent_id_str,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "entity.set-parent",
        .output_required_unless_dry_run = true,
    };
    return nmo_cli_run_write_command(
        file_path,
        output,
        dry_run,
        global,
        &spec,
        entity_set_parent_mutate,
        entity_set_parent_report,
        &args);
}

/* ============================================================================
 * entity set-camera - Set camera fields (fov, near, far)
 * ============================================================================ */

typedef enum entity_field_target {
    ENTITY_FIELD_TARGET_CAMERA,
    ENTITY_FIELD_TARGET_LIGHT,
} entity_field_target_t;

typedef struct entity_set_fields_args {
    nmo_core_object_selector_t selector;
    uint32_t object_id;
    entity_field_target_t target;
    nmo_field_set_entry_t entries[3];
    size_t entry_count;
} entity_set_fields_args_t;

static int entity_set_fields_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    entity_set_fields_args_t *args = (entity_set_fields_args_t *)user_data;
    if (c == NULL || args == NULL || args->entry_count == 0) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_object_t *obj = NULL;
    nmo_object_id_t object_id = 0;
    int resolve_rc = nmo_core_resolve_one_object(c, &args->selector, &obj, &object_id);
    if (resolve_rc != NMO_CLI_EXIT_SUCCESS) {
        if (args->target == ENTITY_FIELD_TARGET_CAMERA) {
            fprintf(stderr, "Usage: nmo entity set-camera [--id <id> | --name <name> | <id>] [--fov <f>] [--near <f>] [--far <f>] <file> -o <output>\n");
        } else {
            fprintf(stderr, "Usage: nmo entity set-light [--id <id> | --name <name> | <id>] [--diffuse <color>] [--range <f>] <file> -o <output>\n");
        }
        return resolve_rc;
    }
    args->object_id = object_id;

    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    void *state = nmo_object_get_state(obj);
    if (state == NULL) {
        fprintf(stderr, "Error: Object #%u has no typed state\n", args->object_id);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (args->target == ENTITY_FIELD_TARGET_CAMERA) {
        if (class_id != NMO_CID_CAMERA && class_id != NMO_CID_TARGETCAMERA) {
            fprintf(stderr, "Error: Object #%u is not a CKCamera or CKTargetCamera (class %u)\n",
                    args->object_id, class_id);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        fprintf(c->out, "Camera #%u:\n", args->object_id);

        nmo_camera_state_t *camera =
            (class_id == NMO_CID_TARGETCAMERA)
                ? &((nmo_targetcamera_state_t *)state)->base
                : (nmo_camera_state_t *)state;
        for (size_t i = 0; i < args->entry_count; ++i) {
            const char *field = args->entries[i].field_name;
            const char *value = args->entries[i].value_str;
            float parsed;
            float *target = NULL;
            if (strcmp(field, "fov") == 0) {
                target = &camera->fov;
            } else if (strcmp(field, "near_plane") == 0) {
                target = &camera->near_plane;
            } else if (strcmp(field, "far_plane") == 0) {
                target = &camera->far_plane;
            }
            if (target == NULL || nmo_parse_f32(value, &parsed) != NMO_OK) {
                fprintf(stderr, "Error: Failed to set '%s' = '%s'\n", field, value);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            float old_value = *target;
            if (!dry_run) {
                *target = parsed;
            }
            fprintf(c->out, "  %s: %.9g -> %.9g%s\n",
                    field, old_value, parsed, dry_run ? " (dry-run)" : "");
        }
        return NMO_CLI_EXIT_SUCCESS;
    } else {
        if (class_id != NMO_CID_LIGHT && class_id != NMO_CID_TARGETLIGHT) {
            fprintf(stderr, "Error: Object #%u is not a CKLight or CKTargetLight (class %u)\n",
                    args->object_id, class_id);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        fprintf(c->out, "Light #%u:\n", args->object_id);

        nmo_light_state_t *light =
            (class_id == NMO_CID_TARGETLIGHT)
                ? &((nmo_targetlight_state_t *)state)->base
                : (nmo_light_state_t *)state;
        for (size_t i = 0; i < args->entry_count; ++i) {
            const char *field = args->entries[i].field_name;
            const char *value = args->entries[i].value_str;
            if (strcmp(field, "diffuse_color") == 0) {
                nmo_color_t parsed;
                if (nmo_color_from_string(&parsed, value) != NMO_OK) {
                    fprintf(stderr, "Error: Failed to set '%s' = '%s'\n", field, value);
                    return NMO_CLI_EXIT_ARG_ERROR;
                }
                uint32_t old_value = nmo_color_to_argb32_opaque(&light->light_data.diffuse);
                uint32_t new_value = nmo_color_to_argb32_opaque(&parsed);
                if (!dry_run) {
                    light->light_data.diffuse = parsed;
                }
                fprintf(c->out, "  %s: 0x%08X -> 0x%08X%s\n",
                        field, old_value, new_value, dry_run ? " (dry-run)" : "");
            } else if (strcmp(field, "range") == 0) {
                float parsed;
                if (nmo_parse_f32(value, &parsed) != NMO_OK) {
                    fprintf(stderr, "Error: Failed to set '%s' = '%s'\n", field, value);
                    return NMO_CLI_EXIT_ARG_ERROR;
                }
                float old_value = light->light_data.range;
                if (!dry_run) {
                    light->light_data.range = parsed;
                }
                fprintf(c->out, "  %s: %.9g -> %.9g%s\n",
                        field, old_value, parsed, dry_run ? " (dry-run)" : "");
            } else {
                fprintf(stderr, "Error: Unsupported light field '%s'\n", field);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
        }
        return NMO_CLI_EXIT_SUCCESS;
    }
}

static int entity_set_fields_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)user_data;
    if (c == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!dry_run && output_path) {
        fprintf(c->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_entity_set_camera(int argc, char **argv,
                              const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output file"},
        {"--fov",     NULL, NMO_OPT_STRING, "Field of view (radians)"},
        {"--near",    NULL, NMO_OPT_STRING, "Near clipping plane"},
        {"--far",     NULL, NMO_OPT_STRING, "Far clipping plane"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without saving"},
        {"--id",      NULL, NMO_OPT_UINT,   "Camera object ID"},
        {"--name",    "-n", NMO_OPT_STRING, "Camera object name"},
    };
    enum { OPT_OUTPUT, OPT_FOV, OPT_NEAR, OPT_FAR, OPT_DRYRUN,
           OPT_ID, OPT_NAME, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = NULL;
    if (has_selector_opt) {
        if (r.pos_count < 1) {
            fprintf(stderr, "Usage: nmo entity set-camera [--id <id> | --name <name> | <id>] [--fov <f>] [--near <f>] [--far <f>] <file> -o <output>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    } else {
        if (r.pos_count < 2) {
            fprintf(stderr, "Usage: nmo entity set-camera [--id <id> | --name <name> | <id>] [--fov <f>] [--near <f>] [--far <f>] <file> -o <output>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        positional_id = r.pos_args[0];
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    entity_set_fields_args_t args = {
        .selector = {
            .has_id = vals[OPT_ID].present,
            .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
            .positional_id = positional_id,
            .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .required_base_class = NMO_CID_CAMERA,
            .selector_label = "Camera",
            .type_label = "CKCamera",
        },
        .target = ENTITY_FIELD_TARGET_CAMERA,
        .entry_count = 0,
    };

    if (vals[OPT_FOV].present)
        args.entries[args.entry_count++] =
            (nmo_field_set_entry_t){"fov", vals[OPT_FOV].val.str};
    if (vals[OPT_NEAR].present)
        args.entries[args.entry_count++] =
            (nmo_field_set_entry_t){"near_plane", vals[OPT_NEAR].val.str};
    if (vals[OPT_FAR].present)
        args.entries[args.entry_count++] =
            (nmo_field_set_entry_t){"far_plane", vals[OPT_FAR].val.str};

    if (args.entry_count == 0) {
        fprintf(stderr, "Error: No camera properties specified. Use --fov, --near, or --far\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const nmo_cli_write_spec_t spec = {
        .command_name = "entity.set-camera",
        .output_required_unless_dry_run = true,
    };
    return nmo_cli_run_write_command(
        file_path,
        output,
        dry_run,
        global,
        &spec,
        entity_set_fields_mutate,
        entity_set_fields_report,
        &args);
}

/* ============================================================================
 * entity set-light - Set light fields (diffuse, range)
 * ============================================================================ */

int nmo_cmd_entity_set_light(int argc, char **argv,
                             const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output file"},
        {"--diffuse", NULL, NMO_OPT_STRING, "Diffuse color"},
        {"--range",   NULL, NMO_OPT_STRING, "Light range"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without saving"},
        {"--id",      NULL, NMO_OPT_UINT,   "Light object ID"},
        {"--name",    "-n", NMO_OPT_STRING, "Light object name"},
    };
    enum { OPT_OUTPUT, OPT_DIFFUSE, OPT_RANGE, OPT_DRYRUN,
           OPT_ID, OPT_NAME, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = NULL;
    if (has_selector_opt) {
        if (r.pos_count < 1) {
            fprintf(stderr, "Usage: nmo entity set-light [--id <id> | --name <name> | <id>] [--diffuse <color>] [--range <f>] <file> -o <output>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    } else {
        if (r.pos_count < 2) {
            fprintf(stderr, "Usage: nmo entity set-light [--id <id> | --name <name> | <id>] [--diffuse <color>] [--range <f>] <file> -o <output>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        positional_id = r.pos_args[0];
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    entity_set_fields_args_t args = {
        .selector = {
            .has_id = vals[OPT_ID].present,
            .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
            .positional_id = positional_id,
            .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .required_base_class = NMO_CID_LIGHT,
            .selector_label = "Light",
            .type_label = "CKLight",
        },
        .target = ENTITY_FIELD_TARGET_LIGHT,
        .entry_count = 0,
    };

    if (vals[OPT_DIFFUSE].present)
        args.entries[args.entry_count++] =
            (nmo_field_set_entry_t){"diffuse_color", vals[OPT_DIFFUSE].val.str};
    if (vals[OPT_RANGE].present)
        args.entries[args.entry_count++] =
            (nmo_field_set_entry_t){"range", vals[OPT_RANGE].val.str};

    if (args.entry_count == 0) {
        fprintf(stderr, "Error: No light properties specified. Use --diffuse or --range\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const nmo_cli_write_spec_t spec = {
        .command_name = "entity.set-light",
        .output_required_unless_dry_run = true,
    };
    return nmo_cli_run_write_command(
        file_path,
        output,
        dry_run,
        global,
        &spec,
        entity_set_fields_mutate,
        entity_set_fields_report,
        &args);
}
