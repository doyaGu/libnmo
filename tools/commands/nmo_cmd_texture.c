/**
 * @file nmo_cmd_texture.c
 * @brief CLI texture command group implementation
 */

#include "nmo_cmd_texture.h"
#include "nmo_cmd_object_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_write.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_record.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "object/nmo_asset_edit.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "format/nmo_stb_adapter.h"
#include "format/nmo_image.h"
#include "core/nmo_arena.h"
#include "document/nmo_document_save.h"

#include <stdarg.h>
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

static int nmo_cmd_texture_extract_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

int nmo_cmd_texture_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: texture list|show|extract ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "list") == 0 || strcmp(argv[0], "ls") == 0) {
        return nmo_cmd_object_list_class_in_session(ctx, argc, argv, "CKTexture");
    }
    if (strcmp(argv[0], "show") == 0 || strcmp(argv[0], "s") == 0) {
        return nmo_cmd_object_show_class_in_session(
            ctx, argc, argv, NMO_CID_TEXTURE, "CKTexture");
    }
    if (strcmp(argv[0], "extract") == 0 || strcmp(argv[0], "x") == 0) {
        return nmo_cmd_texture_extract_in_session(ctx, argc, argv);
    }

    fprintf(stderr, "Unsupported texture read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}

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

static const char *save_options_str(uint32_t opts) {
    if (opts & NMO_CKTEXTURE_EXTERNAL)         return "external";
    if (opts & NMO_CKTEXTURE_IMAGEFORMAT)      return "imageformat";
    if (opts & NMO_CKTEXTURE_INCLUDEORIGINALFILE) return "include_original";
    if (opts & NMO_CKTEXTURE_USEGLOBAL)        return "use_global";
    return "rawdata";
}

static bool is_external_texture(const nmo_texture_state_t *ts) {
    return (ts->save_options & NMO_CKTEXTURE_EXTERNAL) != 0;
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

/* Returns 0 when the directory exists or was created; otherwise -1 with a
 * malloc'd message in *out_error (free with free()). */
static int tex_ensure_dir(const char *dir_path, char **out_error) {
    if (!dir_path || !*dir_path) return -1;
#ifdef _WIN32
    if (_mkdir(dir_path) == 0) return 0;
#else
    if (mkdir(dir_path, 0755) == 0) return 0;
#endif
    if (errno == EEXIST) return 0;
    if (out_error) {
        *out_error = nmo_tool_strdup_fmt("Failed to create directory '%s' (%s)",
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
 * Output file name for a texture: "<name>_<id>.<ext>" with /\\:*?"<>| and
 * control characters replaced by '_', or "texture_<id>.<ext>" when unnamed.
 * malloc'd to the exact length; NULL on OOM.
 */
static char *tex_filename_dup(const char *name, nmo_object_id_t id, const char *ext) {
    if (!name || !name[0]) {
        return nmo_tool_strdup_fmt("texture_%u.%s", id, ext);
    }
    char *safe = nmo_tool_strdup(name);
    if (!safe) {
        return NULL;
    }
    for (unsigned char *p = (unsigned char *)safe; *p; ++p) {
        unsigned char ch = *p;
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' ||
            ch == '?' || ch == '"' || ch == '<' || ch == '>' ||
            ch == '|' || ch < 0x20) {
            *p = '_';
        }
    }
    char *fname = nmo_tool_strdup_fmt("%s_%u.%s", safe, id, ext);
    free(safe);
    return fname;
}

static void texture_display_dimensions(const nmo_texture_state_t *ts,
                                       int32_t *out_width,
                                       int32_t *out_height) {
    int32_t width = 0;
    int32_t height = 0;
    if (ts != NULL) {
        width = ts->reader_width;
        height = ts->reader_height;
        if ((width <= 0 || height <= 0) &&
            ts->bitmap_kind == CKTEXTURE_BITMAP_RAW &&
            ts->raw_slots != NULL &&
            ts->slot_count > 0) {
            width = ts->raw_slots[0].width;
            height = ts->raw_slots[0].height;
        }
        if ((width <= 0 || height <= 0) &&
            ts->user_mipmaps != NULL &&
            ts->user_mipmap_count > 0) {
            width = ts->user_mipmaps[0].width;
            height = ts->user_mipmaps[0].height;
        }
    }
    if (out_width != NULL) {
        *out_width = width;
    }
    if (out_height != NULL) {
        *out_height = height;
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

/* One texture row. JSON key order and text column order differ historically
 * (name is second in JSON, last in text), so name is added twice. */
static bool texture_list_build_record(nmo_object_t *obj, nmo_cli_record_t *rec)
{
    const char *name = nmo_object_get_name(obj);
    bool ok = nmo_cli_record_uint(rec, "id", "ID", nmo_object_get_id(obj));
    ok = ok && nmo_cli_record_str_opt(rec, "name", NULL, name, NULL);

    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    if (chunk) {
        ok = ok && nmo_cli_record_uint(rec, "size", "SIZE",
                                       (uint64_t)nmo_chunk_get_data_size(chunk));
    } else {
        ok = ok && nmo_cli_record_text(rec, "SIZE", "-");
    }

    const nmo_texture_state_t *ts =
        (const nmo_texture_state_t *)nmo_object_get_state(obj);
    if (ts) {
        int32_t width = 0;
        int32_t height = 0;
        texture_display_dimensions(ts, &width, &height);
        ok = ok && nmo_cli_record_int(rec, "width", NULL, width);
        ok = ok && nmo_cli_record_int(rec, "height", NULL, height);
        ok = ok && ((width > 0 && height > 0)
                        ? nmo_cli_record_text_fmt(rec, "DIMS", "%dx%d", width, height)
                        : nmo_cli_record_text(rec, "DIMS", "-"));
        ok = ok && nmo_cli_record_str(rec, "bitmap_kind", NULL,
                                      bitmap_kind_str(ts->bitmap_kind));
        ok = ok && nmo_cli_record_text(rec, "FORMAT", format_label(ts));
        ok = ok && nmo_cli_record_uint(rec, "slot_count", "SLOTS", ts->slot_count);
        ok = ok && nmo_cli_record_bool(rec, "is_external", NULL, is_external_texture(ts));
    } else {
        ok = ok && nmo_cli_record_text(rec, "DIMS", "-");
        ok = ok && nmo_cli_record_text(rec, "FORMAT", "-");
        ok = ok && nmo_cli_record_text(rec, "SLOTS", "-");
    }
    ok = ok && nmo_cli_record_text(rec, "NAME", (name && name[0]) ? name : "-");
    return ok;
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
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, 3, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *sort_by = nmo_opt_str(&vals[0]);
    uint32_t top_n = nmo_opt_uint_or(&vals[1], 0);
    bool reverse = vals[2].val.flag;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Collect texture objects */
    nmo_object_query_t query = {0};
    nmo_core_query_set_class_id(&query, NMO_CID_TEXTURE, true);

    texture_list_t tl = {0};
    nmo_core_iter_result_t iter_result = {0};
    nmo_core_object_query_run(&c, &query, collect_texture_visitor, &tl, &iter_result);

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

    static const nmo_cli_table_col_t columns[] = {
        {"ID",     NMO_CLI_ALIGN_RIGHT, 5,  0},
        {"SIZE",   NMO_CLI_ALIGN_RIGHT, 8,  0},
        {"DIMS",   NMO_CLI_ALIGN_LEFT,  11, 0},
        {"FORMAT", NMO_CLI_ALIGN_LEFT,  10, 0},
        {"SLOTS",  NMO_CLI_ALIGN_RIGHT, 5,  0},
        {"NAME",   NMO_CLI_ALIGN_LEFT,  20, 50},
    };

    nmo_cli_record_t *rec = nmo_cli_record_new();
    nmo_cli_record_uint(rec, "count", NULL, (uint64_t)display_count);
    nmo_cli_record_uint(rec, "total", NULL, (uint64_t)tl.count);
    if (display_count < tl.count) {
        nmo_cli_record_raw_fmt(rec, "Textures: %zu (of %zu total)\n\n",
                               display_count, tl.count);
    } else {
        nmo_cli_record_raw_fmt(rec, "Textures: %zu\n\n", display_count);
    }
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, "textures", NULL);
    nmo_cli_record_array_set_table(arr, columns, sizeof(columns) / sizeof(columns[0]));

    for (size_t i = 0; i < display_count; ++i) {
        nmo_cli_record_t *item = nmo_cli_record_new();
        if (!item) {
            continue;
        }
        if (!texture_list_build_record(tl.objects[i], item) ||
            !nmo_cli_record_array_add(arr, item)) {
            nmo_cli_record_free(item);
        }
    }

    free(tl.objects);
    rc = nmo_cmd_ctx_emit_record(&c, rec, "texture.list", 0, c.colorize);
    return nmo_cmd_ctx_done(&c, rc);
}

static bool texture_record_yes_no(nmo_cli_record_t *rec, const char *key,
                                  const char *label, bool value)
{
    return nmo_cli_record_bool(rec, key, label, value) &&
           nmo_cli_record_set_text(rec, value ? "yes" : "no");
}

/* Per-slot details: JSON "slots" array, text "Slots:" block. */
static bool texture_record_slots(nmo_cli_record_t *rec, const nmo_texture_state_t *ts)
{
    nmo_cli_record_array_t *slots = nmo_cli_record_array(rec, "slots", NULL);
    bool ok = slots != NULL;
    if (ok && ts->slot_count > 0) {
        ok = nmo_cli_record_raw(rec, "\nSlots:\n");
    }
    for (uint32_t i = 0; ok && i < ts->slot_count; ++i) {
        nmo_cli_record_t *slot = nmo_cli_record_new();
        ok = slot != NULL && nmo_cli_record_uint(slot, "index", NULL, i) &&
             nmo_cli_record_raw_fmt(rec, "  Slot %u:\n", i);
        if (!ok) {
            nmo_cli_record_free(slot);
            break;
        }
        switch (ts->bitmap_kind) {
        case CKTEXTURE_BITMAP_READER:
            if (ts->reader_slots) {
                const nmo_texture_reader_slot_t *rs = &ts->reader_slots[i];
                ok = nmo_cli_record_str(slot, "type", NULL, "reader") &&
                     nmo_cli_record_uint(slot, "data_size", NULL, rs->data_size) &&
                     nmo_cli_record_uint(slot, "format_type", NULL, rs->format_type) &&
                     nmo_cli_record_uint(slot, "extension", NULL, rs->extension) &&
                     nmo_cli_record_raw_fmt(rec,
                                         "    type:       reader\n"
                                         "    data_size:  %u\n"
                                         "    format:     %u\n"
                                         "    extension:  %u\n",
                                         rs->data_size, rs->format_type, rs->extension);
                if (ok && rs->alpha_plane_size > 0) {
                    ok = nmo_cli_record_uint(slot, "alpha_plane_size", NULL,
                                             rs->alpha_plane_size) &&
                         nmo_cli_record_raw_fmt(rec,
                                             "    alpha_size: %u\n", rs->alpha_plane_size);
                }
            }
            break;
        case CKTEXTURE_BITMAP_RAW:
            if (ts->raw_slots) {
                const nmo_texture_raw_slot_t *rs = &ts->raw_slots[i];
                ok = nmo_cli_record_str(slot, "type", NULL, "raw") &&
                     nmo_cli_record_int(slot, "width", NULL, rs->width) &&
                     nmo_cli_record_int(slot, "height", NULL, rs->height) &&
                     nmo_cli_record_int(slot, "bits_per_pixel", NULL, rs->bits_per_pixel);
                ok = ok && nmo_cli_record_hex32(slot, "red_mask", NULL, rs->red_mask) &&
                     nmo_cli_record_hex32(slot, "green_mask", NULL, rs->green_mask) &&
                     nmo_cli_record_hex32(slot, "blue_mask", NULL, rs->blue_mask) &&
                     nmo_cli_record_hex32(slot, "alpha_mask", NULL, rs->alpha_mask);
                ok = ok && nmo_cli_record_uint(slot, "red_size", NULL, rs->red_size) &&
                     nmo_cli_record_uint(slot, "green_size", NULL, rs->green_size) &&
                     nmo_cli_record_uint(slot, "blue_size", NULL, rs->blue_size) &&
                     nmo_cli_record_uint(slot, "alpha_size", NULL, rs->alpha_size);
                ok = ok && nmo_cli_record_raw_fmt(rec,
                        "    type:       raw\n"
                        "    dims:       %dx%d @ %d bpp\n"
                        "    masks:      R=0x%08X G=0x%08X B=0x%08X A=0x%08X\n"
                        "    channels:   R=%u G=%u B=%u A=%u bytes\n",
                        rs->width, rs->height, rs->bits_per_pixel,
                        rs->red_mask, rs->green_mask, rs->blue_mask, rs->alpha_mask,
                        rs->red_size, rs->green_size, rs->blue_size, rs->alpha_size);
            }
            break;
        case CKTEXTURE_BITMAP_BITMAP2:
            if (ts->bitmap2_slots) {
                const nmo_texture_bitmap2_slot_t *bs = &ts->bitmap2_slots[i];
                ok = nmo_cli_record_str(slot, "type", NULL, "bitmap2") &&
                     nmo_cli_record_uint(slot, "buffer_size", NULL, bs->buffer_size) &&
                     nmo_cli_record_raw_fmt(rec,
                                         "    type:       bitmap2\n"
                                         "    buf_size:   %u\n", bs->buffer_size);
            }
            break;
        default:
            ok = nmo_cli_record_str(slot, "type", NULL, "none") &&
                 nmo_cli_record_raw_fmt(rec, "    type:       none\n");
            break;
        }
        ok = nmo_cli_record_array_add(slots, slot) && ok;
    }
    return ok;
}

static bool texture_show_build_record(const nmo_cmd_ctx_t *c,
                                      nmo_cli_record_t *rec,
                                      nmo_object_id_t object_id,
                                      nmo_class_id_t class_id,
                                      const char *name,
                                      const nmo_texture_state_t *ts)
{
    const char *class_name = nmo_core_class_name(c, class_id);

    bool ok = nmo_cli_record_uint(rec, "id", NULL, object_id);
    ok = ok && nmo_cli_record_str_opt(rec, "name", NULL, name, NULL);
    ok = ok && nmo_cli_record_text_fmt(rec, "ID / Name", "#%u (%s)", object_id,
                                       (name && name[0]) ? name : "(unnamed)");
    ok = ok && nmo_cli_record_uint(rec, "class_id", NULL, class_id);
    ok = ok && nmo_cli_record_str_opt(rec, "class_name", NULL, class_name, NULL);
    ok = ok && nmo_cli_record_text_fmt(rec, "Class", "#%u (%s)", class_id,
                                       class_name ? class_name : "-");
    if (!ok) {
        return false;
    }
    if (!ts) {
        return nmo_cli_record_null(rec, "state", NULL, NULL);
    }

    ok = nmo_cli_record_int(rec, "reader_width", NULL, ts->reader_width);
    ok = ok && nmo_cli_record_int(rec, "reader_height", NULL, ts->reader_height);
    ok = ok && ((ts->reader_width > 0 && ts->reader_height > 0)
                    ? nmo_cli_record_text_fmt(rec, "Dimensions", "%dx%d",
                                              ts->reader_width, ts->reader_height)
                    : nmo_cli_record_text(rec, "Dimensions", "-"));
    ok = ok && nmo_cli_record_int(rec, "reader_bpp", "BPP", ts->reader_bpp);
    ok = ok && nmo_cli_record_str(rec, "bitmap_kind", "Bitmap Kind",
                                  bitmap_kind_str(ts->bitmap_kind));
    ok = ok && nmo_cli_record_uint(rec, "slot_count", NULL, ts->slot_count);
    ok = ok && nmo_cli_record_str(rec, "save_options", "Save Options",
                                  save_options_str(ts->save_options));
    ok = ok && nmo_cli_record_uint(rec, "save_options_raw", NULL, ts->save_options);
    ok = ok && nmo_cli_record_text_fmt(rec, "Slot Count", "%u", ts->slot_count);
    ok = ok && nmo_cli_record_uint(rec, "mipmap_level", "Mipmap Level", ts->mipmap_level);
    ok = ok && texture_record_yes_no(rec, "is_transparent", "Transparent",
                                     ts->is_transparent != 0);
    ok = ok && texture_record_yes_no(rec, "is_cubemap", "Cubemap", ts->is_cubemap != 0);
    ok = ok && texture_record_yes_no(rec, "is_external", "External",
                                     is_external_texture(ts));

    if (ts->has_desired_video_format) {
        ok = ok && nmo_cli_record_uint(rec, "desired_video_format", "Video Format",
                                       ts->desired_video_format);
    }
    if (ts->has_transparent_color) {
        ok = ok && nmo_cli_record_hex32(rec, "transparent_color", "Transp. Color",
                                        ts->transparent_color);
    }
    if (ts->has_current_slot) {
        ok = ok && nmo_cli_record_int(rec, "current_slot", "Current Slot", ts->current_slot);
    }
    if (ts->has_pick_threshold) {
        ok = ok && nmo_cli_record_int(rec, "pick_threshold", "Pick Threshold",
                                      ts->pick_threshold);
    }
    if (ts->has_movie_filename && ts->movie_filename) {
        ok = ok && nmo_cli_record_str(rec, "movie_filename", "Movie File", ts->movie_filename);
    }

    if (ok && ts->has_slot_filenames && ts->slot_filenames) {
        ok = nmo_cli_record_raw(rec, "\nSlot Filenames:\n");
        for (uint32_t i = 0; ok && i < ts->slot_count; ++i) {
            ok = nmo_cli_record_raw_fmt(rec, "  [%u] %s\n", i,
                                     ts->slot_filenames[i] ? ts->slot_filenames[i] : "(null)");
        }
        ok = ok && nmo_cli_record_str_list(rec, "slot_filenames", NULL,
                                           (const char *const *)ts->slot_filenames,
                                           ts->slot_count, NULL);
    }

    ok = ok && texture_record_slots(rec, ts);
    return ok;
}

/* ============================================================================
 * texture show
 * ============================================================================ */

int nmo_cmd_texture_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--id", NULL, NMO_OPT_UINT, "Texture object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Texture object name"},
    };
    enum { OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_t *obj = NULL;
    nmo_object_id_t object_id = 0;
    nmo_core_object_selector_t selector = {
        .has_id = vals[OPT_ID].present,
        .id = nmo_opt_uint_or(&vals[OPT_ID], 0),
        .positional_id = (!vals[OPT_ID].present && !vals[OPT_NAME].present && r.pos_count >= 1)
            ? r.pos_args[0]
            : NULL,
        .name = nmo_opt_str(&vals[OPT_NAME]),
        .required_base_class = NMO_CID_TEXTURE,
        .selector_label = "Texture",
        .type_label = "CKTexture",
    };
    rc = nmo_core_resolve_one_object(&c, &selector, &obj, &object_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo texture show [--id <id> | --name <name> | <id>] <file>\n");
        return nmo_cmd_ctx_done(&c, rc);
    }

    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const char *name = nmo_object_get_name(obj);
    const nmo_texture_state_t *ts =
        (const nmo_texture_state_t *)nmo_object_get_state(obj);

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec || !nmo_cli_record_title(rec, "Texture Details") ||
        !texture_show_build_record(&c, rec, object_id, class_id, name, ts)) {
        nmo_cli_record_free(rec);
        fprintf(stderr, "Error: Out of memory while describing texture %u\n", object_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    if (!ts) {
        nmo_cli_record_raw(rec, "\n  (no deserialized state)\n");
    }

    rc = nmo_cmd_ctx_emit_record(&c, rec, "texture.show", 18, c.colorize);
    return nmo_cmd_ctx_done(&c, rc);
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

    uint8_t *pixels = NULL;
    int channels = 0;
    nmo_status_t st = nmo_image_reconstruct_pixels(
        rs->red_data, rs->green_data, rs->blue_data, rs->alpha_data,
        rs->red_size, rs->green_size, rs->blue_size, rs->alpha_size,
        rs->width, rs->height, rs->bits_per_pixel,
        arena, &pixels, &channels);
    if (st != NMO_OK) return NULL;

    *out_w = rs->width;
    *out_h = rs->height;
    *out_ch = channels;
    return pixels;
}

/**
 * Check whether a raw slot holds compressed (e.g. DXT) data.
 */
static bool raw_slot_is_compressed(const nmo_texture_raw_slot_t *rs) {
    return rs->compression != 0;
}

/**
 * Map the raw slot compression field to an nmo_pixel_format_t DXT format.
 * Falls back to desired_video_format if the compression field is non-standard.
 */
static nmo_pixel_format_t dxt_format_from_raw_slot(
    const nmo_texture_raw_slot_t *rs, uint32_t desired_video_format) {
    /* Common VX_TEXTURECOMPRESSION values match DXT type directly */
    switch (rs->compression) {
    case 1: return NMO_PIXEL_FORMAT_DXT1;
    case 3: return NMO_PIXEL_FORMAT_DXT3;
    case 5: return NMO_PIXEL_FORMAT_DXT5;
    default: break;
    }
    /* Fall back to desired_video_format if it's a DXT format */
    if (desired_video_format >= NMO_PIXEL_FORMAT_DXT1 &&
        desired_video_format <= NMO_PIXEL_FORMAT_DXT5) {
        return (nmo_pixel_format_t)desired_video_format;
    }
    return NMO_PIXEL_FORMAT_DXT1; /* last resort */
}

/**
 * Decode a DXT-compressed raw slot to RGBA pixels.
 * The compressed data is reconstituted from the channel buffers (typically
 * stored entirely in blue_data by the Virtools serializer).
 */
static uint8_t *decode_raw_slot_dxt(nmo_arena_t *arena,
                                    const nmo_texture_raw_slot_t *rs,
                                    uint32_t desired_video_format,
                                    int *out_w, int *out_h, int *out_ch) {
    if (rs->width <= 0 || rs->height <= 0) return NULL;

    nmo_pixel_format_t dxt_fmt = dxt_format_from_raw_slot(rs, desired_video_format);

    /* Reconstitute contiguous DXT data from the 4 channel buffers.
     * Virtools typically stores the full DXT stream in blue_data (first buffer).
     * If data is split, concatenate all non-empty buffers. */
    const uint8_t *bufs[] = { rs->blue_data, rs->green_data, rs->red_data, rs->alpha_data };
    const uint32_t sizes[] = { rs->blue_size, rs->green_size, rs->red_size, rs->alpha_size };
    size_t total_size = 0;
    int nonempty_count = 0;
    int first_nonempty = -1;
    for (int i = 0; i < 4; i++) {
        if (bufs[i] && sizes[i] > 0) {
            total_size += sizes[i];
            if (first_nonempty < 0) first_nonempty = i;
            nonempty_count++;
        }
    }
    if (total_size == 0 || first_nonempty < 0) return NULL;

    const uint8_t *dxt_data;
    if (nonempty_count == 1) {
        /* Fast path: single buffer, no copy needed */
        dxt_data = bufs[first_nonempty];
    } else {
        /* Concatenate multiple buffers */
        uint8_t *concat = (uint8_t *)nmo_arena_alloc(arena, total_size, 1);
        if (!concat) return NULL;
        size_t offset = 0;
        for (int i = 0; i < 4; i++) {
            if (bufs[i] && sizes[i] > 0) {
                memcpy(concat + offset, bufs[i], sizes[i]);
                offset += sizes[i];
            }
        }
        dxt_data = concat;
    }

    uint8_t *rgba = NULL;
    nmo_status_t st = nmo_image_decode_dxt(
        dxt_data, total_size,
        rs->width, rs->height,
        dxt_fmt, arena, &rgba);
    if (st != NMO_OK) return NULL;

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

typedef struct texture_extract_args {
    const char *out_dir;
    bool has_id;
    uint32_t id;
    const char *name_pat;
    nmo_bitmap_format_t out_format;
    const char *ext;
    uint32_t quality;
    bool overwrite;
} texture_extract_args_t;

static int texture_extract_parse(int argc, char **argv,
                                 bool expect_file_operand,
                                 texture_extract_args_t *args,
                                 const char *usage) {
    memset(args, 0, sizeof(*args));

    static const nmo_opt_def_t opts[] = {
        {"--out-dir",   "-d", NMO_OPT_STRING, "Output directory"},
        {"--id",        NULL,  NMO_OPT_UINT,   "Extract single texture by ID"},
        {"--name",      "-n", NMO_OPT_STRING, "Filter by name wildcard"},
        {"--format",    "-f", NMO_OPT_STRING, "Output format: png, bmp, tga, jpg (default: png)"},
        {"--quality",   "-q", NMO_OPT_UINT,   "JPEG quality (1-100, default: 90)"},
        {"--overwrite", NULL,  NMO_OPT_FLAG,   "Overwrite existing files"},
    };
    nmo_opt_val_t vals[6];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, 6, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    if (!expect_file_operand && r.pos_count != 0) {
        fprintf(stderr, "Error: Unexpected argument '%s'\n", r.pos_args[0]);
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    args->out_dir = nmo_opt_str(&vals[0]);
    args->has_id = vals[1].present;
    args->id = nmo_opt_uint_or(&vals[1], 0);
    args->name_pat = nmo_opt_str(&vals[2]);
    const char *fmt_str   = nmo_opt_str(&vals[3]);
    args->quality = nmo_opt_uint_or(&vals[4], 90);
    args->overwrite = nmo_opt_flag(&vals[5]);

    if (!args->out_dir || !*args->out_dir) {
        fprintf(stderr, "Error: Missing --out-dir\n");
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    args->out_format = parse_format_name(fmt_str);
    args->ext = format_ext(args->out_format);

    if (args->quality < 1) args->quality = 1;
    if (args->quality > 100) args->quality = 100;

    return NMO_CLI_EXIT_SUCCESS;
}

/* Start an extract entry holding the id, name and (when known) output path. */
static nmo_cli_record_t *texture_extract_entry_new(nmo_object_id_t id,
                                                   const char *name,
                                                   const char *path)
{
    nmo_cli_record_t *e = nmo_cli_record_new();
    nmo_cli_record_uint(e, "id", NULL, id);
    nmo_cli_record_str(e, "name", NULL, name ? name : "");
    if (path) {
        nmo_cli_record_str(e, "path", NULL, path);
    }
    return e;
}

static void texture_extract_entry_status(nmo_cli_record_t *e, const char *status,
                                         const char *reason)
{
    nmo_cli_record_str(e, "status", NULL, status);
    nmo_cli_record_str(e, "reason", NULL, reason);
}

static int texture_extract_run(nmo_cmd_ctx_t *ctx,
                               const texture_extract_args_t *args,
                               bool close_ctx) {
    nmo_cmd_ctx_t c = *ctx;

    char *dir_error = NULL;
    if (tex_ensure_dir(args->out_dir, &dir_error) != 0) {
        fprintf(stderr, "Error: %s\n", dir_error ? dir_error : "Failed to create directory");
        free(dir_error);
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR)
                         : NMO_CLI_EXIT_IO_ERROR;
    }

    /* Collect textures */
    nmo_object_query_t query = {0};
    nmo_core_query_set_class_id(&query, NMO_CID_TEXTURE, true);
    if (args->has_id) {
        nmo_core_query_set_object_id(&query, args->id);
    }
    if (args->name_pat) {
        nmo_core_query_set_name_wildcard(&query, args->name_pat);
    }

    texture_list_t tl = {0};
    nmo_core_iter_result_t iter_result = {0};
    nmo_core_object_query_run(&c, &query, collect_texture_visitor, &tl, &iter_result);

    if (tl.count == 0) {
        if (args->has_id) {
            fprintf(stderr, "Error: Texture %u not found\n", args->id);
        } else {
            fprintf(stderr, "No textures found\n");
        }
        free(tl.objects);
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR)
                         : NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_arena_t *arena = nmo_tool_owner_arena(c.workspace);

    uint32_t extracted = 0;
    uint32_t skipped = 0;
    uint32_t warnings = 0;

    nmo_cli_record_t **entries =
        (nmo_cli_record_t **)calloc(tl.count, sizeof(*entries));
    if (!entries) {
        fprintf(stderr, "Error: Out of memory\n");
        free(tl.objects);
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    for (size_t ti = 0; ti < tl.count; ++ti) {
        nmo_object_t *obj = tl.objects[ti];
        nmo_object_id_t id = nmo_object_get_id(obj);
        const char *name = nmo_object_get_name(obj);
        const char *label = (name && name[0]) ? name : "(unnamed)";

        const nmo_texture_state_t *ts =
            (const nmo_texture_state_t *)nmo_object_get_state(obj);

        /* No state? */
        if (!ts) {
            skipped++;
            nmo_cli_record_t *e = texture_extract_entry_new(id, name, NULL);
            entries[ti] = e;
            texture_extract_entry_status(e, "skip", "no_state");
            nmo_cli_record_set_summary_fmt(e, "  [SKIP] %u %s -> no state", id, label);
            continue;
        }

        /* External textures */
        if (ts->bitmap_kind == CKTEXTURE_BITMAP_NONE && is_external_texture(ts)) {
            skipped++;
            nmo_cli_record_t *e = texture_extract_entry_new(id, name, NULL);
            entries[ti] = e;
            texture_extract_entry_status(e, "skip", "external");
            nmo_cli_record_set_summary_fmt(e, "  [SKIP] %u %s -> external texture", id, label);
            continue;
        }

        /* No slots */
        if (ts->slot_count == 0) {
            skipped++;
            nmo_cli_record_t *e = texture_extract_entry_new(id, name, NULL);
            entries[ti] = e;
            texture_extract_entry_status(e, "skip", "no_slots");
            nmo_cli_record_set_summary_fmt(e, "  [SKIP] %u %s -> no slots", id, label);
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
                const nmo_texture_raw_slot_t *rs0 = &ts->raw_slots[0];
                if (raw_slot_is_compressed(rs0)) {
                    pixels = decode_raw_slot_dxt(arena, rs0,
                        ts->desired_video_format, &w, &h, &ch);
                    if (!pixels) skip_reason = "dxt_decode_failed";
                } else {
                    pixels = decode_raw_slot(arena, rs0, &w, &h, &ch);
                    if (!pixels) {
                        skip_reason = (rs0->bits_per_pixel != 32)
                            ? "unsupported_raw_bpp"
                            : "decode_failed";
                    }
                }
            } else {
                skip_reason = "no_raw_data";
            }
            break;

        case CKTEXTURE_BITMAP_BITMAP2:
            if (ts->bitmap2_slots) {
                pixels = decode_bitmap2_slot(arena, &ts->bitmap2_slots[0], &w, &h, &ch);
                if (!pixels) {
                    /* stb_image failed -- try interleaved raw pixel decode.
                     * Use reader dimensions and desired_video_format as hints. */
                    const nmo_texture_bitmap2_slot_t *bs = &ts->bitmap2_slots[0];
                    int32_t expected_bpl = (ts->reader_width > 0 && ts->reader_bpp > 0)
                        ? nmo_image_calc_bytes_per_line(ts->reader_width, ts->reader_bpp) : 0;
                    size_t expected_size = (expected_bpl > 0 && ts->reader_height > 0)
                        ? (size_t)expected_bpl * (size_t)ts->reader_height : 0;
                    if (bs->buffer && bs->buffer_size > 0 &&
                        expected_size > 0 && bs->buffer_size >= expected_size &&
                        ts->reader_width > 0 && ts->reader_height > 0 &&
                        ts->reader_bpp > 0) {
                        nmo_image_desc_t desc;
                        memset(&desc, 0, sizeof(desc));
                        desc.format = (nmo_pixel_format_t)ts->desired_video_format;
                        desc.width = ts->reader_width;
                        desc.height = ts->reader_height;
                        desc.bits_per_pixel = ts->reader_bpp;
                        desc.bytes_per_line = nmo_image_calc_bytes_per_line(
                            desc.width, desc.bits_per_pixel);
                        desc.image_data = bs->buffer;

                        uint8_t *rgba = NULL;
                        int dw = 0, dh = 0;
                        nmo_status_t ist = nmo_image_decode_interleaved_to_rgba32(
                            &desc, arena, &rgba, &dw, &dh);
                        if (ist == NMO_OK && rgba) {
                            pixels = rgba;
                            w = dw;
                            h = dh;
                            ch = 4;
                        }
                    }
                    if (!pixels) skip_reason = "bitmap2_decode_failed";
                }
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
            nmo_cli_record_t *e = texture_extract_entry_new(id, name, NULL);
            entries[ti] = e;
            texture_extract_entry_status(e, "warn", skip_reason);
            nmo_cli_record_set_summary_fmt(e, "  [WARN] %u %s -> %s", id, label,
                                           skip_reason);
            continue;
        }

        /* Encode to output format */
        size_t out_size = 0;
        uint8_t *encoded = nmo_stbi_write_to_memory(
            arena, args->out_format, w, h, ch, pixels, (int)args->quality, &out_size);

        if (!encoded || out_size == 0) {
            warnings++;
            nmo_cli_record_t *e = texture_extract_entry_new(id, name, NULL);
            entries[ti] = e;
            texture_extract_entry_status(e, "warn", "encode_failed");
            nmo_cli_record_set_summary_fmt(e, "  [WARN] %u %s -> encode failed", id, label);
            continue;
        }

        /* Write to file */
        char *fname = tex_filename_dup(name, id, args->ext);
        char *path = fname ? tex_join_path(args->out_dir, fname) : NULL;
        if (!path) {
            free(fname);
            warnings++;
            continue;
        }

        if (!args->overwrite && tex_file_exists(path)) {
            skipped++;
            nmo_cli_record_t *e = texture_extract_entry_new(id, name, path);
            entries[ti] = e;
            texture_extract_entry_status(e, "skip", "exists");
            nmo_cli_record_set_summary_fmt(e, "  [SKIP] %u %s -> exists (use --overwrite)", id, label);
            free(path);
            free(fname);
            continue;
        }

        FILE *fp = fopen(path, "wb");
        if (!fp) {
            warnings++;
            nmo_cli_record_t *e = texture_extract_entry_new(id, name, path);
            entries[ti] = e;
            texture_extract_entry_status(e, "warn", "open_failed");
            nmo_cli_record_set_summary_fmt(e, "  [WARN] %u %s -> open failed (%s)", id, label, strerror(errno));
            free(path);
            free(fname);
            continue;
        }

        size_t written = fwrite(encoded, 1, out_size, fp);
        fclose(fp);

        if (written != out_size) {
            warnings++;
            nmo_cli_record_t *e = texture_extract_entry_new(id, name, path);
            entries[ti] = e;
            texture_extract_entry_status(e, "warn", "write_failed");
            nmo_cli_record_set_summary_fmt(e, "  [WARN] %u %s -> write failed", id, label);
            free(path);
            free(fname);
            continue;
        }

        extracted++;
        nmo_cli_record_t *e = texture_extract_entry_new(id, name, path);
        entries[ti] = e;
        nmo_cli_record_int(e, "width", NULL, w);
        nmo_cli_record_int(e, "height", NULL, h);
        nmo_cli_record_uint(e, "file_size", NULL, (uint64_t)out_size);
        nmo_cli_record_str(e, "status", NULL, "ok");
        nmo_cli_record_set_summary_fmt(e, "  [OK]   %u %s -> %s (%dx%d, %zu bytes)",
                                       id, label, fname, w, h, out_size);
        free(path);
        free(fname);
    }

    int exit_code = (warnings > 0 && extracted == 0)
        ? NMO_CLI_EXIT_IO_ERROR : NMO_CLI_EXIT_SUCCESS;

    nmo_cli_record_t *rec = nmo_cli_record_new();
    nmo_cli_record_str(rec, "out_dir", NULL, args->out_dir);
    nmo_cli_record_str(rec, "format", NULL, args->ext);
    nmo_cli_record_raw_fmt(rec, "Extracting textures to: %s (format: %s)\n",
                           args->out_dir, args->ext);
    nmo_cli_record_uint(rec, "extracted", NULL, extracted);
    nmo_cli_record_uint(rec, "skipped", NULL, skipped);
    nmo_cli_record_uint(rec, "warnings", NULL, warnings);
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, "entries", NULL);
    nmo_cli_record_array_omit_heading(arr);
    for (size_t k = 0; k < tl.count; ++k) {
        if (entries[k] && !nmo_cli_record_array_add(arr, entries[k])) {
            nmo_cli_record_free(entries[k]);
        }
    }
    free(entries);
    nmo_cli_record_raw_fmt(rec, "\nExtracted: %u, Skipped: %u, Warnings: %u\n",
                           extracted, skipped, warnings);

    int emit_rc = nmo_cmd_ctx_emit_record(&c, rec, "texture.extract", 0, false);
    if (emit_rc != NMO_CLI_EXIT_SUCCESS) {
        exit_code = emit_rc;
    }

    free(tl.objects);
    return close_ctx ? nmo_cmd_ctx_done(&c, exit_code) : exit_code;
}

int nmo_cmd_texture_extract(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    texture_extract_args_t args;
    const char *usage = "nmo texture extract --out-dir <dir> [options] <file>";
    int rc = texture_extract_parse(argc, argv, true, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    return texture_extract_run(&c, &args, true);
}

static int nmo_cmd_texture_extract_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv) {
    texture_extract_args_t args;
    const char *usage = "texture extract --out-dir <dir> [options]";
    int rc = texture_extract_parse(argc, argv, false, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    return texture_extract_run(ctx, &args, false);
}

/* ============================================================================
 * texture replace
 * ============================================================================ */

int nmo_cmd_texture_replace(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--file",    "-f", NMO_OPT_STRING, "Image file to load"},
        NMO_OPT_DEF_WRITE_OUTPUT,
        NMO_OPT_DEF_DRY_RUN,
        {"--id",      NULL,  NMO_OPT_UINT,   "Texture object ID"},
        {"--name",    "-n", NMO_OPT_STRING, "Texture object name"},
    };
    enum { OPT_FILE, OPT_OUTPUT, OPT_DRYRUN, OPT_ID, OPT_NAME, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *image_path  = nmo_opt_str(&vals[OPT_FILE]);
    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run            = nmo_opt_flag(&vals[OPT_DRYRUN]);

    /* Positional: <id> <nmo-file>, or just <nmo-file> with --id/--name. */
    const char *positional_id = NULL;
    const char *file_path = NULL;
    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    if (has_selector_opt) {
        if (r.pos_count >= 1) {
            file_path = r.pos_args[r.pos_count - 1];
        }
    } else if (r.pos_count >= 2) {
        positional_id = r.pos_args[0];
        file_path = r.pos_args[r.pos_count - 1];
    }

    if (!file_path) {
        fprintf(stderr, "Error: Expected <nmo-file>\n");
        fprintf(stderr, "Usage: nmo texture replace [--id <id> | --name <name> | <id>] --file <image> <nmo-file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!image_path) {
        fprintf(stderr, "Error: --file is required\n");
        fprintf(stderr, "Usage: nmo texture replace [--id <id> | --name <name> | <id>] --file <image> <nmo-file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!dry_run && !output_path) {
        fprintf(stderr, "Error: -o/--output is required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Read image file from disk */
    FILE *img_fp = fopen(image_path, "rb");
    if (!img_fp) {
        fprintf(stderr, "Error: Cannot open image file '%s': %s\n",
                image_path, strerror(errno));
        return NMO_CLI_EXIT_IO_ERROR;
    }
    fseek(img_fp, 0, SEEK_END);
    long img_file_size = ftell(img_fp);
    fseek(img_fp, 0, SEEK_SET);
    if (img_file_size <= 0 || img_file_size > (long)(256 * 1024 * 1024)) {
        fprintf(stderr, "Error: Image file size invalid or too large (%ld bytes)\n",
                img_file_size);
        fclose(img_fp);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    uint8_t *img_data = (uint8_t *)malloc((size_t)img_file_size);
    if (!img_data) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(img_fp);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    size_t read_bytes = fread(img_data, 1, (size_t)img_file_size, img_fp);
    fclose(img_fp);
    if (read_bytes != (size_t)img_file_size) {
        fprintf(stderr, "Error: Failed to read image file\n");
        free(img_data);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Open session */
    nmo_cmd_ctx_t c;
    int rc;
    if (file_path) {
        rc = nmo_cli_write_init_ctx(&c, file_path, global);
    } else {
        rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    }
    if (rc) { free(img_data); return rc; }

    nmo_arena_t *arena = nmo_tool_owner_arena(c.workspace);

    /* Decode image via stb_image (force RGBA) */
    int img_w = 0, img_h = 0, img_ch = 0;
    uint8_t *pixels = nmo_stbi_load_from_memory(
        arena, img_data, (int)img_file_size, &img_w, &img_h, &img_ch, 4);
    free(img_data);

    if (!pixels || img_w <= 0 || img_h <= 0) {
        fprintf(stderr, "Error: Failed to decode image '%s'\n", image_path);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    nmo_object_t *obj = NULL;
    nmo_object_id_t object_id = 0;
    nmo_core_object_selector_t selector = {
        .has_id = vals[OPT_ID].present,
        .id = nmo_opt_uint_or(&vals[OPT_ID], 0),
        .positional_id = positional_id,
        .name = nmo_opt_str(&vals[OPT_NAME]),
        .required_base_class = NMO_CID_TEXTURE,
        .selector_label = "Texture",
        .type_label = "CKTexture",
    };
    rc = nmo_core_resolve_one_object(&c, &selector, &obj, &object_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo texture replace [--id <id> | --name <name> | <id>] --file <image> <nmo-file> -o <output>\n");
        return nmo_cmd_ctx_done(&c, rc);
    }

    nmo_texture_state_t *ts =
        (nmo_texture_state_t *)nmo_object_get_state(obj);
    if (!ts) {
        fprintf(stderr, "Error: No texture state for object %u\n", object_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Record old dimensions for display */
    int32_t old_w = ts->reader_width;
    int32_t old_h = ts->reader_height;
    CKTEXTURE_BITMAP_KIND old_kind = ts->bitmap_kind;

    nmo_workspace_edit_t *edit = NULL;
    nmo_status_t edit_rc = nmo_workspace_edit_begin(c.workspace, "texture.replace", &edit);
    if (edit_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin texture edit: %s\n",
                nmo_error_string(edit_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    nmo_status_t replace_rc = nmo_asset_edit_set_texture_rgba(
        edit, object_id, pixels, (uint32_t)img_w, (uint32_t)img_h);
    if (replace_rc != NMO_OK) {
        nmo_workspace_edit_rollback(edit);
        fprintf(stderr, "Error: Failed to replace bitmap: %s\n",
                nmo_error_string(replace_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    edit_rc = nmo_workspace_edit_commit(edit);
    if (edit_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to commit texture edit: %s\n",
                nmo_error_string(edit_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Get encoded size for display */
    size_t encoded_size = 0;
    if (ts->reader_slots)
        encoded_size = ts->reader_slots[0].data_size;

    const char *name = nmo_object_get_name(obj);
    int exit_code = NMO_CLI_EXIT_SUCCESS;

    /* Save */
    bool saved = false;
    if (!dry_run && output_path) {
        nmo_save_options_t save_opts = nmo_tool_owner_save_options_default();
        int save_rc = nmo_cli_save_document(c.document, output_path, &save_opts);
        if (save_rc != NMO_CLI_EXIT_SUCCESS) {
            exit_code = save_rc;
        } else {
            saved = true;
        }
    }

    /* Output */
    nmo_cli_record_t *rec = nmo_cli_record_new();
    nmo_cli_record_uint(rec, "id", NULL, object_id);
    nmo_cli_record_str(rec, "name", NULL, name);
    if (name && name[0]) {
        nmo_cli_record_raw_fmt(rec, "Texture #%u (%s)\n", object_id, name);
    } else {
        nmo_cli_record_raw_fmt(rec, "Texture #%u\n", object_id);
    }
    nmo_cli_record_str(rec, "image_file", NULL, image_path);
    nmo_cli_record_raw_fmt(rec, "  Image:    %s\n", image_path);

    nmo_cli_record_t *old_dims = nmo_cli_record_object(rec, "old");
    nmo_cli_record_int(old_dims, "width", NULL, old_w);
    nmo_cli_record_int(old_dims, "height", NULL, old_h);
    nmo_cli_record_str(old_dims, "bitmap_kind", NULL, bitmap_kind_str(old_kind));
    nmo_cli_record_raw_fmt(rec, "  Old dims: %dx%d (%s)\n", old_w, old_h,
                           bitmap_kind_str(old_kind));

    nmo_cli_record_t *new_dims = nmo_cli_record_object(rec, "new");
    nmo_cli_record_int(new_dims, "width", NULL, img_w);
    nmo_cli_record_int(new_dims, "height", NULL, img_h);
    nmo_cli_record_uint(new_dims, "encoded_size", NULL, (uint64_t)encoded_size);
    nmo_cli_record_str(new_dims, "bitmap_kind", NULL, "reader");
    nmo_cli_record_raw_fmt(rec, "  New dims: %dx%d (reader, %zu bytes PNG)\n",
                           img_w, img_h, encoded_size);

    nmo_cli_record_bool(rec, "dry_run", NULL, dry_run);
    if (dry_run) {
        nmo_cli_record_raw(rec, "  (dry run - not saved)\n");
    } else if (output_path) {
        nmo_cli_record_str(rec, "output", NULL, output_path);
    }
    if (saved) {
        nmo_cli_record_raw_fmt(rec, "Saved to: %s\n", output_path);
    }

    int emit_rc = nmo_cmd_ctx_emit_record(&c, rec, "texture.replace", 0, false);
    if (emit_rc != NMO_CLI_EXIT_SUCCESS && exit_code == NMO_CLI_EXIT_SUCCESS) {
        exit_code = emit_rc;
    }

    return nmo_cmd_ctx_done(&c, exit_code);
}
