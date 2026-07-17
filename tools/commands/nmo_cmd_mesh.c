/**
 * @file nmo_cmd_mesh.c
 * @brief CLI mesh command group implementation
 */

#include "nmo_cmd_mesh.h"
#include "nmo_cmd_object_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_write.h"
#include "../nmo_cli_output.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "runtime/nmo_context.h"
#include "document/nmo_document_save.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_asset_edit.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/nmo_object_struct_defs.h"
#include "format/nmo_obj_parser.h"
#include "core/nmo_arena.h"
#include "core/nmo_string.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#define NMO_MESH_PATH_SEP '\\'
#else
#include <sys/stat.h>
#define NMO_MESH_PATH_SEP '/'
#endif

static int nmo_cmd_mesh_export_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

int nmo_cmd_mesh_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: mesh list|show|export ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "list") == 0 || strcmp(argv[0], "ls") == 0) {
        return nmo_cmd_object_list_class_in_session(ctx, argc, argv, "CKMesh");
    }
    if (strcmp(argv[0], "show") == 0 || strcmp(argv[0], "s") == 0) {
        return nmo_cmd_object_show_class_in_session(
            ctx, argc, argv, NMO_CID_MESH, "CKMesh");
    }
    if (strcmp(argv[0], "export") == 0 || strcmp(argv[0], "x") == 0) {
        return nmo_cmd_mesh_export_in_session(ctx, argc, argv);
    }

    fprintf(stderr, "Unsupported mesh read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
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

static int mesh_ensure_dir(const char *dir_path) {
    if (!dir_path || !*dir_path) return -1;
#ifdef _WIN32
    if (_mkdir(dir_path) == 0) return 0;
#else
    if (mkdir(dir_path, 0755) == 0) return 0;
#endif
    if (errno == EEXIST) return 0;
    return -1;
}

static char *mesh_join_path(const char *dir, const char *file) {
    if (!dir || !file) return NULL;
    size_t dir_len = strlen(dir);
    size_t file_len = strlen(file);
    size_t need_sep = (dir_len > 0 &&
                       dir[dir_len - 1] != (char)NMO_MESH_PATH_SEP) ? 1u : 0u;
    size_t total = dir_len + need_sep + file_len + 1u;
    char *out = (char *)malloc(total);
    if (!out) return NULL;
    memcpy(out, dir, dir_len);
    size_t pos = dir_len;
    if (need_sep) out[pos++] = (char)NMO_MESH_PATH_SEP;
    memcpy(out + pos, file, file_len);
    out[pos + file_len] = '\0';
    return out;
}

/* Filename sanitization is provided by nmo_sanitize_filename() from core/nmo_string.h. */

static void argb_to_rgb_float(uint32_t argb, float *r, float *g, float *b) {
    *r = (float)((argb >> 16) & 0xFF) / 255.0f;
    *g = (float)((argb >> 8)  & 0xFF) / 255.0f;
    *b = (float)((argb)       & 0xFF) / 255.0f;
}

typedef struct mesh_list_json_data {
    yyjson_mut_doc *doc;
    yyjson_mut_val *arr;
    uint32_t found;
} mesh_list_json_data_t;

typedef struct mesh_list_table_data {
    nmo_cli_table_t *table;
    uint32_t found;
} mesh_list_table_data_t;

static int mesh_list_json_visitor(size_t index,
                                  nmo_object_t *obj,
                                  const nmo_cmd_ctx_t *c,
                                  void *user)
{
    (void)index;
    (void)c;
    mesh_list_json_data_t *data = (mesh_list_json_data_t *)user;
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

    const nmo_mesh_state_t *ms =
        (const nmo_mesh_state_t *)nmo_object_get_state(obj);
    if (ms) {
        yyjson_mut_obj_add_uint(doc, item, "vertices", ms->vertex_count);
        yyjson_mut_obj_add_uint(doc, item, "faces", ms->face_count);
        yyjson_mut_obj_add_uint(doc, item, "materials",
                                ms->material_group_count);
    }

    yyjson_mut_arr_add_val(data->arr, item);
    data->found++;
    return 0;
}

static int mesh_list_table_visitor(size_t index,
                                   nmo_object_t *obj,
                                   const nmo_cmd_ctx_t *c,
                                   void *user)
{
    (void)index;
    (void)c;
    mesh_list_table_data_t *data = (mesh_list_table_data_t *)user;
    if (obj == NULL || data == NULL || data->table == NULL) {
        return 0;
    }

    char id_buf[16];
    snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

    const char *name = nmo_object_get_name(obj);
    if (!name || !name[0]) name = "-";

    char vert_buf[16], face_buf[16], mat_buf[16];

    const nmo_mesh_state_t *ms =
        (const nmo_mesh_state_t *)nmo_object_get_state(obj);
    if (ms) {
        snprintf(vert_buf, sizeof(vert_buf), "%u", ms->vertex_count);
        snprintf(face_buf, sizeof(face_buf), "%u", ms->face_count);
        snprintf(mat_buf, sizeof(mat_buf), "%u", ms->material_group_count);
    } else {
        snprintf(vert_buf, sizeof(vert_buf), "-");
        snprintf(face_buf, sizeof(face_buf), "-");
        snprintf(mat_buf, sizeof(mat_buf), "-");
    }

    const char *cells[] = {id_buf, name, vert_buf, face_buf, mat_buf};
    nmo_cli_table_add_row(data->table, cells, 5);
    data->found++;
    return 0;
}

/* ============================================================================
 * mesh list
 * ============================================================================ */

int nmo_cmd_mesh_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_query_t query = {0};
    nmo_core_query_set_class_id(&query, NMO_CID_MESH, false);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        mesh_list_json_data_t jd = { .doc = doc, .arr = arr };
        rc = nmo_core_object_query_run(&c, &query,
                                       mesh_list_json_visitor, &jd, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return nmo_cmd_ctx_done(&c, rc);
        }

        yyjson_mut_obj_add_uint(doc, data, "count", jd.found);
        yyjson_mut_obj_add_val(doc, data, "meshes", arr);
        nmo_cmd_ctx_json_end(&c, doc, data, "mesh.list");
    } else {
        static const nmo_cli_table_col_t columns[] = {
            {"ID",        NMO_CLI_ALIGN_RIGHT, 6,  0},
            {"NAME",      NMO_CLI_ALIGN_LEFT,  24, 50},
            {"VERTICES",  NMO_CLI_ALIGN_RIGHT, 8,  0},
            {"FACES",     NMO_CLI_ALIGN_RIGHT, 8,  0},
            {"MATERIALS", NMO_CLI_ALIGN_RIGHT, 9,  0},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));
        mesh_list_table_data_t td = { .table = &table };
        rc = nmo_core_object_query_run(&c, &query,
                                       mesh_list_table_visitor, &td, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_cli_table_free(&table);
            return nmo_cmd_ctx_done(&c, rc);
        }

        fprintf(c.out, "Meshes: %u\n\n", td.found);
        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * mesh show
 * ============================================================================ */

int nmo_cmd_mesh_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--id",   "-i", NMO_OPT_UINT,   "Mesh object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Mesh object name"},
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
        .required_base_class = NMO_CID_MESH,
        .selector_label = "Mesh",
        .type_label = "CKMesh",
    };
    nmo_object_t *obj = NULL;
    nmo_object_id_t obj_id = 0;
    rc = nmo_core_resolve_one_object(&c, &selector, &obj, &obj_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo mesh show [--id <id> | --name <name> | <id>] <file>\n");
        return nmo_cmd_ctx_done(&c, rc);
    }

    const char *name = nmo_object_get_name(obj);
    const nmo_mesh_state_t *ms =
        (const nmo_mesh_state_t *)nmo_object_get_state(obj);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "id", obj_id);
        nmo_cli_json_add_str_safe(doc, data, "name",
                                  (name && name[0]) ? name : "");

        if (ms) {
            yyjson_mut_obj_add_uint(doc, data, "vertex_count", ms->vertex_count);
            yyjson_mut_obj_add_uint(doc, data, "face_count", ms->face_count);
            yyjson_mut_obj_add_uint(doc, data, "line_count", ms->line_count);
            yyjson_mut_obj_add_uint(doc, data, "material_group_count",
                                    ms->material_group_count);
            yyjson_mut_obj_add_uint(doc, data, "flags", ms->flags);
            yyjson_mut_obj_add_bool(doc, data, "has_progressive_mesh",
                                    ms->has_progressive_mesh);
            yyjson_mut_obj_add_real(doc, data, "radius", (double)ms->radius);

            /* Bary center */
            yyjson_mut_val *bc = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_real(doc, bc, "x", (double)ms->bary_center.x);
            yyjson_mut_obj_add_real(doc, bc, "y", (double)ms->bary_center.y);
            yyjson_mut_obj_add_real(doc, bc, "z", (double)ms->bary_center.z);
            yyjson_mut_obj_add_val(doc, data, "bary_center", bc);

            /* Bounding box */
            yyjson_mut_val *bmin = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_real(doc, bmin, "x", (double)ms->local_box_min.x);
            yyjson_mut_obj_add_real(doc, bmin, "y", (double)ms->local_box_min.y);
            yyjson_mut_obj_add_real(doc, bmin, "z", (double)ms->local_box_min.z);
            yyjson_mut_obj_add_val(doc, data, "local_box_min", bmin);

            yyjson_mut_val *bmax = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_real(doc, bmax, "x", (double)ms->local_box_max.x);
            yyjson_mut_obj_add_real(doc, bmax, "y", (double)ms->local_box_max.y);
            yyjson_mut_obj_add_real(doc, bmax, "z", (double)ms->local_box_max.z);
            yyjson_mut_obj_add_val(doc, data, "local_box_max", bmax);

            /* Material groups */
            if (ms->material_group_count > 0 && ms->material_groups) {
                yyjson_mut_val *mats = yyjson_mut_arr(doc);
                for (uint32_t gi = 0; gi < ms->material_group_count; ++gi) {
                    yyjson_mut_val *mg = yyjson_mut_obj(doc);
                    nmo_object_id_t mid =
                        nmo_ref_runtime_id(&ms->material_groups[gi].material);
                    yyjson_mut_obj_add_uint(doc, mg, "material_id", mid);
                    const char *mname = resolve_name(&c, mid);
                    if (mname && mname[0]) {
                        nmo_cli_json_add_str_safe(doc, mg, "material_name", mname);
                    }
                    yyjson_mut_arr_add_val(mats, mg);
                }
                yyjson_mut_obj_add_val(doc, data, "material_groups", mats);
            }
        } else {
            yyjson_mut_obj_add_null(doc, data, "state");
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "mesh.show");
    } else {
        nmo_cli_print_heading(c.out, "Mesh Details", c.colorize);

        char buf[128];
        snprintf(buf, sizeof(buf), "#%u (%s)", obj_id,
                 (name && name[0]) ? name : "(unnamed)");
        nmo_cli_print_kv(c.out, "ID / Name", buf, 22, c.colorize);

        if (!ms) {
            fprintf(c.out, "\n  (no deserialized state)\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
        }

        snprintf(buf, sizeof(buf), "%u", ms->vertex_count);
        nmo_cli_print_kv(c.out, "Vertices", buf, 22, c.colorize);

        snprintf(buf, sizeof(buf), "%u", ms->face_count);
        nmo_cli_print_kv(c.out, "Faces", buf, 22, c.colorize);

        snprintf(buf, sizeof(buf), "%u", ms->line_count);
        nmo_cli_print_kv(c.out, "Lines", buf, 22, c.colorize);

        snprintf(buf, sizeof(buf), "%u", ms->material_group_count);
        nmo_cli_print_kv(c.out, "Material Groups", buf, 22, c.colorize);

        snprintf(buf, sizeof(buf), "0x%08X", ms->flags);
        nmo_cli_print_kv(c.out, "Flags", buf, 22, c.colorize);

        nmo_cli_print_kv(c.out, "Progressive Mesh",
                         ms->has_progressive_mesh ? "yes" : "no", 22, c.colorize);

        snprintf(buf, sizeof(buf), "%.4f", (double)ms->radius);
        nmo_cli_print_kv(c.out, "Radius", buf, 22, c.colorize);

        snprintf(buf, sizeof(buf), "(%.4f, %.4f, %.4f)",
                 (double)ms->bary_center.x,
                 (double)ms->bary_center.y,
                 (double)ms->bary_center.z);
        nmo_cli_print_kv(c.out, "Bary Center", buf, 22, c.colorize);

        snprintf(buf, sizeof(buf), "(%.4f, %.4f, %.4f)",
                 (double)ms->local_box_min.x,
                 (double)ms->local_box_min.y,
                 (double)ms->local_box_min.z);
        nmo_cli_print_kv(c.out, "Local Box Min", buf, 22, c.colorize);

        snprintf(buf, sizeof(buf), "(%.4f, %.4f, %.4f)",
                 (double)ms->local_box_max.x,
                 (double)ms->local_box_max.y,
                 (double)ms->local_box_max.z);
        nmo_cli_print_kv(c.out, "Local Box Max", buf, 22, c.colorize);

        /* Material groups */
        if (ms->material_group_count > 0 && ms->material_groups) {
            fprintf(c.out, "\nMaterial Groups:\n");
            for (uint32_t gi = 0; gi < ms->material_group_count; ++gi) {
                nmo_object_id_t mid =
                    nmo_ref_runtime_id(&ms->material_groups[gi].material);
                const char *mname = resolve_name(&c, mid);
                if (mid && mname && mname[0]) {
                    fprintf(c.out, "  [%u] #%u (%s)\n", gi, mid, mname);
                } else if (mid) {
                    fprintf(c.out, "  [%u] #%u\n", gi, mid);
                } else {
                    fprintf(c.out, "  [%u] (none)\n", gi);
                }
            }
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * mesh export - OBJ + MTL writer
 * ============================================================================ */

static int write_mtl_file(const nmo_cmd_ctx_t *c,
                          const nmo_mesh_state_t *ms,
                          const char *mtl_path) {
    FILE *f = fopen(mtl_path, "w");
    if (!f) {
        fprintf(stderr, "Error: Cannot open '%s' for writing (%s)\n",
                mtl_path, strerror(errno));
        return -1;
    }

    fprintf(f, "# Exported by nmo\n\n");

    for (uint32_t gi = 0; gi < ms->material_group_count; ++gi) {
        nmo_object_id_t mid =
            nmo_ref_runtime_id(&ms->material_groups[gi].material);
        const char *mat_name = NULL;
        const nmo_material_state_t *mat = NULL;

        if (mid) {
            nmo_object_t *mat_obj = nmo_core_find_by_id(c, mid);
            if (mat_obj) {
                mat_name = nmo_object_get_name(mat_obj);
                mat = (const nmo_material_state_t *)nmo_object_get_state(mat_obj);
            }
        }

        if (!mat_name || !mat_name[0]) {
            char fallback[32];
            snprintf(fallback, sizeof(fallback), "material_%u", gi);
            fprintf(f, "newmtl %s\n", fallback);
        } else {
            fprintf(f, "newmtl %s\n", mat_name);
        }

        if (mat) {
            float r, g, b;
            argb_to_rgb_float(mat->diffuse_color, &r, &g, &b);
            fprintf(f, "Kd %.6f %.6f %.6f\n", (double)r, (double)g, (double)b);
            argb_to_rgb_float(mat->ambient_color, &r, &g, &b);
            fprintf(f, "Ka %.6f %.6f %.6f\n", (double)r, (double)g, (double)b);
            argb_to_rgb_float(mat->specular_color, &r, &g, &b);
            fprintf(f, "Ks %.6f %.6f %.6f\n", (double)r, (double)g, (double)b);
            fprintf(f, "Ns %.4f\n", (double)mat->specular_power);
        } else {
            fprintf(f, "Kd 0.800000 0.800000 0.800000\n");
            fprintf(f, "Ka 0.200000 0.200000 0.200000\n");
            fprintf(f, "Ks 0.000000 0.000000 0.000000\n");
            fprintf(f, "Ns 0.0000\n");
        }
        fprintf(f, "\n");
    }

    fclose(f);
    return 0;
}

static int write_obj_file(const nmo_cmd_ctx_t *c,
                          const nmo_mesh_state_t *ms,
                          const char *obj_path,
                          const char *mtl_filename) {
    FILE *f = fopen(obj_path, "w");
    if (!f) {
        fprintf(stderr, "Error: Cannot open '%s' for writing (%s)\n",
                obj_path, strerror(errno));
        return -1;
    }

    fprintf(f, "# Exported by nmo\n");
    if (mtl_filename) {
        fprintf(f, "mtllib %s\n", mtl_filename);
    }
    fprintf(f, "\n");

    /* Vertices: position */
    if (ms->vertices) {
        for (uint32_t vi = 0; vi < ms->vertex_count; ++vi) {
            const nmo_vertex_t *v = &ms->vertices[vi];
            fprintf(f, "v %.6f %.6f %.6f\n",
                    (double)v->position.x,
                    (double)v->position.y,
                    (double)v->position.z);
        }
        fprintf(f, "\n");

        /* Texture coordinates */
        for (uint32_t vi = 0; vi < ms->vertex_count; ++vi) {
            const nmo_vertex_t *v = &ms->vertices[vi];
            fprintf(f, "vt %.6f %.6f\n",
                    (double)v->uv.x,
                    (double)v->uv.y);
        }
        fprintf(f, "\n");

        /* Normals */
        for (uint32_t vi = 0; vi < ms->vertex_count; ++vi) {
            const nmo_vertex_t *v = &ms->vertices[vi];
            fprintf(f, "vn %.6f %.6f %.6f\n",
                    (double)v->normal.x,
                    (double)v->normal.y,
                    (double)v->normal.z);
        }
        fprintf(f, "\n");
    }

    /* Faces grouped by material */
    if (ms->faces && ms->face_vertex_indices) {
        /* Build per-material-group face lists */
        uint32_t num_groups = ms->material_group_count > 0
                            ? ms->material_group_count : 1;

        for (uint32_t gi = 0; gi < num_groups; ++gi) {
            /* Emit material name */
            if (ms->material_group_count > 0 && ms->material_groups) {
                nmo_object_id_t mid =
                    nmo_ref_runtime_id(&ms->material_groups[gi].material);
                const char *mname = resolve_name(c, mid);
                if (mname && mname[0]) {
                    fprintf(f, "usemtl %s\n", mname);
                } else {
                    fprintf(f, "usemtl material_%u\n", gi);
                }
            }

            /* Output faces for this group */
            for (uint32_t fi = 0; fi < ms->face_count; ++fi) {
                if (ms->faces[fi].material_group_idx != (uint16_t)gi) continue;

                /* 3 vertex indices per face (interleaved in face_vertex_indices) */
                uint16_t vi0 = ms->face_vertex_indices[fi * 3 + 0];
                uint16_t vi1 = ms->face_vertex_indices[fi * 3 + 1];
                uint16_t vi2 = ms->face_vertex_indices[fi * 3 + 2];

                /* OBJ is 1-based; since vertices are interleaved
                   (pos/uv/normal share the same index) */
                fprintf(f, "f %u/%u/%u %u/%u/%u %u/%u/%u\n",
                        (unsigned)(vi0 + 1), (unsigned)(vi0 + 1), (unsigned)(vi0 + 1),
                        (unsigned)(vi1 + 1), (unsigned)(vi1 + 1), (unsigned)(vi1 + 1),
                        (unsigned)(vi2 + 1), (unsigned)(vi2 + 1), (unsigned)(vi2 + 1));
            }
            fprintf(f, "\n");
        }
    }

    fclose(f);
    return 0;
}

static int export_single_mesh(const nmo_cmd_ctx_t *c,
                              nmo_object_t *obj,
                              const char *out_dir,
                              FILE *out_stream,
                              bool is_json,
                              yyjson_mut_doc *doc,
                              yyjson_mut_val *entries) {
    nmo_object_id_t id = nmo_object_get_id(obj);
    const char *name = nmo_object_get_name(obj);
    const nmo_mesh_state_t *ms =
        (const nmo_mesh_state_t *)nmo_object_get_state(obj);

    if (!ms || ms->vertex_count == 0) {
        if (is_json && doc && entries) {
            yyjson_mut_val *e = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, e, "id", id);
            yyjson_mut_obj_add_str(doc, e, "status", "skip");
            yyjson_mut_obj_add_str(doc, e, "reason", "no_geometry");
            yyjson_mut_arr_add_val(entries, e);
        } else {
            fprintf(out_stream, "  [SKIP] %u %s -> no geometry\n",
                    id, (name && name[0]) ? name : "(unnamed)");
        }
        return 0;
    }

    char safe_name[256];
    nmo_sanitize_filename(safe_name, sizeof(safe_name), name, id);

    /* Build file paths */
    char obj_fname[280];
    char mtl_fname[280];
    snprintf(obj_fname, sizeof(obj_fname), "%s.obj", safe_name);
    snprintf(mtl_fname, sizeof(mtl_fname), "%s.mtl", safe_name);

    char *obj_path = mesh_join_path(out_dir, obj_fname);
    char *mtl_path = mesh_join_path(out_dir, mtl_fname);
    if (!obj_path || !mtl_path) {
        free(obj_path);
        free(mtl_path);
        return -1;
    }

    /* Write MTL if material groups exist */
    if (ms->material_group_count > 0) {
        if (write_mtl_file(c, ms, mtl_path) < 0) {
            free(obj_path);
            free(mtl_path);
            return -1;
        }
    }

    /* Write OBJ */
    const char *mtl_ref = ms->material_group_count > 0 ? mtl_fname : NULL;
    if (write_obj_file(c, ms, obj_path, mtl_ref) < 0) {
        free(obj_path);
        free(mtl_path);
        return -1;
    }

    if (is_json && doc && entries) {
        yyjson_mut_val *e = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, e, "id", id);
        nmo_cli_json_add_str_safe(doc, e, "name", name ? name : "");
        yyjson_mut_obj_add_str(doc, e, "obj_file", obj_path);
        yyjson_mut_obj_add_str(doc, e, "mtl_file", mtl_path);
        yyjson_mut_obj_add_uint(doc, e, "vertices", ms->vertex_count);
        yyjson_mut_obj_add_uint(doc, e, "faces", ms->face_count);
        yyjson_mut_obj_add_str(doc, e, "status", "ok");
        yyjson_mut_arr_add_val(entries, e);
    } else {
        fprintf(out_stream, "  [OK]   %u %s -> %s (%u verts, %u faces)\n",
                id, (name && name[0]) ? name : "(unnamed)",
                obj_fname, ms->vertex_count, ms->face_count);
    }

    free(obj_path);
    free(mtl_path);
    return 0;
}

typedef struct mesh_export_data {
    const char *out_dir;
    yyjson_mut_doc *doc;
    yyjson_mut_val *entries;
    uint32_t exported;
    uint32_t errors;
} mesh_export_data_t;

typedef struct mesh_export_args {
    const char *out_dir;
    bool has_id;
    uint32_t id;
    const char *name;
    const char *positional_id;
    bool export_all;
} mesh_export_args_t;

static int mesh_export_parse(int argc, char **argv, bool expect_file_operand,
                             mesh_export_args_t *args, const char *usage) {
    memset(args, 0, sizeof(*args));

    static const nmo_opt_def_t opts[] = {
        {"--out-dir", "-d", NMO_OPT_STRING, "Output directory (required)"},
        {"--id",      NULL,  NMO_OPT_UINT,   "Export single mesh by ID"},
        {"--name",    "-n",  NMO_OPT_STRING, "Export single mesh by name"},
        {"--all",     "-a", NMO_OPT_FLAG,   "Export all meshes"},
    };
    enum { OPT_OUT_DIR, OPT_ID, OPT_NAME, OPT_ALL, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    args->out_dir = vals[OPT_OUT_DIR].present ? vals[OPT_OUT_DIR].val.str : NULL;
    args->has_id = vals[OPT_ID].present;
    args->id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0;
    args->name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL;
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
        fprintf(stderr, "Error: Missing --out-dir\n");
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

static int mesh_export_visitor(size_t index,
                               nmo_object_t *obj,
                               const nmo_cmd_ctx_t *c,
                               void *user)
{
    (void)index;
    mesh_export_data_t *data = (mesh_export_data_t *)user;
    if (obj == NULL || data == NULL) {
        return 0;
    }

    int ret = export_single_mesh(c, obj, data->out_dir, c->out,
                                 c->is_json, data->doc, data->entries);
    if (ret < 0) {
        data->errors++;
    } else {
        data->exported++;
    }
    return 0;
}

static int mesh_export_run(nmo_cmd_ctx_t *ctx, const mesh_export_args_t *args,
                           bool close_ctx, const char *usage) {
    nmo_cmd_ctx_t c = *ctx;

    if (mesh_ensure_dir(args->out_dir) < 0) {
        fprintf(stderr, "Error: Cannot create directory '%s' (%s)\n",
                args->out_dir, strerror(errno));
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR)
                         : NMO_CLI_EXIT_IO_ERROR;
    }

    uint32_t exported = 0;
    uint32_t errors = 0;

    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *data = NULL;
    yyjson_mut_val *entries = NULL;

    if (c.is_json) {
        doc = nmo_cmd_ctx_json_begin(&c);
        data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, data, "out_dir", args->out_dir);
        entries = yyjson_mut_arr(doc);
    } else {
        fprintf(c.out, "Exporting meshes to: %s\n", args->out_dir);
    }

    nmo_object_query_t query = {0};
    nmo_core_query_set_class_id(&query, NMO_CID_MESH, false);
    if (!args->export_all) {
        nmo_core_object_selector_t selector = {
            .has_id = args->has_id,
            .id = args->id,
            .positional_id = args->positional_id,
            .name = args->name,
            .required_base_class = NMO_CID_MESH,
            .selector_label = "Mesh",
            .type_label = "CKMesh",
        };
        nmo_object_t *selected = NULL;
        nmo_object_id_t selected_id = 0;
        int rc = nmo_core_resolve_one_object(&c, &selector, &selected, &selected_id);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            fprintf(stderr, "Usage: %s\n", usage);
            return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
        }
        query.object_id = selected_id;
    }

    mesh_export_data_t export_data = {
        .out_dir = args->out_dir,
        .doc = doc,
        .entries = entries
    };
    int rc = nmo_core_object_query_run(&c, &query, mesh_export_visitor, &export_data, NULL);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
    }
    exported = export_data.exported;
    errors = export_data.errors;

    int exit_code = (errors > 0 && exported == 0)
                  ? NMO_CLI_EXIT_IO_ERROR : NMO_CLI_EXIT_SUCCESS;

    if (c.is_json) {
        yyjson_mut_obj_add_uint(doc, data, "exported", exported);
        yyjson_mut_obj_add_uint(doc, data, "errors", errors);
        yyjson_mut_obj_add_val(doc, data, "entries", entries);
        nmo_cmd_ctx_json_end(&c, doc, data, "mesh.export");
    } else {
        fprintf(c.out, "\nExported: %u, Errors: %u\n", exported, errors);
    }

    return close_ctx ? nmo_cmd_ctx_done(&c, exit_code) : exit_code;
}

int nmo_cmd_mesh_export(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    mesh_export_args_t args;
    const char *usage = "nmo mesh export --out-dir <dir> [--all | --id <id> | --name <name> | <id>] <file>";
    int rc = mesh_export_parse(argc, argv, true, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    return mesh_export_run(&c, &args, true, usage);
}

static int nmo_cmd_mesh_export_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv) {
    mesh_export_args_t args;
    const char *usage = "mesh export --out-dir <dir> [--all | --id <id> | --name <name> | <id>]";
    int rc = mesh_export_parse(argc, argv, false, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    return mesh_export_run(ctx, &args, false, usage);
}

/* ============================================================================
 * mesh import - OBJ -> NMO mesh
 * ============================================================================ */

static uint8_t *read_file_to_buffer(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }

    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);

    if (rd != (size_t)sz) { free(buf); return NULL; }
    *out_size = (size_t)sz;
    return buf;
}

int nmo_cmd_mesh_import(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output NMO file (required unless --dry-run)"},
        {"--replace", NULL,  NMO_OPT_STRING, "Replace existing mesh by ID"},
        {"--replace-name", NULL, NMO_OPT_STRING, "Replace existing mesh by exact name"},
        {"--name",    "-n", NMO_OPT_STRING, "Mesh name (default: filename)"},
        {"--dry-run", NULL,  NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_REPLACE, OPT_REPLACE_NAME, OPT_NAME, OPT_DRYRUN, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path  = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    const char *replace_str  = vals[OPT_REPLACE].present ? vals[OPT_REPLACE].val.str : NULL;
    const char *replace_name = vals[OPT_REPLACE_NAME].present ? vals[OPT_REPLACE_NAME].val.str : NULL;
    const char *mesh_name    = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;

    /* Positional: <obj-file> <nmo-file> */
    const char *obj_file_path = NULL;
    const char *nmo_file_path = NULL;

    if (r.pos_count >= 2) {
        obj_file_path = r.pos_args[0];
        nmo_file_path = r.pos_args[1];
    } else if (r.pos_count == 1) {
        obj_file_path = r.pos_args[0];
    }

    if (!obj_file_path) {
        fprintf(stderr, "Error: Missing OBJ file path\n");
        fprintf(stderr, "Usage: nmo mesh import <obj-file> <nmo-file> -o <output> [--dry-run]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!nmo_file_path) {
        fprintf(stderr, "Error: Missing NMO file path\n");
        fprintf(stderr, "Usage: nmo mesh import <obj-file> <nmo-file> -o <output> [--dry-run]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!dry_run && (!output_path || !*output_path)) {
        fprintf(stderr, "Error: Missing --output/-o\n");
        fprintf(stderr, "Usage: nmo mesh import <obj-file> <nmo-file> -o <output> [--dry-run]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Read OBJ file */
    size_t obj_size = 0;
    uint8_t *obj_buf = read_file_to_buffer(obj_file_path, &obj_size);
    if (!obj_buf) {
        fprintf(stderr, "Error: Cannot read OBJ file '%s' (%s)\n",
                obj_file_path, strerror(errno));
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Open NMO session */
    nmo_cmd_ctx_t c;
    int rc = nmo_cli_write_init_ctx(&c, nmo_file_path, global);
    if (rc) { free(obj_buf); return rc; }

    nmo_arena_t *arena = nmo_tool_owner_arena(c.workspace);

    /* Parse OBJ */
    nmo_obj_data_t obj_data;
    nmo_status_t st = nmo_obj_parse(arena, (const char *)obj_buf, obj_size,
                                    &obj_data);
    free(obj_buf);

    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to parse OBJ file '%s'\n", obj_file_path);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    if (obj_data.face_count == 0 && obj_data.line_count == 0) {
        fprintf(stderr, "Error: OBJ file has no faces or lines\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_asset_mesh_material_binding_t *material_bindings = NULL;
    if (obj_data.material_name_count > 0) {
        material_bindings = (nmo_asset_mesh_material_binding_t *)nmo_arena_alloc(
            arena,
            obj_data.material_name_count * sizeof(*material_bindings),
            alignof(nmo_asset_mesh_material_binding_t));
        if (!material_bindings) {
            fprintf(stderr, "Error: Out of memory\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        nmo_object_repository_t *repo = nmo_tool_owner_repository(c.workspace);
        for (size_t mi = 0; mi < obj_data.material_name_count; ++mi) {
            const char *material_name =
                obj_data.material_names ? obj_data.material_names[mi] : NULL;
            material_bindings[mi].name = material_name;
            material_bindings[mi].material_id = NMO_OBJECT_ID_NONE;

            if (material_name && *material_name) {
                nmo_object_t *material =
                    nmo_object_repository_find_by_name(repo, material_name);
                if (material) {
                    if (nmo_object_get_class_id(material) != NMO_CID_MATERIAL) {
                        fprintf(stderr,
                                "Error: OBJ material '%s' resolves to a non-material object\n",
                                material_name);
                        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
                    }
                    material_bindings[mi].material_id = nmo_object_get_id(material);
                }
            }
        }
    }

    nmo_asset_mesh_import_options_t import_options = {
        .materials = material_bindings,
        .material_count = obj_data.material_name_count,
    };

    /* Find or create mesh object */
    nmo_object_t *mesh_obj = NULL;
    nmo_object_id_t created_mesh_id = 0;

    if (replace_str || replace_name) {
        nmo_core_object_selector_t selector = {
            .positional_id = replace_str,
            .name = replace_name,
            .required_base_class = NMO_CID_MESH,
            .selector_label = "Mesh",
            .type_label = "CKMesh",
        };
        nmo_object_id_t selected_id = 0;
        int resolve_rc = nmo_core_resolve_one_object(&c, &selector, &mesh_obj, &selected_id);
        if (resolve_rc != NMO_CLI_EXIT_SUCCESS) {
            fprintf(stderr, "Usage: nmo mesh import <obj-file> <nmo-file> -o <output> [--replace <id> | --replace-name <name>] [--dry-run]\n");
            return nmo_cmd_ctx_done(&c, resolve_rc);
        }
    }

    if (!mesh_obj) {
        /* Derive name from --name option or OBJ filename */
        const char *create_name = mesh_name;
        char name_buf[256];
        if (!create_name) {
            /* Extract basename without extension from OBJ path */
            const char *base = obj_file_path;
            const char *p;
            for (p = obj_file_path; *p; p++) {
                if (*p == '/' || *p == '\\') base = p + 1;
            }
            size_t blen = strlen(base);
            const char *dot = NULL;
            for (p = base + blen; p > base; p--) {
                if (*(p - 1) == '.') { dot = p - 1; break; }
            }
            size_t namelen = dot ? (size_t)(dot - base) : blen;
            if (namelen >= sizeof(name_buf)) namelen = sizeof(name_buf) - 1;
            memcpy(name_buf, base, namelen);
            name_buf[namelen] = '\0';
            create_name = name_buf;
        }

        nmo_object_id_t new_id = 0;
        nmo_runtime_report_t report;
        memset(&report, 0, sizeof(report));
        int create_rc = nmo_tool_owner_create_object(
            c.workspace, NMO_CID_MESH, create_name, NMO_GUID_NULL,
            &new_id, &report);
        if (create_rc != NMO_OK) {
            fprintf(stderr, "Error: Failed to create mesh object: %s\n",
                    nmo_error_string(create_rc));
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        mesh_obj = nmo_core_find_by_id(&c, new_id);
        if (!mesh_obj) {
            fprintf(stderr, "Error: Created mesh object %u not found\n", new_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        created_mesh_id = new_id;
    }

    nmo_workspace_edit_t *edit = NULL;
    nmo_status_t edit_rc = nmo_workspace_edit_begin(c.workspace, "mesh.import", &edit);
    if (edit_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin mesh edit: %s\n",
                nmo_error_string(edit_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    if (created_mesh_id != 0) {
        edit_rc = nmo_workspace_edit_track_created_object(edit, created_mesh_id);
        if (edit_rc != NMO_OK) {
            nmo_workspace_edit_rollback(edit);
            fprintf(stderr, "Error: Failed to track created mesh object: %s\n",
                    nmo_error_string(edit_rc));
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
    }

    nmo_mesh_state_t *ms =
        (nmo_mesh_state_t *)nmo_object_get_state(mesh_obj);
    if (!ms) {
        nmo_status_t alloc_rc = nmo_object_alloc_state(mesh_obj, sizeof(nmo_mesh_state_t));
        if (alloc_rc != NMO_OK) {
            nmo_workspace_edit_rollback(edit);
            fprintf(stderr, "Error: Failed to allocate mesh state\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        ms = (nmo_mesh_state_t *)nmo_object_get_state(mesh_obj);
        if (!ms) {
            nmo_workspace_edit_rollback(edit);
            fprintf(stderr, "Error: Mesh state allocation failed\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        memset(ms, 0, sizeof(*ms));
    }

    edit_rc = nmo_asset_edit_set_obj_mesh(
        edit,
        nmo_object_get_id(mesh_obj),
        &obj_data,
        &import_options);
    if (edit_rc != NMO_OK) {
        int exit_rc = edit_rc == NMO_ERR_INVALID_ARGUMENT
            ? NMO_CLI_EXIT_ARG_ERROR
            : NMO_CLI_EXIT_INTERNAL_ERROR;
        nmo_workspace_edit_rollback(edit);
        fprintf(stderr, "Error: Failed to import mesh asset: %s\n",
                nmo_error_string(edit_rc));
        return nmo_cmd_ctx_done(&c, exit_rc);
    }

    if (mesh_name) {
        edit_rc =
            nmo_object_edit_rename(edit, nmo_object_get_id(mesh_obj), mesh_name);
        if (edit_rc != NMO_OK) {
            nmo_workspace_edit_rollback(edit);
            fprintf(stderr, "Error: Failed to rename mesh object: %s\n",
                    nmo_error_string(edit_rc));
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
    }

    edit_rc = nmo_workspace_edit_commit(edit);
    if (edit_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to commit mesh edit: %s\n",
                nmo_error_string(edit_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    ms = (nmo_mesh_state_t *)nmo_object_get_state(mesh_obj);
    size_t total_verts = ms ? (size_t)ms->vertex_count : 0u;

    if (!dry_run) {
        int save_rc = nmo_cli_save_document(c.document, output_path, NULL);
        if (save_rc != NMO_CLI_EXIT_SUCCESS) {
            return nmo_cmd_ctx_done(&c, save_rc);
        }
    }

    if (c.is_json) {
        yyjson_mut_doc *jdoc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *jdata = yyjson_mut_obj(jdoc);
        nmo_cli_json_add_bool_safe(jdoc, jdata, "dry_run", dry_run);
        if (output_path) {
            yyjson_mut_obj_add_str(jdoc, jdata, "output", output_path);
        }
        yyjson_mut_obj_add_uint(jdoc, jdata, "vertex_count", (uint64_t)total_verts);
        yyjson_mut_obj_add_uint(jdoc, jdata, "face_count",
                                (uint64_t)obj_data.face_count);
        yyjson_mut_obj_add_str(jdoc, jdata, "status", "ok");
        nmo_cmd_ctx_json_end(&c, jdoc, jdata, "mesh.import");
    } else {
        if (dry_run) {
            fprintf(c.out, "[dry-run] ");
        }
        fprintf(c.out, "Imported %zu vertices, %zu faces from '%s'\n",
                total_verts, obj_data.face_count, obj_file_path);
        if (dry_run) {
            fprintf(c.out, "No output written\n");
        } else {
            fprintf(c.out, "Saved to '%s'\n", output_path);
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

