/**
 * @file nmo_cmd_mesh.c
 * @brief CLI mesh command group implementation
 */

#include "nmo_cmd_mesh.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "session/nmo_session.h"
#include "session/nmo_context.h"
#include "app/nmo_save.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/nmo_object_struct_defs.h"
#include "format/nmo_obj_parser.h"
#include "core/nmo_arena.h"
#include "type/nmo_type_system.h"

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

static void sanitize_name(char *dst, size_t dst_size, const char *name,
                          nmo_object_id_t id) {
    /* Limit name to 200 chars so that name + "_" + uint32 fits in 256 */
    if (name && name[0]) {
        char safe[201];
        size_t i = 0;
        for (; name[i] && i < sizeof(safe) - 1; ++i) {
            unsigned char ch = (unsigned char)name[i];
            if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' ||
                ch == '?' || ch == '"' || ch == '<' || ch == '>' ||
                ch == '|' || ch < 0x20 || ch == ' ') {
                safe[i] = '_';
            } else {
                safe[i] = (char)ch;
            }
        }
        safe[i] = '\0';
        snprintf(dst, dst_size, "%s_%u", safe, id);
    } else {
        snprintf(dst, dst_size, "mesh_%u", id);
    }
}

static void argb_to_rgb_float(uint32_t argb, float *r, float *g, float *b) {
    *r = (float)((argb >> 16) & 0xFF) / 255.0f;
    *g = (float)((argb >> 8)  & 0xFF) / 255.0f;
    *b = (float)((argb)       & 0xFF) / 255.0f;
}

/* ============================================================================
 * mesh list
 * ============================================================================ */

int nmo_cmd_mesh_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
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
            if (!obj || nmo_object_get_class_id(obj) != NMO_CID_MESH) continue;

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            nmo_object_id_t id = nmo_object_get_id(obj);
            yyjson_mut_obj_add_uint(doc, item, "id", id);

            const char *name = nmo_object_get_name(obj);
            nmo_cli_json_add_str_safe(doc, item, "name",
                                      (name && name[0]) ? name : "");

            const nmo_mesh_state_t *ms =
                (const nmo_mesh_state_t *)nmo_object_get_data(obj);
            if (ms) {
                yyjson_mut_obj_add_uint(doc, item, "vertices", ms->vertex_count);
                yyjson_mut_obj_add_uint(doc, item, "faces", ms->face_count);
                yyjson_mut_obj_add_uint(doc, item, "materials",
                                        ms->material_group_count);
            }

            yyjson_mut_arr_add_val(arr, item);
            found++;
        }

        yyjson_mut_obj_add_uint(doc, data, "count", found);
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
        uint32_t found = 0;

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            if (!obj || nmo_object_get_class_id(obj) != NMO_CID_MESH) continue;

            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

            const char *name = nmo_object_get_name(obj);
            if (!name || !name[0]) name = "-";

            char vert_buf[16], face_buf[16], mat_buf[16];

            const nmo_mesh_state_t *ms =
                (const nmo_mesh_state_t *)nmo_object_get_data(obj);
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
            nmo_cli_table_add_row(&table, cells, 5);
            found++;
        }

        fprintf(c.out, "Meshes: %u\n\n", found);
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
        fprintf(stderr, "Error: No mesh ID specified\n");
        fprintf(stderr, "Usage: nmo mesh show <id> <file>\n");
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
    if (nmo_object_get_class_id(obj) != NMO_CID_MESH) {
        fprintf(stderr, "Error: Object %u is not a CKMesh\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const char *name = nmo_object_get_name(obj);
    const nmo_mesh_state_t *ms =
        (const nmo_mesh_state_t *)nmo_object_get_data(obj);

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
                mat = (const nmo_material_state_t *)nmo_object_get_data(mat_obj);
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
        (const nmo_mesh_state_t *)nmo_object_get_data(obj);

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
    sanitize_name(safe_name, sizeof(safe_name), name, id);

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

int nmo_cmd_mesh_export(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--out-dir", "-d", NMO_OPT_STRING, "Output directory (required)"},
        {"--id",      NULL,  NMO_OPT_UINT,   "Export single mesh by ID"},
        {"--all",     "-a", NMO_OPT_FLAG,   "Export all meshes"},
    };
    nmo_opt_val_t vals[3];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 3, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *out_dir = vals[0].present ? vals[0].val.str : NULL;
    uint32_t filter_id  = vals[1].present ? vals[1].val.u   : 0;
    bool export_all     = vals[2].val.flag;

    /* Also accept positional: <id> <file> */
    if (!filter_id && !export_all && r.pos_count >= 2) {
        uint32_t pid;
        if (nmo_tool_parse_u32(r.pos_args[0], &pid)) {
            filter_id = pid;
        }
    }

    if (!out_dir || !*out_dir) {
        fprintf(stderr, "Error: Missing --out-dir\n");
        fprintf(stderr, "Usage: nmo mesh export --out-dir <dir> [--id <n> | --all] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!filter_id && !export_all) {
        fprintf(stderr, "Error: Specify --id <n> or --all\n");
        fprintf(stderr, "Usage: nmo mesh export --out-dir <dir> [--id <n> | --all] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (mesh_ensure_dir(out_dir) < 0) {
        fprintf(stderr, "Error: Cannot create directory '%s' (%s)\n",
                out_dir, strerror(errno));
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    (void)nmo_session_get_objects(c.session, &objects, &object_count);

    uint32_t exported = 0;
    uint32_t errors = 0;

    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *data = NULL;
    yyjson_mut_val *entries = NULL;

    if (c.is_json) {
        doc = nmo_cmd_ctx_json_begin(&c);
        data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, data, "out_dir", out_dir);
        entries = yyjson_mut_arr(doc);
    } else {
        fprintf(c.out, "Exporting meshes to: %s\n", out_dir);
    }

    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *obj = objects[i];
        if (!obj || nmo_object_get_class_id(obj) != NMO_CID_MESH) continue;
        if (filter_id && nmo_object_get_id(obj) != filter_id) continue;

        int ret = export_single_mesh(&c, obj, out_dir, c.out,
                                     c.is_json, doc, entries);
        if (ret < 0) errors++;
        else exported++;
    }

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

    return nmo_cmd_ctx_done(&c, exit_code);
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

int nmo_cmd_mesh_import(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output NMO file (required)"},
        {"--replace", NULL,  NMO_OPT_STRING, "Replace existing mesh by ID"},
        {"--name",    "-n", NMO_OPT_STRING, "Mesh name (default: filename)"},
    };
    nmo_opt_val_t vals[3];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 3, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path  = vals[0].present ? vals[0].val.str : NULL;
    const char *replace_str  = vals[1].present ? vals[1].val.str : NULL;
    const char *mesh_name    = vals[2].present ? vals[2].val.str : NULL;

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
        fprintf(stderr, "Usage: nmo mesh import <obj-file> <nmo-file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!nmo_file_path) {
        fprintf(stderr, "Error: Missing NMO file path\n");
        fprintf(stderr, "Usage: nmo mesh import <obj-file> <nmo-file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!output_path || !*output_path) {
        fprintf(stderr, "Error: Missing --output/-o\n");
        fprintf(stderr, "Usage: nmo mesh import <obj-file> <nmo-file> -o <output>\n");
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
    int rc = nmo_cmd_ctx_init_with_file(&c, nmo_file_path, global);
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

    if (obj_data.face_count == 0) {
        fprintf(stderr, "Error: OBJ file has no faces\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    /* Build vertex array from face data (no dedup for v1) */
    size_t total_verts = obj_data.face_count * 3;
    if (total_verts > 65535) {
        fprintf(stderr, "Error: Mesh exceeds 65535 vertex limit (%zu)\n",
                total_verts);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_vertex_t *vertices = (nmo_vertex_t *)nmo_arena_alloc(
        arena, total_verts * sizeof(nmo_vertex_t), alignof(nmo_vertex_t));
    uint16_t *face_indices = (uint16_t *)nmo_arena_alloc(
        arena, obj_data.face_count * 3 * sizeof(uint16_t), alignof(uint16_t));
    nmo_face_t *faces = (nmo_face_t *)nmo_arena_alloc(
        arena, obj_data.face_count * sizeof(nmo_face_t), alignof(nmo_face_t));

    if (!vertices || !face_indices || !faces) {
        fprintf(stderr, "Error: Out of memory\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Expand face vertices into interleaved vertex array */
    uint16_t vert_idx = 0;
    for (size_t fi = 0; fi < obj_data.face_count; fi++) {
        const nmo_obj_face_t *of = &obj_data.faces[fi];

        for (int vi = 0; vi < 3; vi++) {
            nmo_vertex_t *v = &vertices[vert_idx];
            memset(v, 0, sizeof(*v));

            int32_t pi = of->verts[vi].pos_idx;
            if (pi >= 0 && (size_t)pi < obj_data.pos_count) {
                v->position.x = obj_data.positions[pi * 3 + 0];
                v->position.y = obj_data.positions[pi * 3 + 1];
                v->position.z = obj_data.positions[pi * 3 + 2];
            }

            int32_t ui = of->verts[vi].uv_idx;
            if (ui >= 0 && (size_t)ui < obj_data.uv_count) {
                v->uv.x = obj_data.uvs[ui * 2 + 0];
                v->uv.y = obj_data.uvs[ui * 2 + 1];
            }

            int32_t ni = of->verts[vi].normal_idx;
            if (ni >= 0 && (size_t)ni < obj_data.normal_count) {
                v->normal.x = obj_data.normals[ni * 3 + 0];
                v->normal.y = obj_data.normals[ni * 3 + 1];
                v->normal.z = obj_data.normals[ni * 3 + 2];
            }

            face_indices[fi * 3 + vi] = vert_idx;
            vert_idx++;
        }

        /* Face normal: compute from cross product */
        nmo_vertex_t *v0 = &vertices[fi * 3 + 0];
        nmo_vertex_t *v1 = &vertices[fi * 3 + 1];
        nmo_vertex_t *v2 = &vertices[fi * 3 + 2];
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
        faces[fi].material_group_idx = (uint16_t)of->material_group;
        faces[fi].channel_mask = 0;
    }

    /* Build material groups from OBJ material names */
    uint32_t mat_group_count = obj_data.material_name_count > 0
                             ? (uint32_t)obj_data.material_name_count : 1;
    nmo_material_group_t *mat_groups = (nmo_material_group_t *)nmo_arena_alloc(
        arena, mat_group_count * sizeof(nmo_material_group_t),
        alignof(nmo_material_group_t));
    if (!mat_groups) {
        fprintf(stderr, "Error: Out of memory\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    for (uint32_t gi = 0; gi < mat_group_count; gi++) {
        mat_groups[gi].material_id = 0;
        /* Resolve material by OBJ usemtl name -> NMO CKMaterial name match */
        if (gi < obj_data.material_name_count && obj_data.material_names[gi]) {
            nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
            nmo_object_t *mat = nmo_object_repository_find_by_name(repo, obj_data.material_names[gi]);
            if (mat && nmo_object_get_class_id(mat) == NMO_CID_MATERIAL) {
                mat_groups[gi].material_id = nmo_object_get_id(mat);
            }
        }
    }

    /* Compute bounds */
    nmo_vector_t center, box_min, box_max;
    float bnd_radius;
    compute_bounds(&obj_data, &center, &box_min, &box_max, &bnd_radius);

    /* Find or create mesh object */
    nmo_object_t *mesh_obj = NULL;
    uint32_t replace_id = 0;

    if (replace_str) {
        if (!nmo_tool_parse_u32_dec(replace_str, &replace_id)) {
            fprintf(stderr, "Error: Invalid --replace ID '%s'\n", replace_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        mesh_obj = nmo_core_find_by_id(&c, replace_id);
        if (!mesh_obj) {
            fprintf(stderr, "Error: Mesh object %u not found\n", replace_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_NOT_FOUND);
        }
        if (nmo_object_get_class_id(mesh_obj) != NMO_CID_MESH) {
            fprintf(stderr, "Error: Object %u is not a CKMesh\n", replace_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    if (!mesh_obj) {
        fprintf(stderr, "Error: --replace is required (mesh creation not yet supported)\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    /* Update mesh state */
    nmo_mesh_state_t *ms =
        (nmo_mesh_state_t *)nmo_object_get_data(mesh_obj);
    if (!ms) {
        fprintf(stderr, "Error: Mesh object has no state\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    ms->vertex_count = (uint32_t)total_verts;
    ms->vertices = vertices;
    ms->face_count = (uint32_t)obj_data.face_count;
    ms->faces = faces;
    ms->face_vertex_indices = face_indices;
    ms->material_group_count = mat_group_count;
    ms->material_groups = mat_groups;
    ms->bary_center = center;
    ms->local_box_min = box_min;
    ms->local_box_max = box_max;
    ms->radius = bnd_radius;

    /* Update name if requested */
    if (mesh_name) {
        nmo_object_set_name(mesh_obj, mesh_name);
    }

    /* Serialize updated mesh back to its chunk */
    nmo_chunk_t *chunk = nmo_object_get_chunk(mesh_obj);
    if (chunk) {
        const nmo_type_descriptor_t *type_desc = NULL;
        if (c.registry) {
            type_desc = nmo_type_registry_find_by_class_id(c.registry, NMO_CID_MESH);
        }
        if (type_desc) {
            st = nmo_mesh_serialize(ms, chunk, type_desc, c.session);
            if (st != NMO_OK) {
                fprintf(stderr, "Warning: Mesh serialization returned %d\n", st);
            }
        }
    }

    /* Save */
    st = nmo_save_file(c.session, output_path, NULL);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to save '%s'\n", output_path);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    if (c.is_json) {
        yyjson_mut_doc *jdoc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *jdata = yyjson_mut_obj(jdoc);
        yyjson_mut_obj_add_str(jdoc, jdata, "output", output_path);
        yyjson_mut_obj_add_uint(jdoc, jdata, "vertex_count", (uint64_t)total_verts);
        yyjson_mut_obj_add_uint(jdoc, jdata, "face_count",
                                (uint64_t)obj_data.face_count);
        yyjson_mut_obj_add_str(jdoc, jdata, "status", "ok");
        nmo_cmd_ctx_json_end(&c, jdoc, jdata, "mesh.import");
    } else {
        fprintf(c.out, "Imported %zu vertices, %zu faces from '%s'\n",
                total_verts, obj_data.face_count, obj_file_path);
        fprintf(c.out, "Saved to '%s'\n", output_path);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}
