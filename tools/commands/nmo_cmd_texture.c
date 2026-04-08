/**
 * @file nmo_cmd_texture.c
 * @brief CLI texture command group implementation
 */

#include "nmo_cmd_texture.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "format/nmo_stb_adapter.h"
#include "format/nmo_image.h"
#include "core/nmo_arena.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#define NMO_TEX_PATH_SEP '\\'
#else
#include <sys/stat.h>
#define NMO_TEX_PATH_SEP '/'
#endif

/* ============================================================================
 * Helpers
 * ============================================================================ */

static const char *bitmap_kind_str(CKTEXTURE_BITMAP_KIND kind) {
    switch (kind) {
    case CKTEXTURE_BITMAP_READER:  return "reader";
    case CKTEXTURE_BITMAP_RAW:     return "raw";
    case CKTEXTURE_BITMAP_BITMAP2: return "bitmap2";
    case CKTEXTURE_BITMAP_NONE:    return "none";
    default:                       return "unknown";
    }
}

static const char *save_options_str(uint16_t opts) {
    if (opts & NMO_CKTEXTURE_EXTERNAL)         return "external";
    if (opts & NMO_CKTEXTURE_IMAGEFORMAT)      return "imageformat";
    if (opts & NMO_CKTEXTURE_INCLUDEORIGINALFILE) return "include_original";
    if (opts & NMO_CKTEXTURE_USEGLOBAL)        return "use_global";
    return "rawdata";
}

static bool is_external_texture(const nmo_texture_state_t *ts) {
    return (ts->save_options & NMO_CKTEXTURE_EXTERNAL) != 0;
}

static void format_dims(char *buf, size_t buf_size, int32_t w, int32_t h) {
    if (w > 0 && h > 0) {
        snprintf(buf, buf_size, "%dx%d", w, h);
    } else {
        snprintf(buf, buf_size, "-");
    }
}

static const char *format_label(const nmo_texture_state_t *ts) {
    switch (ts->bitmap_kind) {
    case CKTEXTURE_BITMAP_READER:  return "reader";
    case CKTEXTURE_BITMAP_RAW:     return "raw";
    case CKTEXTURE_BITMAP_BITMAP2: return "bitmap2";
    case CKTEXTURE_BITMAP_NONE:
        if (is_external_texture(ts)) return "(external)";
        return "none";
    default:
        return "unknown";
    }
}

static int tex_ensure_dir(const char *dir_path, char *errbuf, size_t errbuf_size) {
    if (!dir_path || !*dir_path) return -1;
#ifdef _WIN32
    if (_mkdir(dir_path) == 0) return 0;
#else
    if (mkdir(dir_path, 0755) == 0) return 0;
#endif
    if (errno == EEXIST) return 0;
    if (errbuf && errbuf_size > 0) {
        snprintf(errbuf, errbuf_size, "Failed to create directory '%s' (%s)",
                 dir_path, strerror(errno));
    }
    return -1;
}

static char *tex_join_path(const char *dir, const char *file) {
    if (!dir || !file) return NULL;
    size_t dir_len = strlen(dir);
    size_t file_len = strlen(file);
    size_t need_sep = (dir_len > 0 && dir[dir_len - 1] != (char)NMO_TEX_PATH_SEP) ? 1u : 0u;
    size_t total = dir_len + need_sep + file_len + 1u;
    char *out = (char *)malloc(total);
    if (!out) return NULL;
    memcpy(out, dir, dir_len);
    size_t pos = dir_len;
    if (need_sep) out[pos++] = (char)NMO_TEX_PATH_SEP;
    memcpy(out + pos, file, file_len);
    out[pos + file_len] = '\0';
    return out;
}

static bool tex_file_exists(const char *path) {
    if (!path || !*path) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

/**
 * Sanitize texture name for filename: replace /\:*?"<>| with '_'
 */
static void sanitize_tex_filename(char *dst, size_t dst_size,
                                  const char *name, nmo_object_id_t id,
                                  const char *ext) {
    char safe[256];
    if (name && name[0]) {
        size_t i = 0;
        for (; name[i] && i < sizeof(safe) - 1; ++i) {
            unsigned char ch = (unsigned char)name[i];
            if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' ||
                ch == '?' || ch == '"' || ch == '<' || ch == '>' ||
                ch == '|' || ch < 0x20) {
                safe[i] = '_';
            } else {
                safe[i] = (char)ch;
            }
        }
        safe[i] = '\0';
        snprintf(dst, dst_size, "%s_%u.%s", safe, id, ext);
    } else {
        snprintf(dst, dst_size, "texture_%u.%s", id, ext);
    }
}

static nmo_bitmap_format_t parse_format_name(const char *name) {
    if (!name) return NMO_BITMAP_FORMAT_PNG;
    if (nmo_tool_streq_ci(name, "png"))  return NMO_BITMAP_FORMAT_PNG;
    if (nmo_tool_streq_ci(name, "bmp"))  return NMO_BITMAP_FORMAT_BMP;
    if (nmo_tool_streq_ci(name, "tga"))  return NMO_BITMAP_FORMAT_TGA;
    if (nmo_tool_streq_ci(name, "jpg"))  return NMO_BITMAP_FORMAT_JPG;
    if (nmo_tool_streq_ci(name, "jpeg")) return NMO_BITMAP_FORMAT_JPG;
    return NMO_BITMAP_FORMAT_PNG;
}

static const char *format_ext(nmo_bitmap_format_t fmt) {
    switch (fmt) {
    case NMO_BITMAP_FORMAT_PNG: return "png";
    case NMO_BITMAP_FORMAT_BMP: return "bmp";
    case NMO_BITMAP_FORMAT_TGA: return "tga";
    case NMO_BITMAP_FORMAT_JPG: return "jpg";
    default: return "png";
    }
}

/* ============================================================================
 * Collect textures
 * ============================================================================ */

typedef struct {
    nmo_object_t **objects;
    size_t count;
    size_t capacity;
} texture_list_t;

static int collect_texture_visitor(size_t index, nmo_object_t *obj,
                                   const nmo_cmd_ctx_t *c, void *user) {
    (void)index;
    (void)c;
    texture_list_t *list = (texture_list_t *)user;
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity ? list->capacity * 2 : 64;
        nmo_object_t **tmp = (nmo_object_t **)realloc(list->objects, new_cap * sizeof(nmo_object_t *));
        if (!tmp) return 0; /* skip on OOM */
        list->objects = tmp;
        list->capacity = new_cap;
    }
    list->objects[list->count++] = obj;
    return 0;
}

/* ============================================================================
 * texture list
 * ============================================================================ */

int nmo_cmd_texture_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--sort",    "-s", NMO_OPT_STRING, "Sort by: id, name, size"},
        {"--top",     "-t", NMO_OPT_UINT,   "Show only top N textures"},
        {"--reverse", "-r", NMO_OPT_FLAG,   "Reverse sort order"},
    };
    nmo_opt_val_t vals[3];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 3, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *sort_by = vals[0].present ? vals[0].val.str : NULL;
    uint32_t top_n = vals[1].present ? vals[1].val.u : 0;
    bool reverse = vals[2].val.flag;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Collect texture objects */
    nmo_core_object_filter_t filter = {0};
    filter.class_id = NMO_CID_TEXTURE;
    filter.class_derived = true;

    texture_list_t tl = {0};
    nmo_core_iter_result_t iter_result;
    nmo_core_iter_objects(&c, &filter, collect_texture_visitor, &tl, &iter_result);

    /* Sort if requested */
    if (sort_by && tl.count > 1) {
        /* Simple bubble sort -- texture counts are small */
        for (size_t i = 0; i < tl.count - 1; ++i) {
            for (size_t j = 0; j < tl.count - 1 - i; ++j) {
                int cmp = 0;
                if (nmo_tool_streq_ci(sort_by, "name")) {
                    const char *a = nmo_object_get_name(tl.objects[j]);
                    const char *b = nmo_object_get_name(tl.objects[j + 1]);
                    if (!a) a = "";
                    if (!b) b = "";
                    cmp = nmo_tool_stricmp(a, b);
                } else if (nmo_tool_streq_ci(sort_by, "size")) {
                    nmo_chunk_t *ca = nmo_object_get_chunk(tl.objects[j]);
                    nmo_chunk_t *cb = nmo_object_get_chunk(tl.objects[j + 1]);
                    size_t sa = ca ? nmo_chunk_get_data_size(ca) : 0;
                    size_t sb = cb ? nmo_chunk_get_data_size(cb) : 0;
                    cmp = (sa > sb) ? 1 : (sa < sb) ? -1 : 0;
                } else {
                    /* default: sort by id */
                    nmo_object_id_t a = nmo_object_get_id(tl.objects[j]);
                    nmo_object_id_t b = nmo_object_get_id(tl.objects[j + 1]);
                    cmp = (a > b) ? 1 : (a < b) ? -1 : 0;
                }
                if (reverse) cmp = -cmp;
                if (cmp > 0) {
                    nmo_object_t *tmp = tl.objects[j];
                    tl.objects[j] = tl.objects[j + 1];
                    tl.objects[j + 1] = tmp;
                }
            }
        }
    }

    size_t display_count = tl.count;
    if (top_n > 0 && (size_t)top_n < display_count) {
        display_count = (size_t)top_n;
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)display_count);
        yyjson_mut_obj_add_uint(doc, data, "total", (uint64_t)tl.count);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < display_count; ++i) {
            nmo_object_t *obj = tl.objects[i];
            yyjson_mut_val *item = yyjson_mut_obj(doc);

            nmo_object_id_t id = nmo_object_get_id(obj);
            yyjson_mut_obj_add_uint(doc, item, "id", id);

            const char *name = nmo_object_get_name(obj);
            if (name && name[0]) {
                nmo_cli_json_add_str_safe(doc, item, "name", name);
            }

            nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
            if (chunk) {
                yyjson_mut_obj_add_uint(doc, item, "size", (uint64_t)nmo_chunk_get_data_size(chunk));
            }

            const nmo_texture_state_t *ts =
                (const nmo_texture_state_t *)nmo_object_get_state(obj);
            if (ts) {
                yyjson_mut_obj_add_int(doc, item, "width", ts->reader_width);
                yyjson_mut_obj_add_int(doc, item, "height", ts->reader_height);
                yyjson_mut_obj_add_str(doc, item, "bitmap_kind", bitmap_kind_str(ts->bitmap_kind));
                yyjson_mut_obj_add_uint(doc, item, "slot_count", ts->slot_count);
                yyjson_mut_obj_add_bool(doc, item, "is_external", is_external_texture(ts));
            }

            yyjson_mut_arr_add_val(arr, item);
        }
        yyjson_mut_obj_add_val(doc, data, "textures", arr);
        nmo_cmd_ctx_json_end(&c, doc, data, "texture.list");
    } else {
        static const nmo_cli_table_col_t columns[] = {
            {"ID",     NMO_CLI_ALIGN_RIGHT, 5,  0},
            {"SIZE",   NMO_CLI_ALIGN_RIGHT, 8,  0},
            {"DIMS",   NMO_CLI_ALIGN_LEFT,  11, 0},
            {"FORMAT", NMO_CLI_ALIGN_LEFT,  10, 0},
            {"SLOTS",  NMO_CLI_ALIGN_RIGHT, 5,  0},
            {"NAME",   NMO_CLI_ALIGN_LEFT,  20, 50},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        for (size_t i = 0; i < display_count; ++i) {
            nmo_object_t *obj = tl.objects[i];
            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

            char size_buf[16];
            nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
            if (chunk) {
                snprintf(size_buf, sizeof(size_buf), "%zu", nmo_chunk_get_data_size(chunk));
            } else {
                snprintf(size_buf, sizeof(size_buf), "-");
            }

            char dims_buf[32];
            char slots_buf[16];
            const char *fmt_label = "-";

            const nmo_texture_state_t *ts =
                (const nmo_texture_state_t *)nmo_object_get_state(obj);
            if (ts) {
                format_dims(dims_buf, sizeof(dims_buf), ts->reader_width, ts->reader_height);
                snprintf(slots_buf, sizeof(slots_buf), "%u", ts->slot_count);
                fmt_label = format_label(ts);
            } else {
                snprintf(dims_buf, sizeof(dims_buf), "-");
                snprintf(slots_buf, sizeof(slots_buf), "-");
            }

            const char *name = nmo_object_get_name(obj);
            const char *cells[] = {
                id_buf, size_buf, dims_buf, fmt_label, slots_buf,
                (name && name[0]) ? name : "-"
            };
            nmo_cli_table_add_row(&table, cells, 6);
        }

        fprintf(c.out, "Textures: %zu", display_count);
        if (display_count < tl.count) {
            fprintf(c.out, " (of %zu total)", tl.count);
        }
        fprintf(c.out, "\n\n");
        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    free(tl.objects);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * texture show
 * ============================================================================ */

int nmo_cmd_texture_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--id", NULL, NMO_OPT_UINT, "Texture object ID"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    /* Get ID from --id or first positional arg */
    uint32_t object_id = 0;
    if (vals[0].present) {
        object_id = vals[0].val.u;
    } else if (r.pos_count >= 1) {
        if (!nmo_tool_parse_u32(r.pos_args[0], &object_id)) {
            fprintf(stderr, "Error: Invalid object ID '%s'\n", r.pos_args[0]);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }
    if (object_id == 0) {
        fprintf(stderr, "Error: No texture ID specified\n");
        fprintf(stderr, "Usage: nmo texture show <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_t *obj = nmo_core_find_by_id(&c, object_id);
    if (!obj) {
        fprintf(stderr, "Error: Object %u not found\n", object_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    if (!nmo_core_class_derives(&c, class_id, NMO_CID_TEXTURE)) {
        fprintf(stderr, "Error: Object %u is not a CKTexture (class %u)\n",
                object_id, class_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const char *name = nmo_object_get_name(obj);
    const nmo_texture_state_t *ts =
        (const nmo_texture_state_t *)nmo_object_get_state(obj);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "id", object_id);
        if (name && name[0]) {
            nmo_cli_json_add_str_safe(doc, data, "name", name);
        }
        yyjson_mut_obj_add_uint(doc, data, "class_id", class_id);
        const char *class_name = nmo_core_class_name(&c, class_id);
        if (class_name) {
            yyjson_mut_obj_add_str(doc, data, "class_name", class_name);
        }

        if (ts) {
            yyjson_mut_obj_add_int(doc, data, "reader_width", ts->reader_width);
            yyjson_mut_obj_add_int(doc, data, "reader_height", ts->reader_height);
            yyjson_mut_obj_add_int(doc, data, "reader_bpp", ts->reader_bpp);
            yyjson_mut_obj_add_str(doc, data, "bitmap_kind",
                                   bitmap_kind_str(ts->bitmap_kind));
            yyjson_mut_obj_add_uint(doc, data, "slot_count", ts->slot_count);
            yyjson_mut_obj_add_str(doc, data, "save_options",
                                   save_options_str(ts->save_options));
            yyjson_mut_obj_add_uint(doc, data, "save_options_raw", ts->save_options);
            yyjson_mut_obj_add_uint(doc, data, "mipmap_level", ts->mipmap_level);
            yyjson_mut_obj_add_bool(doc, data, "is_transparent", ts->is_transparent != 0);
            yyjson_mut_obj_add_bool(doc, data, "is_cubemap", ts->is_cubemap != 0);
            yyjson_mut_obj_add_bool(doc, data, "is_external", is_external_texture(ts));

            if (ts->has_desired_video_format) {
                yyjson_mut_obj_add_uint(doc, data, "desired_video_format",
                                        ts->desired_video_format);
            }
            if (ts->has_transparent_color) {
                char color_buf[16];
                snprintf(color_buf, sizeof(color_buf), "0x%08X",
                         ts->transparent_color);
                yyjson_mut_obj_add_str(doc, data, "transparent_color", color_buf);
            }
            if (ts->has_current_slot) {
                yyjson_mut_obj_add_int(doc, data, "current_slot", ts->current_slot);
            }
            if (ts->has_pick_threshold) {
                yyjson_mut_obj_add_int(doc, data, "pick_threshold", ts->pick_threshold);
            }

            if (ts->has_movie_filename && ts->movie_filename) {
                nmo_cli_json_add_str_safe(doc, data, "movie_filename",
                                          ts->movie_filename);
            }

            /* Slot filenames */
            if (ts->has_slot_filenames && ts->slot_filenames) {
                yyjson_mut_val *fnames = yyjson_mut_arr(doc);
                for (uint32_t i = 0; i < ts->slot_count; ++i) {
                    if (ts->slot_filenames[i]) {
                        nmo_cli_json_add_str_safe_to_arr(doc, fnames,
                                                         ts->slot_filenames[i]);
                    } else {
                        yyjson_mut_arr_add_null(doc, fnames);
                    }
                }
                yyjson_mut_obj_add_val(doc, data, "slot_filenames", fnames);
            }

            /* Per-slot details */
            yyjson_mut_val *slots = yyjson_mut_arr(doc);
            for (uint32_t i = 0; i < ts->slot_count; ++i) {
                yyjson_mut_val *slot = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, slot, "index", i);

                switch (ts->bitmap_kind) {
                case CKTEXTURE_BITMAP_READER:
                    if (ts->reader_slots) {
                        const nmo_texture_reader_slot_t *rs = &ts->reader_slots[i];
                        yyjson_mut_obj_add_str(doc, slot, "type", "reader");
                        yyjson_mut_obj_add_uint(doc, slot, "data_size", rs->data_size);
                        yyjson_mut_obj_add_uint(doc, slot, "format_type", rs->format_type);
                        yyjson_mut_obj_add_uint(doc, slot, "extension", rs->extension);
                        if (rs->alpha_plane_size > 0) {
                            yyjson_mut_obj_add_uint(doc, slot, "alpha_plane_size",
                                                    rs->alpha_plane_size);
                        }
                    }
                    break;
                case CKTEXTURE_BITMAP_RAW:
                    if (ts->raw_slots) {
                        const nmo_texture_raw_slot_t *rs = &ts->raw_slots[i];
                        yyjson_mut_obj_add_str(doc, slot, "type", "raw");
                        yyjson_mut_obj_add_int(doc, slot, "width", rs->width);
                        yyjson_mut_obj_add_int(doc, slot, "height", rs->height);
                        yyjson_mut_obj_add_int(doc, slot, "bits_per_pixel", rs->bits_per_pixel);
                        {
                            char mask_buf[16];
                            snprintf(mask_buf, sizeof(mask_buf), "0x%08X", rs->red_mask);
                            yyjson_mut_obj_add_str(doc, slot, "red_mask", mask_buf);
                            snprintf(mask_buf, sizeof(mask_buf), "0x%08X", rs->green_mask);
                            yyjson_mut_obj_add_str(doc, slot, "green_mask", mask_buf);
                            snprintf(mask_buf, sizeof(mask_buf), "0x%08X", rs->blue_mask);
                            yyjson_mut_obj_add_str(doc, slot, "blue_mask", mask_buf);
                            snprintf(mask_buf, sizeof(mask_buf), "0x%08X", rs->alpha_mask);
                            yyjson_mut_obj_add_str(doc, slot, "alpha_mask", mask_buf);
                        }
                        yyjson_mut_obj_add_uint(doc, slot, "red_size", rs->red_size);
                        yyjson_mut_obj_add_uint(doc, slot, "green_size", rs->green_size);
                        yyjson_mut_obj_add_uint(doc, slot, "blue_size", rs->blue_size);
                        yyjson_mut_obj_add_uint(doc, slot, "alpha_size", rs->alpha_size);
                    }
                    break;
                case CKTEXTURE_BITMAP_BITMAP2:
                    if (ts->bitmap2_slots) {
                        const nmo_texture_bitmap2_slot_t *bs = &ts->bitmap2_slots[i];
                        yyjson_mut_obj_add_str(doc, slot, "type", "bitmap2");
                        yyjson_mut_obj_add_uint(doc, slot, "buffer_size", bs->buffer_size);
                    }
                    break;
                default:
                    yyjson_mut_obj_add_str(doc, slot, "type", "none");
                    break;
                }

                yyjson_mut_arr_add_val(slots, slot);
            }
            yyjson_mut_obj_add_val(doc, data, "slots", slots);
        } else {
            yyjson_mut_obj_add_null(doc, data, "state");
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "texture.show");
    } else {
        /* Text output */
        nmo_cli_print_heading(c.out, "Texture Details", c.colorize);

        char buf[128];
        snprintf(buf, sizeof(buf), "#%u (%s)", object_id,
                 (name && name[0]) ? name : "(unnamed)");
        nmo_cli_print_kv(c.out, "ID / Name", buf, 18, c.colorize);

        const char *class_name = nmo_core_class_name(&c, class_id);
        snprintf(buf, sizeof(buf), "#%u (%s)", class_id, class_name ? class_name : "-");
        nmo_cli_print_kv(c.out, "Class", buf, 18, c.colorize);

        if (!ts) {
            fprintf(c.out, "\n  (no deserialized state)\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
        }

        format_dims(buf, sizeof(buf), ts->reader_width, ts->reader_height);
        nmo_cli_print_kv(c.out, "Dimensions", buf, 18, c.colorize);

        snprintf(buf, sizeof(buf), "%d", ts->reader_bpp);
        nmo_cli_print_kv(c.out, "BPP", buf, 18, c.colorize);

        nmo_cli_print_kv(c.out, "Bitmap Kind", bitmap_kind_str(ts->bitmap_kind), 18, c.colorize);
        nmo_cli_print_kv(c.out, "Save Options", save_options_str(ts->save_options), 18, c.colorize);

        snprintf(buf, sizeof(buf), "%u", ts->slot_count);
        nmo_cli_print_kv(c.out, "Slot Count", buf, 18, c.colorize);

        snprintf(buf, sizeof(buf), "%u", ts->mipmap_level);
        nmo_cli_print_kv(c.out, "Mipmap Level", buf, 18, c.colorize);

        nmo_cli_print_kv(c.out, "Transparent", ts->is_transparent ? "yes" : "no", 18, c.colorize);
        nmo_cli_print_kv(c.out, "Cubemap", ts->is_cubemap ? "yes" : "no", 18, c.colorize);
        nmo_cli_print_kv(c.out, "External", is_external_texture(ts) ? "yes" : "no", 18, c.colorize);

        if (ts->has_desired_video_format) {
            snprintf(buf, sizeof(buf), "%u", ts->desired_video_format);
            nmo_cli_print_kv(c.out, "Video Format", buf, 18, c.colorize);
        }
        if (ts->has_transparent_color) {
            snprintf(buf, sizeof(buf), "0x%08X", ts->transparent_color);
            nmo_cli_print_kv(c.out, "Transp. Color", buf, 18, c.colorize);
        }
        if (ts->has_current_slot) {
            snprintf(buf, sizeof(buf), "%d", ts->current_slot);
            nmo_cli_print_kv(c.out, "Current Slot", buf, 18, c.colorize);
        }
        if (ts->has_pick_threshold) {
            snprintf(buf, sizeof(buf), "%d", ts->pick_threshold);
            nmo_cli_print_kv(c.out, "Pick Threshold", buf, 18, c.colorize);
        }
        if (ts->has_movie_filename && ts->movie_filename) {
            nmo_cli_print_kv(c.out, "Movie File", ts->movie_filename, 18, c.colorize);
        }

        /* Slot filenames */
        if (ts->has_slot_filenames && ts->slot_filenames) {
            fprintf(c.out, "\nSlot Filenames:\n");
            for (uint32_t i = 0; i < ts->slot_count; ++i) {
                fprintf(c.out, "  [%u] %s\n", i,
                        ts->slot_filenames[i] ? ts->slot_filenames[i] : "(null)");
            }
        }

        /* Per-slot details */
        if (ts->slot_count > 0) {
            fprintf(c.out, "\nSlots:\n");
            for (uint32_t i = 0; i < ts->slot_count; ++i) {
                fprintf(c.out, "  Slot %u:\n", i);
                switch (ts->bitmap_kind) {
                case CKTEXTURE_BITMAP_READER:
                    if (ts->reader_slots) {
                        const nmo_texture_reader_slot_t *rs = &ts->reader_slots[i];
                        fprintf(c.out, "    type:       reader\n");
                        fprintf(c.out, "    data_size:  %u\n", rs->data_size);
                        fprintf(c.out, "    format:     %u\n", rs->format_type);
                        fprintf(c.out, "    extension:  %u\n", rs->extension);
                        if (rs->alpha_plane_size > 0) {
                            fprintf(c.out, "    alpha_size: %u\n", rs->alpha_plane_size);
                        }
                    }
                    break;
                case CKTEXTURE_BITMAP_RAW:
                    if (ts->raw_slots) {
                        const nmo_texture_raw_slot_t *rs = &ts->raw_slots[i];
                        fprintf(c.out, "    type:       raw\n");
                        fprintf(c.out, "    dims:       %dx%d @ %d bpp\n",
                                rs->width, rs->height, rs->bits_per_pixel);
                        fprintf(c.out, "    masks:      R=0x%08X G=0x%08X B=0x%08X A=0x%08X\n",
                                rs->red_mask, rs->green_mask,
                                rs->blue_mask, rs->alpha_mask);
                        fprintf(c.out, "    channels:   R=%u G=%u B=%u A=%u bytes\n",
                                rs->red_size, rs->green_size,
                                rs->blue_size, rs->alpha_size);
                    }
                    break;
                case CKTEXTURE_BITMAP_BITMAP2:
                    if (ts->bitmap2_slots) {
                        const nmo_texture_bitmap2_slot_t *bs = &ts->bitmap2_slots[i];
                        fprintf(c.out, "    type:       bitmap2\n");
                        fprintf(c.out, "    buf_size:   %u\n", bs->buffer_size);
                    }
                    break;
                default:
                    fprintf(c.out, "    type:       none\n");
                    break;
                }
            }
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * texture extract - pixel reconstruction helpers
 * ============================================================================ */

/**
 * Try to decode a reader slot to RGBA pixels.
 * Returns arena-allocated RGBA buffer, or NULL on failure.
 */
static uint8_t *decode_reader_slot(nmo_arena_t *arena,
                                   const nmo_texture_reader_slot_t *rs,
                                   int *out_w, int *out_h, int *out_ch) {
    if (!rs->data || rs->data_size == 0) return NULL;

    uint8_t *pixels = nmo_stbi_load_from_memory(
        arena, rs->data, (int)rs->data_size, out_w, out_h, out_ch, 4);
    if (!pixels) return NULL;

    /* Composite separate alpha plane if present */
    if (rs->alpha_plane && rs->alpha_plane_size > 0 && *out_w > 0 && *out_h > 0) {
        size_t pixel_count = (size_t)(*out_w) * (size_t)(*out_h);
        size_t alpha_count = (size_t)rs->alpha_plane_size;
        size_t usable = (alpha_count < pixel_count) ? alpha_count : pixel_count;
        for (size_t i = 0; i < usable; ++i) {
            pixels[i * 4 + 3] = rs->alpha_plane[i];
        }
        *out_ch = 4;
    }

    return pixels;
}

/**
 * Try to reconstruct RGBA from a raw slot (32bpp, 1-byte-per-channel only).
 * Returns arena-allocated RGBA buffer, or NULL on failure.
 */
static uint8_t *decode_raw_slot(nmo_arena_t *arena,
                                const nmo_texture_raw_slot_t *rs,
                                int *out_w, int *out_h, int *out_ch) {
    if (rs->width <= 0 || rs->height <= 0) return NULL;

    /* Only handle the simple case: 32bpp with separate 1-byte channels */
    if (rs->bits_per_pixel != 32) return NULL;

    size_t pixel_count = (size_t)rs->width * (size_t)rs->height;
    size_t expected = pixel_count;

    /* Verify channel data sizes */
    bool has_red   = (rs->red_data   && rs->red_size   >= expected);
    bool has_green = (rs->green_data && rs->green_size >= expected);
    bool has_blue  = (rs->blue_data  && rs->blue_size  >= expected);
    bool has_alpha = (rs->alpha_data && rs->alpha_size >= expected);

    if (!has_red && !has_green && !has_blue) return NULL;

    uint8_t *rgba = (uint8_t *)nmo_arena_alloc(arena, pixel_count * 4, 1);
    if (!rgba) return NULL;

    for (size_t i = 0; i < pixel_count; ++i) {
        rgba[i * 4 + 0] = has_red   ? rs->red_data[i]   : 0;
        rgba[i * 4 + 1] = has_green ? rs->green_data[i] : 0;
        rgba[i * 4 + 2] = has_blue  ? rs->blue_data[i]  : 0;
        rgba[i * 4 + 3] = has_alpha ? rs->alpha_data[i] : 255;
    }

    *out_w = rs->width;
    *out_h = rs->height;
    *out_ch = 4;
    return rgba;
}

/**
 * Try to decode a bitmap2 slot via stb_image.
 * Returns arena-allocated RGBA buffer, or NULL on failure.
 */
static uint8_t *decode_bitmap2_slot(nmo_arena_t *arena,
                                    const nmo_texture_bitmap2_slot_t *bs,
                                    int *out_w, int *out_h, int *out_ch) {
    if (!bs->buffer || bs->buffer_size == 0) return NULL;
    return nmo_stbi_load_from_memory(
        arena, bs->buffer, (int)bs->buffer_size, out_w, out_h, out_ch, 4);
}

/* ============================================================================
 * texture extract
 * ============================================================================ */

int nmo_cmd_texture_extract(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--out-dir",   "-d", NMO_OPT_STRING, "Output directory"},
        {"--id",        NULL,  NMO_OPT_UINT,   "Extract single texture by ID"},
        {"--name",      "-n", NMO_OPT_STRING, "Filter by name wildcard"},
        {"--format",    "-f", NMO_OPT_STRING, "Output format: png, bmp, tga, jpg"},
        {"--quality",   "-q", NMO_OPT_UINT,   "JPEG quality (1-100, default 90)"},
        {"--overwrite", NULL,  NMO_OPT_FLAG,   "Overwrite existing files"},
    };
    nmo_opt_val_t vals[6];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 6, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *out_dir   = vals[0].present ? vals[0].val.str : NULL;
    uint32_t filter_id    = vals[1].present ? vals[1].val.u   : 0;
    const char *name_pat  = vals[2].present ? vals[2].val.str : NULL;
    const char *fmt_str   = vals[3].present ? vals[3].val.str : NULL;
    uint32_t quality      = vals[4].present ? vals[4].val.u   : 90;
    bool overwrite        = vals[5].val.flag;

    if (!out_dir || !*out_dir) {
        fprintf(stderr, "Error: Missing --out-dir\n");
        fprintf(stderr, "Usage: nmo texture extract --out-dir <dir> [options] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_bitmap_format_t out_format = parse_format_name(fmt_str);
    const char *ext = format_ext(out_format);

    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;

    char dir_err[256];
    if (tex_ensure_dir(out_dir, dir_err, sizeof(dir_err)) != 0) {
        fprintf(stderr, "Error: %s\n", dir_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Collect textures */
    nmo_core_object_filter_t filter = {0};
    filter.class_id = NMO_CID_TEXTURE;
    filter.class_derived = true;
    if (filter_id) {
        filter.object_id = filter_id;
    }
    if (name_pat) {
        filter.name_pattern = name_pat;
    }

    texture_list_t tl = {0};
    nmo_core_iter_result_t iter_result;
    nmo_core_iter_objects(&c, &filter, collect_texture_visitor, &tl, &iter_result);

    if (tl.count == 0) {
        if (filter_id) {
            fprintf(stderr, "Error: Texture %u not found\n", filter_id);
        } else {
            fprintf(stderr, "No textures found\n");
        }
        free(tl.objects);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_arena_t *arena = nmo_session_get_arena(c.session);

    uint32_t extracted = 0;
    uint32_t skipped = 0;
    uint32_t warnings = 0;

    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *data = NULL;
    yyjson_mut_val *entries = NULL;
    if (c.is_json) {
        doc = nmo_cmd_ctx_json_begin(&c);
        data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, data, "out_dir", out_dir);
        yyjson_mut_obj_add_str(doc, data, "format", ext);
        entries = yyjson_mut_arr(doc);
    } else {
        fprintf(c.out, "Extracting textures to: %s (format: %s)\n", out_dir, ext);
    }

    for (size_t ti = 0; ti < tl.count; ++ti) {
        nmo_object_t *obj = tl.objects[ti];
        nmo_object_id_t id = nmo_object_get_id(obj);
        const char *name = nmo_object_get_name(obj);

        const nmo_texture_state_t *ts =
            (const nmo_texture_state_t *)nmo_object_get_state(obj);

        /* Build output filename */
        char fname[512];
        sanitize_tex_filename(fname, sizeof(fname), name, id, ext);

        /* No state? */
        if (!ts) {
            skipped++;
            if (c.is_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "id", id);
                nmo_cli_json_add_str_safe(doc, e, "name", name ? name : "");
                yyjson_mut_obj_add_str(doc, e, "status", "skip");
                yyjson_mut_obj_add_str(doc, e, "reason", "no_state");
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(c.out, "  [SKIP] %u %s -> no state\n", id,
                        (name && name[0]) ? name : "(unnamed)");
            }
            continue;
        }

        /* External textures */
        if (ts->bitmap_kind == CKTEXTURE_BITMAP_NONE && is_external_texture(ts)) {
            skipped++;
            if (c.is_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "id", id);
                nmo_cli_json_add_str_safe(doc, e, "name", name ? name : "");
                yyjson_mut_obj_add_str(doc, e, "status", "skip");
                yyjson_mut_obj_add_str(doc, e, "reason", "external");
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(c.out, "  [SKIP] %u %s -> external texture\n", id,
                        (name && name[0]) ? name : "(unnamed)");
            }
            continue;
        }

        /* No slots */
        if (ts->slot_count == 0) {
            skipped++;
            if (c.is_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "id", id);
                nmo_cli_json_add_str_safe(doc, e, "name", name ? name : "");
                yyjson_mut_obj_add_str(doc, e, "status", "skip");
                yyjson_mut_obj_add_str(doc, e, "reason", "no_slots");
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(c.out, "  [SKIP] %u %s -> no slots\n", id,
                        (name && name[0]) ? name : "(unnamed)");
            }
            continue;
        }

        /* Decode slot 0 (primary) */
        int w = 0, h = 0, ch = 0;
        uint8_t *pixels = NULL;
        const char *skip_reason = NULL;

        switch (ts->bitmap_kind) {
        case CKTEXTURE_BITMAP_READER:
            if (ts->reader_slots) {
                pixels = decode_reader_slot(arena, &ts->reader_slots[0], &w, &h, &ch);
                if (!pixels) skip_reason = "decode_failed";
            } else {
                skip_reason = "no_reader_data";
            }
            break;

        case CKTEXTURE_BITMAP_RAW:
            if (ts->raw_slots) {
                pixels = decode_raw_slot(arena, &ts->raw_slots[0], &w, &h, &ch);
                if (!pixels) {
                    skip_reason = (ts->raw_slots[0].bits_per_pixel != 32)
                        ? "unsupported_raw_bpp"
                        : "decode_failed";
                }
            } else {
                skip_reason = "no_raw_data";
            }
            break;

        case CKTEXTURE_BITMAP_BITMAP2:
            if (ts->bitmap2_slots) {
                pixels = decode_bitmap2_slot(arena, &ts->bitmap2_slots[0], &w, &h, &ch);
                if (!pixels) skip_reason = "bitmap2_decode_failed";
            } else {
                skip_reason = "no_bitmap2_data";
            }
            break;

        default:
            skip_reason = "no_pixel_data";
            break;
        }

        if (!pixels || w <= 0 || h <= 0) {
            if (!skip_reason) skip_reason = "decode_failed";
            warnings++;
            if (c.is_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "id", id);
                nmo_cli_json_add_str_safe(doc, e, "name", name ? name : "");
                yyjson_mut_obj_add_str(doc, e, "status", "warn");
                yyjson_mut_obj_add_str(doc, e, "reason", skip_reason);
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(c.out, "  [WARN] %u %s -> %s\n", id,
                        (name && name[0]) ? name : "(unnamed)", skip_reason);
            }
            continue;
        }

        /* Encode to output format */
        size_t out_size = 0;
        uint8_t *encoded = nmo_stbi_write_to_memory(
            arena, out_format, w, h, ch, pixels, (int)quality, &out_size);

        if (!encoded || out_size == 0) {
            warnings++;
            if (c.is_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "id", id);
                nmo_cli_json_add_str_safe(doc, e, "name", name ? name : "");
                yyjson_mut_obj_add_str(doc, e, "status", "warn");
                yyjson_mut_obj_add_str(doc, e, "reason", "encode_failed");
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(c.out, "  [WARN] %u %s -> encode failed\n", id,
                        (name && name[0]) ? name : "(unnamed)");
            }
            continue;
        }

        /* Write to file */
        char *path = tex_join_path(out_dir, fname);
        if (!path) {
            warnings++;
            continue;
        }

        if (!overwrite && tex_file_exists(path)) {
            skipped++;
            if (c.is_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "id", id);
                nmo_cli_json_add_str_safe(doc, e, "name", name ? name : "");
                yyjson_mut_obj_add_str(doc, e, "path", path);
                yyjson_mut_obj_add_str(doc, e, "status", "skip");
                yyjson_mut_obj_add_str(doc, e, "reason", "exists");
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(c.out, "  [SKIP] %u %s -> exists (use --overwrite)\n", id,
                        (name && name[0]) ? name : "(unnamed)");
            }
            free(path);
            continue;
        }

        FILE *fp = fopen(path, "wb");
        if (!fp) {
            warnings++;
            if (c.is_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "id", id);
                nmo_cli_json_add_str_safe(doc, e, "name", name ? name : "");
                yyjson_mut_obj_add_str(doc, e, "path", path);
                yyjson_mut_obj_add_str(doc, e, "status", "warn");
                yyjson_mut_obj_add_str(doc, e, "reason", "open_failed");
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(c.out, "  [WARN] %u %s -> open failed (%s)\n", id,
                        (name && name[0]) ? name : "(unnamed)", strerror(errno));
            }
            free(path);
            continue;
        }

        size_t written = fwrite(encoded, 1, out_size, fp);
        fclose(fp);

        if (written != out_size) {
            warnings++;
            if (c.is_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "id", id);
                nmo_cli_json_add_str_safe(doc, e, "name", name ? name : "");
                yyjson_mut_obj_add_str(doc, e, "path", path);
                yyjson_mut_obj_add_str(doc, e, "status", "warn");
                yyjson_mut_obj_add_str(doc, e, "reason", "write_failed");
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(c.out, "  [WARN] %u %s -> write failed\n", id,
                        (name && name[0]) ? name : "(unnamed)");
            }
            free(path);
            continue;
        }

        extracted++;
        if (c.is_json) {
            yyjson_mut_val *e = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, e, "id", id);
            nmo_cli_json_add_str_safe(doc, e, "name", name ? name : "");
            yyjson_mut_obj_add_str(doc, e, "path", path);
            yyjson_mut_obj_add_int(doc, e, "width", w);
            yyjson_mut_obj_add_int(doc, e, "height", h);
            yyjson_mut_obj_add_uint(doc, e, "file_size", (uint64_t)out_size);
            yyjson_mut_obj_add_str(doc, e, "status", "ok");
            yyjson_mut_arr_add_val(entries, e);
        } else {
            fprintf(c.out, "  [OK]   %u %s -> %s (%dx%d, %zu bytes)\n",
                    id, (name && name[0]) ? name : "(unnamed)",
                    fname, w, h, out_size);
        }
        free(path);
    }

    int exit_code = (warnings > 0 && extracted == 0)
        ? NMO_CLI_EXIT_IO_ERROR : NMO_CLI_EXIT_SUCCESS;

    if (c.is_json) {
        yyjson_mut_obj_add_uint(doc, data, "extracted", extracted);
        yyjson_mut_obj_add_uint(doc, data, "skipped", skipped);
        yyjson_mut_obj_add_uint(doc, data, "warnings", warnings);
        yyjson_mut_obj_add_val(doc, data, "entries", entries);
        nmo_cmd_ctx_json_end(&c, doc, data, "texture.extract");
    } else {
        fprintf(c.out, "\nExtracted: %u, Skipped: %u, Warnings: %u\n",
                extracted, skipped, warnings);
    }

    free(tl.objects);
    return nmo_cmd_ctx_done(&c, exit_code);
}
