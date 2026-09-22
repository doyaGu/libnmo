/**
 * @file nmo_cmd_entity.c
 * @brief CLI 3D entity command group implementation
 */

#include "nmo_cmd_entity.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_record.h"
#include "../nmo_cli_write.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "core/nmo_parse.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_edit.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/builtin/nmo_targetlight_schemas.h"

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

static nmo_status_t parse_color_rgba(const char *text, nmo_color_t *out_color) {
    if (!text || !out_color) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    nmo_status_t status =
        nmo_parse_f32_parenthesized_tuple(text, ",;", values, 4u);
    if (status != NMO_OK) {
        return status;
    }

    out_color->r = values[0];
    out_color->g = values[1];
    out_color->b = values[2];
    out_color->a = values[3];
    return NMO_OK;
}

typedef struct entity_list_data {
    yyjson_mut_doc *doc;    /* JSON sink when non-NULL */
    yyjson_mut_val *arr;
    nmo_cli_table_t *table; /* text sink otherwise */
    uint32_t found;
} entity_list_data_t;

static bool entity_list_build_record(const nmo_cmd_ctx_t *c,
                                     nmo_object_t *obj,
                                     nmo_cli_record_t *rec)
{
    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    const char *name = nmo_object_get_name(obj);

    bool ok = nmo_cli_record_uint(rec, "id", "ID", nmo_object_get_id(obj));
    ok = ok && nmo_cli_record_str_opt(rec, "class", "CLASS",
                                      nmo_core_class_name(c, cid), "-");
    ok = ok && nmo_cli_record_str(rec, "name", "NAME", name);
    if (ok && (!name || !name[0])) {
        ok = nmo_cli_record_set_text(rec, "-");
    }

    const nmo_3dentity_state_t *es =
        (const nmo_3dentity_state_t *)nmo_object_get_state(obj);
    if (es) {
        char pos_buf[64];
        const double pos[3] = {
            (double)es->world_matrix[12],
            (double)es->world_matrix[13],
            (double)es->world_matrix[14],
        };
        format_position(pos_buf, sizeof(pos_buf), es->world_matrix);
        ok = ok && nmo_cli_record_real_list(rec, "position", "POSITION", pos, 3, pos_buf);
        nmo_object_id_t mesh_id = nmo_ref_runtime_id(&es->current_mesh);
        ok = ok && nmo_cli_record_ref_opt(rec, "mesh_id", "mesh", "MESH", mesh_id,
                                          resolve_name(c, mesh_id), "-");
    } else {
        ok = ok && nmo_cli_record_text(rec, "POSITION", "-");
        ok = ok && nmo_cli_record_text(rec, "MESH", "-");
    }
    return ok;
}

static int entity_list_visitor(size_t index,
                               nmo_object_t *obj,
                               const nmo_cmd_ctx_t *c,
                               void *user)
{
    (void)index;
    entity_list_data_t *data = (entity_list_data_t *)user;
    if (obj == NULL || data == NULL) {
        return 0;
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec || !entity_list_build_record(c, obj, rec)) {
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
        entity_list_data_t ld = { .doc = doc, .arr = arr };
        if (class_filter_is_entity) {
            int rc = nmo_core_object_query_run(c, &entity_query,
                                               entity_list_visitor, &ld, NULL);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                return rc;
            }
        }

        yyjson_mut_obj_add_uint(doc, data, "count", ld.found);
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
        entity_list_data_t ld = { .table = &table };
        if (class_filter_is_entity) {
            int rc = nmo_core_object_query_run(c, &entity_query,
                                               entity_list_visitor, &ld, NULL);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                nmo_cli_table_free(&table);
                return rc;
            }
        }

        fprintf(c->out, "3D Entities: %u\n\n", ld.found);
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

/* "\n<Heading> (<count>):\n" plus one "  [i] #id (name)" line per valid reference. */
static bool entity_append_ref_list_text(char **out, size_t *cap, size_t *len,
                                        const char *text)
{
    size_t add = strlen(text);
    if (*len + add + 1u > *cap) {
        size_t new_cap = (*cap ? *cap * 2u : 256u);
        while (new_cap < *len + add + 1u) new_cap *= 2u;
        char *grown = (char *)realloc(*out, new_cap);
        if (!grown) return false;
        *out = grown;
        *cap = new_cap;
    }
    memcpy(*out + *len, text, add + 1u);
    *len += add;
    return true;
}

/* JSON: array of valid ids. Text: heading with the raw count and named lines. */
static bool entity_record_ref_list(const nmo_cmd_ctx_t *c,
                                   nmo_cli_record_t *rec,
                                   const char *key,
                                   const char *heading,
                                   const nmo_ref_t *refs,
                                   uint32_t count)
{
    uint64_t *ids = (uint64_t *)malloc(count * sizeof(uint64_t));
    char *text = NULL;
    size_t cap = 0, len = 0, n = 0;
    char line[192];
    bool ok = ids != NULL;

    snprintf(line, sizeof(line), "\n%s (%u):\n", heading, count);
    ok = ok && entity_append_ref_list_text(&text, &cap, &len, line);
    for (uint32_t i = 0; ok && i < count; ++i) {
        const nmo_object_id_t id = nmo_ref_runtime_id(&refs[i]);
        if (id == NMO_OBJECT_ID_NONE) continue;
        ids[n++] = id;
        const char *rn = resolve_name(c, id);
        if (rn && rn[0]) {
            snprintf(line, sizeof(line), "  [%u] #%u (%s)\n", i, id, rn);
        } else {
            snprintf(line, sizeof(line), "  [%u] #%u\n", i, id);
        }
        ok = entity_append_ref_list_text(&text, &cap, &len, line);
    }
    ok = ok && nmo_cli_record_uint_list(rec, key, NULL, ids, n, NULL);
    ok = ok && nmo_cli_record_raw(rec, text);
    free(ids);
    free(text);
    return ok;
}

static bool entity_show_build_record(const nmo_cmd_ctx_t *c,
                                     nmo_cli_record_t *rec,
                                     nmo_object_t *obj,
                                     nmo_object_id_t obj_id)
{
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const char *name = nmo_object_get_name(obj);
    const char *class_name = nmo_core_class_name(c, class_id);
    const nmo_3dentity_state_t *es =
        (const nmo_3dentity_state_t *)nmo_object_get_state(obj);
    char buf[128];

    snprintf(buf, sizeof(buf), "#%u (%s)", obj_id,
             (name && name[0]) ? name : "(unnamed)");
    bool ok = nmo_cli_record_uint(rec, "id", NULL, obj_id);
    ok = ok && nmo_cli_record_str(rec, "name", NULL, name);
    ok = ok && nmo_cli_record_text(rec, "ID / Name", buf);
    ok = ok && nmo_cli_record_str_opt(rec, "class", NULL, class_name, NULL);
    snprintf(buf, sizeof(buf), "#%u (%s)", class_id, class_name ? class_name : "-");
    ok = ok && nmo_cli_record_text(rec, "Class", buf);
    if (!ok) {
        return false;
    }
    if (!es) {
        return nmo_cli_record_null(rec, "state", NULL, NULL);
    }

    const double pos[3] = {
        (double)es->world_matrix[12],
        (double)es->world_matrix[13],
        (double)es->world_matrix[14],
    };
    format_position(buf, sizeof(buf), es->world_matrix);
    ok = nmo_cli_record_real_list(rec, "position", "Position", pos, 3, buf);

    double matrix[16];
    for (int mi = 0; mi < 16; ++mi) {
        matrix[mi] = (double)es->world_matrix[mi];
    }
    ok = ok && nmo_cli_record_real_list(rec, "world_matrix", NULL, matrix, 16, NULL);

    ok = ok && nmo_cli_record_uint(rec, "entity_flags", "Entity Flags", es->entity_flags);
    snprintf(buf, sizeof(buf), "0x%08X", es->entity_flags);
    ok = ok && nmo_cli_record_set_text(rec, buf);
    ok = ok && nmo_cli_record_uint(rec, "moveable_flags", "Moveable Flags",
                                   es->moveable_flags);
    snprintf(buf, sizeof(buf), "0x%08X", es->moveable_flags);
    ok = ok && nmo_cli_record_set_text(rec, buf);

    nmo_object_id_t mesh_id = nmo_ref_runtime_id(&es->current_mesh);
    ok = ok && nmo_cli_record_ref(rec, "current_mesh_id", "current_mesh",
                                  "Current Mesh", mesh_id, resolve_name(c, mesh_id),
                                  "(none)");

    if (ok && es->mesh_count > 0 && es->mesh_ids) {
        ok = entity_record_ref_list(c, rec, "mesh_ids", "Meshes",
                                    es->mesh_ids, es->mesh_count);
    }
    if (ok && es->animation_count > 0 && es->animation_ids) {
        ok = entity_record_ref_list(c, rec, "animation_ids", "Animations",
                                    es->animation_ids, es->animation_count);
    }

    nmo_object_id_t parent_id = nmo_ref_runtime_id(&es->parent);
    ok = ok && nmo_cli_record_ref(rec, "parent_id", "parent", "Parent", parent_id,
                                  resolve_name(c, parent_id), "(none)");

    /* Text-only matrix block (JSON carries it as "world_matrix" above). */
    {
        char block[512];
        int off = snprintf(block, sizeof(block), "\nWorld Matrix:\n");
        for (int row = 0; row < 4 && off > 0 && (size_t)off < sizeof(block); ++row) {
            off += snprintf(block + off, sizeof(block) - (size_t)off,
                            "  [%8.4f %8.4f %8.4f %8.4f]\n",
                            (double)es->world_matrix[row * 4 + 0],
                            (double)es->world_matrix[row * 4 + 1],
                            (double)es->world_matrix[row * 4 + 2],
                            (double)es->world_matrix[row * 4 + 3]);
        }
        ok = ok && nmo_cli_record_raw(rec, block);
    }

    if (class_id == NMO_CID_CAMERA || class_id == NMO_CID_TARGETCAMERA) {
        const nmo_camera_state_t *cs =
            (const nmo_camera_state_t *)nmo_object_get_state(obj);
        if (cs) {
            ok = ok && nmo_cli_record_raw(rec, "\nCamera:\n");
            ok = ok && nmo_cli_record_str(rec, "projection_type", "  Projection",
                                          projection_type_str(cs->projection_type));
            snprintf(buf, sizeof(buf), "%.4f rad (%.1f deg)",
                     (double)cs->fov, (double)(cs->fov * 180.0f / 3.14159265f));
            ok = ok && nmo_cli_record_real(rec, "fov", "  FOV", (double)cs->fov, NULL);
            ok = ok && nmo_cli_record_set_text(rec, buf);
            ok = ok && nmo_cli_record_real(rec, "near_plane", "  Near Plane",
                                           (double)cs->near_plane, "%.4f");
            ok = ok && nmo_cli_record_real(rec, "far_plane", "  Far Plane",
                                           (double)cs->far_plane, "%.4f");
            ok = ok && nmo_cli_record_int(rec, "width", NULL, cs->width);
            ok = ok && nmo_cli_record_int(rec, "height", NULL, cs->height);
            snprintf(buf, sizeof(buf), "%d x %d", cs->width, cs->height);
            ok = ok && nmo_cli_record_text(rec, "  Viewport", buf);
        }
    }

    if (class_id == NMO_CID_LIGHT || class_id == NMO_CID_TARGETLIGHT) {
        const nmo_light_state_t *ls =
            (const nmo_light_state_t *)nmo_object_get_state(obj);
        if (ls) {
            ok = ok && nmo_cli_record_raw(rec, "\nLight:\n");
            ok = ok && nmo_cli_record_str(rec, "light_type", "  Type",
                                          light_type_str(ls->light_data.type));
            format_color_rgba(buf, sizeof(buf), &ls->light_data.diffuse);
            ok = ok && nmo_cli_record_str(rec, "light_diffuse", "  Diffuse", buf);
            format_color_rgba(buf, sizeof(buf), &ls->light_data.specular);
            ok = ok && nmo_cli_record_str(rec, "light_specular", "  Specular", buf);
            format_color_rgba(buf, sizeof(buf), &ls->light_data.ambient);
            ok = ok && nmo_cli_record_str(rec, "light_ambient", "  Ambient", buf);
            ok = ok && nmo_cli_record_real(rec, "light_range", "  Range",
                                           (double)ls->light_data.range, "%.4f");
            snprintf(buf, sizeof(buf), "(%.4f, %.4f, %.4f)",
                     (double)ls->light_data.attenuation0,
                     (double)ls->light_data.attenuation1,
                     (double)ls->light_data.attenuation2);
            ok = ok && nmo_cli_record_real(rec, "attenuation0", NULL,
                                           (double)ls->light_data.attenuation0, NULL);
            ok = ok && nmo_cli_record_real(rec, "attenuation1", NULL,
                                           (double)ls->light_data.attenuation1, NULL);
            ok = ok && nmo_cli_record_real(rec, "attenuation2", NULL,
                                           (double)ls->light_data.attenuation2, NULL);
            ok = ok && nmo_cli_record_text(rec, "  Attenuation", buf);
            ok = ok && nmo_cli_record_real(rec, "light_power", "  Power",
                                           (double)ls->light_power, "%.4f");
        }
    }
    return ok;
}

static int entity_show_run(nmo_cmd_ctx_t *ctx, const entity_show_args_t *args)
{
    if (ctx == NULL || args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_object_t *obj = NULL;
    nmo_object_id_t obj_id = 0;
    int rc = nmo_core_resolve_one_object(ctx, &args->selector, &obj, &obj_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo entity show [--id <id> | --name <name> | <id>] <file>\n");
        return rc;
    }

    const nmo_3dentity_state_t *es =
        (const nmo_3dentity_state_t *)nmo_object_get_state(obj);

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec || !entity_show_build_record(ctx, rec, obj, obj_id)) {
        nmo_cli_record_free(rec);
        fprintf(stderr, "Error: Out of memory while describing entity %u\n", obj_id);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_record_to_json(rec, doc, data);
        nmo_cli_record_free(rec);
        return nmo_cmd_ctx_json_end(ctx, doc, data, "entity.show");
    }

    nmo_cli_print_heading(ctx->out, "3D Entity Details", ctx->colorize);
    nmo_cli_record_print_kv(rec, ctx->out, 20, ctx->colorize);
    if (!es) {
        fprintf(ctx->out, "\n  (no deserialized state)\n");
    }
    nmo_cli_record_free(rec);
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

    nmo_3dentity_state_t *estate = (nmo_3dentity_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            nmo_context_get_type_registry(c->ctx), obj, CKPGUID_3DENTITY);
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
    nmo_workspace_edit_t *edit = NULL;
    nmo_status_t rc = nmo_workspace_edit_begin(c->workspace, "entity set-position", &edit);
    if (rc == NMO_OK) {
        rc = nmo_entity_edit_set_world_matrix(edit, args->object_id, matrix);
    }
    if (rc != NMO_OK) {
        if (edit) {
            nmo_workspace_edit_rollback(edit);
        }
        fprintf(stderr, "Error: Failed to set position: %s\n", nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (dry_run) {
        nmo_workspace_edit_rollback(edit);
    } else {
        rc = nmo_workspace_edit_commit(edit);
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
 * entity set-parent - Set parent entity through session edit
 * ============================================================================ */

typedef struct entity_set_parent_args {
    nmo_core_object_selector_t selector;
    uint32_t object_id;
    uint32_t parent_id;
} entity_set_parent_args_t;

static int entity_set_parent_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    entity_set_parent_args_t *args = (entity_set_parent_args_t *)user_data;
    if (c == NULL || args == NULL) {
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

    nmo_workspace_edit_t *edit = NULL;
    nmo_status_t rc = nmo_workspace_edit_begin(c->workspace, "entity set-parent", &edit);
    if (rc == NMO_OK) {
        rc = nmo_entity_edit_set_parent(edit, args->object_id, args->parent_id);
    }
    if (rc != NMO_OK) {
        if (edit != NULL) {
            nmo_workspace_edit_rollback(edit);
        }
        fprintf(stderr, "Error: Failed to set parent: %s\n", nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (dry_run) {
        nmo_workspace_edit_rollback(edit);
    } else {
        rc = nmo_workspace_edit_commit(edit);
        if (rc != NMO_OK) {
            fprintf(stderr, "Error: Failed to commit edit: %s\n", nmo_error_string(rc));
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
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
                if (parse_color_rgba(value, &parsed) != NMO_OK) {
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
