/**
 * @file nmo_cmd_mesh.c
 * @brief CLI mesh command group implementation
 */

#include "nmo_cmd_mesh.h"
#include "nmo_cmd_object.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_write.h"
#include "../nmo_cli_output.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "session/nmo_session.h"
#include "session/nmo_context.h"
#include "app/nmo_save.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_serialize_context.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/nmo_object_struct_defs.h"
#include "format/nmo_obj_parser.h"
#include "core/nmo_arena.h"
#include "core/nmo_string.h"
#include "type/nmo_type_system.h"
#include "session/nmo_session_edit.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

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
                    nmo_object_id_t mid = ms->material_groups[gi].material_id;
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
                nmo_object_id_t mid = ms->material_groups[gi].material_id;
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
        nmo_object_id_t mid = ms->material_groups[gi].material_id;
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
                nmo_object_id_t mid = ms->material_groups[gi].material_id;
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

static void compute_bounds(const nmo_obj_data_t *obj_data,
                           nmo_vector_t *center,
                           nmo_vector_t *box_min,
                           nmo_vector_t *box_max,
                           float *radius) {
    if (!obj_data->positions || obj_data->pos_count == 0) {
        memset(center, 0, sizeof(*center));
        memset(box_min, 0, sizeof(*box_min));
        memset(box_max, 0, sizeof(*box_max));
        *radius = 0.0f;
        return;
    }

    float minx = obj_data->positions[0];
    float miny = obj_data->positions[1];
    float minz = obj_data->positions[2];
    float maxx = minx, maxy = miny, maxz = minz;

    for (size_t i = 0; i < obj_data->pos_count; i++) {
        float x = obj_data->positions[i * 3 + 0];
        float y = obj_data->positions[i * 3 + 1];
        float z = obj_data->positions[i * 3 + 2];
        if (x < minx) minx = x;
        if (y < miny) miny = y;
        if (z < minz) minz = z;
        if (x > maxx) maxx = x;
        if (y > maxy) maxy = y;
        if (z > maxz) maxz = z;
    }

    box_min->x = minx; box_min->y = miny; box_min->z = minz;
    box_max->x = maxx; box_max->y = maxy; box_max->z = maxz;
    center->x = (minx + maxx) * 0.5f;
    center->y = (miny + maxy) * 0.5f;
    center->z = (minz + maxz) * 0.5f;

    /* Compute bounding sphere radius from center */
    float max_dist_sq = 0.0f;
    for (size_t i = 0; i < obj_data->pos_count; i++) {
        float dx = obj_data->positions[i * 3 + 0] - center->x;
        float dy = obj_data->positions[i * 3 + 1] - center->y;
        float dz = obj_data->positions[i * 3 + 2] - center->z;
        float dist_sq = dx * dx + dy * dy + dz * dz;
        if (dist_sq > max_dist_sq) max_dist_sq = dist_sq;
    }
    *radius = sqrtf(max_dist_sq);
}

typedef struct mesh_import_dedup_slot {
    int32_t pos_idx;
    int32_t uv_idx;
    int32_t normal_idx;
    uint16_t idx_plus1;
} mesh_import_dedup_slot_t;

static uint8_t mesh_import_float_to_u8(float value) {
    if (value <= 0.0f) return 0;
    if (value >= 1.0f) return 255;
    return (uint8_t)(value * 255.0f + 0.5f);
}

static uint32_t mesh_import_rgb_to_argb(const float *rgb) {
    uint32_t r = mesh_import_float_to_u8(rgb[0]);
    uint32_t g = mesh_import_float_to_u8(rgb[1]);
    uint32_t b = mesh_import_float_to_u8(rgb[2]);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static size_t mesh_import_tuple_hash(int32_t pos_idx,
                                     int32_t uv_idx,
                                     int32_t normal_idx,
                                     size_t capacity) {
    uint64_t h = 1469598103934665603ULL;
    h ^= (uint32_t)pos_idx; h *= 1099511628211ULL;
    h ^= (uint32_t)uv_idx; h *= 1099511628211ULL;
    h ^= (uint32_t)normal_idx; h *= 1099511628211ULL;
    return (size_t)(h % capacity);
}

static int mesh_import_get_or_add_vertex(
    const nmo_obj_data_t *obj_data,
    const nmo_obj_face_vertex_t *fv,
    nmo_vertex_t *vertices,
    uint32_t *vertex_colors,
    mesh_import_dedup_slot_t *dedup_table,
    size_t dedup_cap,
    uint32_t *unique_count,
    uint16_t *out_index) {
    int32_t pi = fv->pos_idx;
    int32_t ui = fv->uv_idx;
    int32_t ni = fv->normal_idx;
    size_t slot = mesh_import_tuple_hash(pi, ui, ni, dedup_cap);

    for (;;) {
        mesh_import_dedup_slot_t *entry = &dedup_table[slot];
        if (entry->idx_plus1 == 0) {
            if (*unique_count >= 65535u) return -1;

            nmo_vertex_t *v = &vertices[*unique_count];
            memset(v, 0, sizeof(*v));

            if (pi >= 0 && (size_t)pi < obj_data->pos_count) {
                v->position.x = obj_data->positions[(size_t)pi * 3u + 0u];
                v->position.y = obj_data->positions[(size_t)pi * 3u + 1u];
                v->position.z = obj_data->positions[(size_t)pi * 3u + 2u];
            }
            if (ui >= 0 && (size_t)ui < obj_data->uv_count) {
                v->uv.x = obj_data->uvs[(size_t)ui * 2u + 0u];
                v->uv.y = obj_data->uvs[(size_t)ui * 2u + 1u];
            }
            if (ni >= 0 && (size_t)ni < obj_data->normal_count) {
                v->normal.x = obj_data->normals[(size_t)ni * 3u + 0u];
                v->normal.y = obj_data->normals[(size_t)ni * 3u + 1u];
                v->normal.z = obj_data->normals[(size_t)ni * 3u + 2u];
            }

            if (vertex_colors) {
                uint32_t color = 0xFFFFFFFFu;
                if (pi >= 0 && obj_data->position_has_color &&
                    obj_data->colors &&
                    (size_t)pi < obj_data->pos_count &&
                    obj_data->position_has_color[pi]) {
                    color = mesh_import_rgb_to_argb(
                        &obj_data->colors[(size_t)pi * 3u]);
                }
                vertex_colors[*unique_count] = color;
            }

            entry->pos_idx = pi;
            entry->uv_idx = ui;
            entry->normal_idx = ni;
            entry->idx_plus1 = (uint16_t)(*unique_count + 1u);
            *out_index = (uint16_t)*unique_count;
            (*unique_count)++;
            return 0;
        }

        if (entry->pos_idx == pi &&
            entry->uv_idx == ui &&
            entry->normal_idx == ni) {
            *out_index = (uint16_t)(entry->idx_plus1 - 1u);
            return 0;
        }

        slot = (slot + 1u) % dedup_cap;
    }
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

    nmo_arena_t *arena = nmo_session_get_arena(c.session);

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

    bool has_vertex_colors = false;
    if (obj_data.position_has_color) {
        for (size_t i = 0; i < obj_data.pos_count; ++i) {
            if (obj_data.position_has_color[i]) {
                has_vertex_colors = true;
                break;
            }
        }
    }

    bool has_unassigned_material = false;
    for (size_t fi = 0; fi < obj_data.face_count; ++fi) {
        if (obj_data.faces[fi].material_group == NMO_OBJ_NO_MATERIAL) {
            has_unassigned_material = true;
            break;
        }
    }

    uint32_t material_offset = has_unassigned_material ? 1u : 0u;
    if (obj_data.material_name_count > UINT32_MAX - material_offset) {
        fprintf(stderr, "Error: OBJ material group count exceeds supported range\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }
    uint32_t mat_group_count = (uint32_t)obj_data.material_name_count + material_offset;
    if (obj_data.face_count > 0 && mat_group_count == 0) {
        mat_group_count = 1;
        has_unassigned_material = true;
        material_offset = 1;
    }
    if (mat_group_count > UINT16_MAX) {
        fprintf(stderr, "Error: OBJ material group count exceeds CKMesh limit (%u)\n",
                mat_group_count);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_material_group_t *mat_groups = NULL;
    if (mat_group_count > 0) {
        mat_groups = (nmo_material_group_t *)nmo_arena_alloc(
            arena, mat_group_count * sizeof(nmo_material_group_t),
            alignof(nmo_material_group_t));
        if (!mat_groups) {
            fprintf(stderr, "Error: Out of memory\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        for (uint32_t gi = 0; gi < mat_group_count; gi++) {
            mat_groups[gi].material_id = 0;
        }
        for (uint32_t mi = 0; mi < obj_data.material_name_count; ++mi) {
            uint32_t gi = mi + material_offset;
            if (obj_data.material_names[mi]) {
                nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
                nmo_object_t *mat = nmo_object_repository_find_by_name(
                    repo, obj_data.material_names[mi]);
                if (mat && nmo_object_get_class_id(mat) == NMO_CID_MATERIAL) {
                    mat_groups[gi].material_id = nmo_object_get_id(mat);
                }
            }
        }
    }

    /* Build deduplicated vertex array from face and line data.
     * Worst case is face_count*3 + line_count*2 unique vertices; we allocate
     * that and shrink the effective count after dedup. */
    size_t max_verts = obj_data.face_count * 3u + obj_data.line_count * 2u;
    if (max_verts > 65535) {
        fprintf(stderr, "Error: Mesh exceeds 65535 vertex limit (%zu)\n",
                max_verts);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_vertex_t *vertices = (nmo_vertex_t *)nmo_arena_alloc(
        arena, max_verts * sizeof(nmo_vertex_t), alignof(nmo_vertex_t));
    uint16_t *face_indices = NULL;
    nmo_face_t *faces = NULL;
    uint16_t *line_indices = NULL;
    uint32_t *vertex_colors = NULL;

    if (obj_data.face_count > 0) {
        face_indices = (uint16_t *)nmo_arena_alloc(
            arena, obj_data.face_count * 3u * sizeof(uint16_t), alignof(uint16_t));
        faces = (nmo_face_t *)nmo_arena_alloc(
            arena, obj_data.face_count * sizeof(nmo_face_t), alignof(nmo_face_t));
    }
    if (obj_data.line_count > 0) {
        line_indices = (uint16_t *)nmo_arena_alloc(
            arena, obj_data.line_count * 2u * sizeof(uint16_t), alignof(uint16_t));
    }
    if (has_vertex_colors) {
        vertex_colors = (uint32_t *)nmo_arena_alloc(
            arena, max_verts * sizeof(uint32_t), alignof(uint32_t));
    }

    if (!vertices ||
        (obj_data.face_count > 0 && (!face_indices || !faces)) ||
        (obj_data.line_count > 0 && !line_indices) ||
        (has_vertex_colors && !vertex_colors)) {
        fprintf(stderr, "Error: Out of memory\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Hash-based vertex deduplication.
     * Key: (pos_idx, uv_idx, normal_idx) tuple.
     * Open-addressing hash table mapping tuple -> unified vertex index. */
    size_t dedup_cap = max_verts * 2;   /* load factor <= 0.5 */
    if (dedup_cap < 64) dedup_cap = 64;

    mesh_import_dedup_slot_t *dedup_table =
        (mesh_import_dedup_slot_t *)calloc(dedup_cap, sizeof(mesh_import_dedup_slot_t));
    if (!dedup_table) {
        fprintf(stderr, "Error: Out of memory\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    uint32_t unique_count = 0;

    for (size_t fi = 0; fi < obj_data.face_count; fi++) {
        const nmo_obj_face_t *of = &obj_data.faces[fi];

        for (int vi = 0; vi < 3; vi++) {
            uint16_t found_idx = 0;
            if (mesh_import_get_or_add_vertex(&obj_data, &of->verts[vi],
                                              vertices, vertex_colors,
                                              dedup_table, dedup_cap,
                                              &unique_count, &found_idx) < 0) {
                free(dedup_table);
                fprintf(stderr, "Error: Mesh exceeds 65535 vertex limit\n");
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
            }

            face_indices[fi * 3u + (size_t)vi] = found_idx;
        }

        /* Face normal: compute from cross product */
        nmo_vertex_t *v0 = &vertices[face_indices[fi * 3 + 0]];
        nmo_vertex_t *v1 = &vertices[face_indices[fi * 3 + 1]];
        nmo_vertex_t *v2 = &vertices[face_indices[fi * 3 + 2]];
        float e1x = v1->position.x - v0->position.x;
        float e1y = v1->position.y - v0->position.y;
        float e1z = v1->position.z - v0->position.z;
        float e2x = v2->position.x - v0->position.x;
        float e2y = v2->position.y - v0->position.y;
        float e2z = v2->position.z - v0->position.z;
        float nx = e1y * e2z - e1z * e2y;
        float ny = e1z * e2x - e1x * e2z;
        float nz = e1x * e2y - e1y * e2x;
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }

        faces[fi].normal.x = nx;
        faces[fi].normal.y = ny;
        faces[fi].normal.z = nz;
        if (of->material_group == NMO_OBJ_NO_MATERIAL) {
            faces[fi].material_group_idx = 0;
        } else {
            faces[fi].material_group_idx =
                (uint16_t)(of->material_group + material_offset);
        }
        faces[fi].channel_mask = 0;
    }

    for (size_t li = 0; li < obj_data.line_count; ++li) {
        const nmo_obj_line_t *ol = &obj_data.lines[li];
        for (int vi = 0; vi < 2; ++vi) {
            uint16_t found_idx = 0;
            if (mesh_import_get_or_add_vertex(&obj_data, &ol->verts[vi],
                                              vertices, vertex_colors,
                                              dedup_table, dedup_cap,
                                              &unique_count, &found_idx) < 0) {
                free(dedup_table);
                fprintf(stderr, "Error: Mesh exceeds 65535 vertex limit\n");
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
            }
            line_indices[li * 2u + (size_t)vi] = found_idx;
        }
    }

    free(dedup_table);
    size_t total_verts = unique_count;

    /* Compute bounds */
    nmo_vector_t center, box_min, box_max;
    float bnd_radius;
    compute_bounds(&obj_data, &center, &box_min, &box_max, &bnd_radius);

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
        int create_rc = nmo_session_create_object(
            c.session, NMO_CID_MESH, create_name, NMO_GUID_NULL,
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

    nmo_session_edit_t *edit = NULL;
    nmo_status_t edit_rc = nmo_session_edit_begin(c.session, "mesh.import", &edit);
    if (edit_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin mesh edit: %s\n",
                nmo_error_string(edit_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    if (created_mesh_id != 0) {
        edit_rc = nmo_session_edit_track_created_object(edit, created_mesh_id);
        if (edit_rc != NMO_OK) {
            nmo_session_edit_rollback(edit);
            fprintf(stderr, "Error: Failed to track created mesh object: %s\n",
                    nmo_error_string(edit_rc));
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
    }

    /* Get or allocate mesh state */
    nmo_mesh_state_t *ms =
        (nmo_mesh_state_t *)nmo_object_get_state(mesh_obj);
    if (!ms) {
        /* Newly created object -- allocate and zero-init state */
        nmo_status_t alloc_rc = nmo_object_alloc_state(mesh_obj, sizeof(nmo_mesh_state_t));
        if (alloc_rc != NMO_OK) {
            nmo_session_edit_rollback(edit);
            fprintf(stderr, "Error: Failed to allocate mesh state\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        ms = (nmo_mesh_state_t *)nmo_object_get_state(mesh_obj);
        if (!ms) {
            nmo_session_edit_rollback(edit);
            fprintf(stderr, "Error: Mesh state allocation failed\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        memset(ms, 0, sizeof(*ms));
    }

    edit_rc = nmo_session_edit_snapshot_bytes(edit, ms, sizeof(*ms));
    if (edit_rc != NMO_OK) {
        nmo_session_edit_rollback(edit);
        fprintf(stderr, "Error: Failed to snapshot mesh state: %s\n",
                nmo_error_string(edit_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    ms->vertex_count = (uint32_t)total_verts;
    ms->vertices = vertices;
    ms->face_count = (uint32_t)obj_data.face_count;
    ms->faces = faces;
    ms->face_vertex_indices = face_indices;
    ms->line_count = (uint32_t)obj_data.line_count;
    ms->line_indices = line_indices;
    ms->vertex_colors = vertex_colors;
    ms->vertex_specular = NULL;
    ms->vertex_weights = NULL;
    ms->vertex_weight_count = 0;
    ms->material_group_count = mat_group_count;
    ms->material_groups = mat_groups;
    ms->bary_center = center;
    ms->local_box_min = box_min;
    ms->local_box_max = box_max;
    ms->radius = bnd_radius;

    /* Update name if requested */
    if (mesh_name) {
        edit_rc =
            nmo_session_edit_rename_object(edit, nmo_object_get_id(mesh_obj), mesh_name);
        if (edit_rc != NMO_OK) {
            nmo_session_edit_rollback(edit);
            fprintf(stderr, "Error: Failed to rename mesh object: %s\n",
                    nmo_error_string(edit_rc));
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
    }

    uint32_t edit_flags = NMO_SESSION_EDIT_OBJECT_STATE | NMO_SESSION_EDIT_REFERENCES;
    if (mesh_name) {
        edit_flags |= NMO_SESSION_EDIT_NAMES;
    }
    nmo_session_edit_mark(edit, edit_flags);

    edit_rc = nmo_session_edit_snapshot_object_chunk(
        edit, nmo_object_get_id(mesh_obj));
    if (edit_rc != NMO_OK) {
        nmo_session_edit_rollback(edit);
        fprintf(stderr, "Error: Failed to snapshot mesh chunk: %s\n",
                nmo_error_string(edit_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Serialize updated mesh back to its chunk (create one if needed) */
    nmo_chunk_t *chunk = nmo_object_get_chunk(mesh_obj);
    if (!chunk) {
        chunk = nmo_chunk_create(arena);
        if (chunk) {
            chunk->class_id = NMO_CID_MESH;
            chunk->chunk_version = 7;
            chunk->data_version = 7;
            chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
            nmo_object_set_chunk(mesh_obj, chunk);
        }
    }
    if (chunk) {
        st = nmo_chunk_start_write(chunk);
        if (st != NMO_OK) {
            nmo_session_edit_rollback(edit);
            fprintf(stderr, "Error: Failed to prepare mesh chunk: %s\n",
                    nmo_error_string(st));
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        const nmo_type_descriptor_t *type_desc = NULL;
        if (c.registry) {
            type_desc = nmo_type_registry_find_by_class_id(c.registry, NMO_CID_MESH);
        }
        if (type_desc) {
            nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
                arena,
                nmo_session_get_repository(c.session),
                NMO_SERIALIZE_FLAG_FILE_MODE,
                0);
            st = nmo_mesh_serialize(ms, chunk, type_desc, &ser_ctx);
            if (st != NMO_OK) {
                nmo_session_edit_rollback(edit);
                fprintf(stderr, "Error: Mesh serialization returned %s\n",
                        nmo_error_string(st));
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
            }
        }
    }

    edit_rc = nmo_session_edit_commit(edit);
    if (edit_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to commit mesh edit: %s\n",
                nmo_error_string(edit_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    if (!dry_run) {
        int save_rc = nmo_cli_save_session(c.session, output_path, NULL);
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
