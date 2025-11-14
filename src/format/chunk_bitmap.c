#include "format/nmo_chunk_api.h"
#include "format/nmo_image_codec.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static nmo_result_t make_error(nmo_error_code_t code, const char *message);
static const char *nmo_chunk_bitmap_default_extension(const nmo_image_codec_t *codec);

static inline nmo_chunk_parser_state_t *nmo_chunk_bitmap_get_state(nmo_chunk_t *chunk) {
    return chunk ? (nmo_chunk_parser_state_t *)chunk->parser_state : NULL;
}

static inline bool nmo_chunk_bitmap_can_read(nmo_chunk_t *chunk, size_t dwords) {
    nmo_chunk_parser_state_t *state = nmo_chunk_bitmap_get_state(chunk);
    return state && (state->current_pos + dwords <= chunk->data_size);
}

static nmo_result_t nmo_chunk_bitmap_map_bytes(nmo_chunk_t *chunk,
                                               size_t size,
                                               const uint8_t **out_ptr) {
    if (!chunk || !out_ptr) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid chunk or output pointer");
    }

    if (size == 0) {
        *out_ptr = NULL;
        return nmo_result_ok();
    }

    size_t dwords = (size + 3u) / 4u;
    if (!nmo_chunk_bitmap_can_read(chunk, dwords)) {
        return make_error(NMO_ERR_EOF, "Insufficient chunk data");
    }

    nmo_chunk_parser_state_t *state = nmo_chunk_bitmap_get_state(chunk);
    const uint8_t *src = (const uint8_t *)&chunk->data[state->current_pos];
    *out_ptr = src;
    state->current_pos += dwords;
    return nmo_result_ok();
}

static void nmo_chunk_bitmap_build_signature(char out_sig[5], const char *extension) {
    out_sig[0] = 'C';
    out_sig[1] = 'K';
    for (size_t i = 0; i < 3; ++i) {
        char ch = (extension && extension[i]) ? extension[i] : ' ';
        out_sig[2 + i] = (char)toupper((unsigned char)ch);
    }
}

static void nmo_chunk_bitmap_extension_from_signature(const uint8_t signature[5],
                                                      char out_ext[4]) {
    if (!signature) {
        out_ext[0] = '\0';
        return;
    }

    for (size_t i = 0; i < 3; ++i) {
        char ch = (char)tolower(signature[2 + i]);
        if (ch < 'a' || ch > 'z') {
            out_ext[i] = '\0';
            while (i < 3) {
                out_ext[i++] = '\0';
            }
            return;
        }
        out_ext[i] = ch;
    }
    out_ext[3] = '\0';
}

static const char *nmo_chunk_bitmap_normalize_extension(const char *extension,
                                                        char out_buffer[4]) {
    if (!extension) {
        out_buffer[0] = '\0';
        return NULL;
    }

    const char *cursor = extension;
    if (cursor[0] == '.') {
        ++cursor;
    }

    size_t len = 0;
    while (cursor[len] && len < 3) {
        out_buffer[len] = (char)tolower((unsigned char)cursor[len]);
        ++len;
    }

    out_buffer[len] = '\0';
    return len > 0 ? out_buffer : NULL;
}

static const nmo_image_codec_t *nmo_chunk_bitmap_resolve_codec(const nmo_bitmap_properties_t *props,
                                                               char extension_buffer[4],
                                                               const char **out_extension) {
    const char *ext_source = (props && props->extension && props->extension[0]) ? props->extension : NULL;
    const char *normalized = nmo_chunk_bitmap_normalize_extension(ext_source, extension_buffer);
    const nmo_image_codec_t *codec = NULL;

    if (normalized) {
        codec = nmo_image_codec_find_by_extension(normalized);
        if (codec && out_extension) {
            *out_extension = normalized;
        }
    }

    if (!codec) {
        nmo_bitmap_format_t requested = props ? props->format : NMO_BITMAP_FORMAT_PNG;
        if (requested <= NMO_BITMAP_FORMAT_RAW || requested >= NMO_BITMAP_FORMAT_COUNT) {
            requested = NMO_BITMAP_FORMAT_PNG;
        }
        codec = nmo_image_codec_get(requested);
        if (codec) {
            const char *default_ext = nmo_chunk_bitmap_default_extension(codec);
            normalized = nmo_chunk_bitmap_normalize_extension(default_ext, extension_buffer);
            if (!normalized) {
                normalized = default_ext;
            }
            if (out_extension) {
                *out_extension = normalized;
            }
        }
    }

    if (!codec && out_extension) {
        *out_extension = NULL;
    }

    return codec;
}

static nmo_result_t nmo_chunk_bitmap_write_legacy_payload(nmo_chunk_t *chunk,
                                                          const char signature[5],
                                                          const uint8_t *encoded_data,
                                                          size_t encoded_size) {
    if (!chunk || !signature) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid legacy payload arguments");
    }

    size_t total_size = encoded_size + 5u;
    if (total_size == 0) {
        return nmo_result_ok();
    }

    size_t dwords = (total_size + 3u) / 4u;
    nmo_result_t result = nmo_chunk_check_size(chunk, dwords);
    if (result.code != NMO_OK) {
        return result;
    }

    nmo_chunk_parser_state_t *state = nmo_chunk_bitmap_get_state(chunk);
    if (!state) {
        return make_error(NMO_ERR_INVALID_STATE, "Chunk parser state missing");
    }

    uint8_t *dest = (uint8_t *)&chunk->data[state->current_pos];
    memcpy(dest, signature, 5);
    if (encoded_size > 0 && encoded_data) {
        memcpy(dest + 5, encoded_data, encoded_size);
    }

    size_t padding = dwords * 4u - total_size;
    if (padding > 0u) {
        memset(dest + total_size, 0, padding);
    }

    state->current_pos += dwords;
    if (state->current_pos > chunk->data_size) {
        chunk->data_size = state->current_pos;
    }

    return nmo_result_ok();
}

static inline uint32_t nmo_chunk_bitmap_pack_argb(uint8_t r,
                             uint8_t g,
                             uint8_t b,
                             uint8_t a) {
    return ((uint32_t)a << 24) |
        ((uint32_t)r << 16) |
        ((uint32_t)g << 8) |
        (uint32_t)b;
}

enum {
    NMO_BITMAP_STORE_NONE = 0,
    NMO_BITMAP_STORE_ENCODED = 1,
    NMO_BITMAP_STORE_ENCODED_WITH_ALPHA = 2
};

enum {
    NMO_BITMAP_ALPHA_CONSTANT = 0,
    NMO_BITMAP_ALPHA_PLANE = 1
};

static const char *nmo_chunk_bitmap_default_extension(const nmo_image_codec_t *codec) {
    if (!codec) {
        return NULL;
    }
    for (size_t i = 0; i < 4; ++i) {
        if (codec->extensions[i]) {
            return codec->extensions[i];
        }
    }
    return codec->name;
}

static void nmo_chunk_bitmap_extension_bytes(char out_bytes[4], const char *ext) {
    memset(out_bytes, 0, 4);
    if (!ext) {
        return;
    }
    for (size_t i = 0; i < 3 && ext[i]; ++i) {
        out_bytes[i] = (char)toupper((unsigned char)ext[i]);
    }
}

static void nmo_chunk_bitmap_tag_to_string(uint32_t tag, char out_ext[4]) {
    out_ext[0] = (char)(tag & 0xFFu);
    out_ext[1] = (char)((tag >> 8) & 0xFFu);
    out_ext[2] = (char)((tag >> 16) & 0xFFu);
    out_ext[3] = '\0';
}

static uint32_t nmo_chunk_bitmap_extension_tag(const char bytes[4]) {
    return (uint32_t)(uint8_t)bytes[0] |
           ((uint32_t)(uint8_t)bytes[1] << 8u) |
           ((uint32_t)(uint8_t)bytes[2] << 16u);
}

static nmo_result_t nmo_chunk_bitmap_check_desc(const nmo_image_desc_t *desc) {
    if (!desc || desc->width <= 0 || desc->height <= 0 || !desc->image_data) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid image descriptor");
    }
    if (desc->height > 0 && desc->width > INT32_MAX / desc->height) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Bitmap dimensions overflow");
    }
    return nmo_result_ok();
}

static inline uint32_t nmo_chunk_bitmap_read_pixel(const uint8_t *ptr, int bytes_per_pixel) {
    switch (bytes_per_pixel) {
        case 1:
            return (uint32_t)ptr[0];
        case 2:
            return (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8);
        case 3:
            return (uint32_t)ptr[0] |
                   ((uint32_t)ptr[1] << 8) |
                   ((uint32_t)ptr[2] << 16);
        case 4: {
            uint32_t value;
            memcpy(&value, ptr, sizeof(uint32_t));
            return value;
        }
        default:
            return 0;
    }
}

static nmo_result_t nmo_chunk_bitmap_convert_interleaved(const nmo_image_desc_t *desc,
                                                         int channels,
                                                         nmo_arena_t *arena,
                                                         uint8_t **out_pixels) {
    if (!arena || !out_pixels || (channels != 3 && channels != 4)) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid conversion arguments");
    }

    NMO_RETURN_IF_ERROR(nmo_chunk_bitmap_check_desc(desc));

    int32_t row_stride = desc->bytes_per_line;
    if (row_stride <= 0) {
        row_stride = nmo_image_calc_bytes_per_line(desc->width, desc->bits_per_pixel);
    }
    if (row_stride <= 0) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid row stride");
    }

    int bytes_per_pixel = desc->bits_per_pixel / 8;
    if ((desc->bits_per_pixel % 8) != 0) {
        bytes_per_pixel += 1;
    }
    if (bytes_per_pixel <= 0 || bytes_per_pixel > 4) {
        return make_error(NMO_ERR_NOT_SUPPORTED, "Unsupported source pixel format");
    }

    const size_t pixel_count = (size_t)desc->width * (size_t)desc->height;
    const size_t total_size = pixel_count * (size_t)channels;
    uint8_t *buffer = nmo_arena_alloc(arena, total_size, 16);
    if (!buffer) {
        return make_error(NMO_ERR_NOMEM, "Failed to allocate interleaved buffer");
    }

    nmo_mask_shifts_t shifts;
    nmo_image_calculate_mask_shifts(desc->red_mask,
                                    desc->green_mask,
                                    desc->blue_mask,
                                    desc->alpha_mask,
                                    &shifts);

    const bool has_alpha_mask = desc->alpha_mask != 0;

    for (int y = 0; y < desc->height; ++y) {
        const uint8_t *row = desc->image_data + (size_t)y * (size_t)row_stride;
        for (int x = 0; x < desc->width; ++x) {
            const uint8_t *pixel_ptr = row + (size_t)x * (size_t)bytes_per_pixel;
            const uint32_t raw = nmo_chunk_bitmap_read_pixel(pixel_ptr, bytes_per_pixel);

            const size_t idx = ((size_t)y * (size_t)desc->width + (size_t)x) * (size_t)channels;
            buffer[idx + 0] = nmo_image_extract_channel(raw, desc->red_mask, &shifts, 0);
            buffer[idx + 1] = nmo_image_extract_channel(raw, desc->green_mask, &shifts, 1);
            buffer[idx + 2] = nmo_image_extract_channel(raw, desc->blue_mask, &shifts, 2);
            if (channels == 4) {
                buffer[idx + 3] = has_alpha_mask
                                      ? nmo_image_extract_channel(raw, desc->alpha_mask, &shifts, 3)
                                      : 0xFFu;
            }
        }
    }

    *out_pixels = buffer;
    return nmo_result_ok();
}

static nmo_result_t nmo_chunk_bitmap_copy_rgba_to_rgb(const uint8_t *rgba,
                                                      size_t pixel_count,
                                                      nmo_arena_t *arena,
                                                      uint8_t **out_rgb) {
    if (!rgba || !arena || !out_rgb) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid RGBA copy arguments");
    }

    size_t total = pixel_count * 3u;
    uint8_t *rgb = nmo_arena_alloc(arena, total, 16);
    if (!rgb) {
        return make_error(NMO_ERR_NOMEM, "Failed to allocate RGB buffer");
    }

    for (size_t i = 0; i < pixel_count; ++i) {
        rgb[i * 3u + 0u] = rgba[i * 4u + 0u];
        rgb[i * 3u + 1u] = rgba[i * 4u + 1u];
        rgb[i * 3u + 2u] = rgba[i * 4u + 2u];
    }

    *out_rgb = rgb;
    return nmo_result_ok();
}

static bool nmo_chunk_bitmap_alpha_is_constant(const uint8_t *rgba,
                                               size_t pixel_count,
                                               uint8_t *out_value) {
    if (!rgba || pixel_count == 0) {
        if (out_value) {
            *out_value = 0xFFu;
        }
        return true;
    }

    const uint8_t first = rgba[3];
    for (size_t i = 1; i < pixel_count; ++i) {
        if (rgba[i * 4u + 3u] != first) {
            if (out_value) {
                *out_value = (uint8_t)0;
            }
            return false;
        }
    }
    if (out_value) {
        *out_value = first;
    }
    return true;
}

static nmo_result_t nmo_chunk_bitmap_extract_alpha_plane(const uint8_t *rgba,
                                                         size_t pixel_count,
                                                         nmo_arena_t *arena,
                                                         uint8_t **out_plane) {
    if (!rgba || !arena || !out_plane) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid alpha extraction arguments");
    }

    uint8_t *plane = nmo_arena_alloc(arena, pixel_count, 1);
    if (!plane) {
        return make_error(NMO_ERR_NOMEM, "Failed to allocate alpha plane");
    }

    for (size_t i = 0; i < pixel_count; ++i) {
        plane[i] = rgba[i * 4u + 3u];
    }

    *out_plane = plane;
    return nmo_result_ok();
}

static nmo_result_t write_empty_bitmap_marker(nmo_chunk_t *chunk) {
    return nmo_chunk_write_int(chunk, 0);
}

static nmo_result_t make_error(nmo_error_code_t code, const char *message) {
    return nmo_result_error(NMO_ERROR(NULL, code, NMO_SEVERITY_ERROR, message));
}

nmo_result_t nmo_chunk_write_raw_bitmap(nmo_chunk_t *chunk,
                                        const nmo_image_desc_t *desc) {
    if (!chunk || !desc) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid chunk or descriptor");
    }

    if (desc->width <= 0 || desc->height <= 0 || desc->bits_per_pixel <= 0 ||
        !desc->image_data) {
        return write_empty_bitmap_marker(chunk);
    }

    if (chunk->arena == NULL) {
        return make_error(NMO_ERR_INVALID_STATE, "Chunk missing arena for allocations");
    }

    const int32_t bpp = desc->bits_per_pixel;
    if ((bpp % 8) != 0 || bpp > 32) {
        return make_error(NMO_ERR_NOT_SUPPORTED, "Only up to 32bpp raw bitmaps are supported");
    }

    const int32_t bytes_per_pixel = bpp / 8;
    if (bytes_per_pixel <= 0 || bytes_per_pixel > 4) {
        return make_error(NMO_ERR_NOT_SUPPORTED, "Unsupported bytes-per-pixel value");
    }

    if (desc->height > 0 && desc->width > INT32_MAX / desc->height) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Bitmap dimensions overflow");
    }

    const size_t plane_size = (size_t)desc->width * (size_t)desc->height;
    if (plane_size == 0 || plane_size > UINT32_MAX) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid bitmap plane size");
    }

    int32_t row_stride = desc->bytes_per_line;
    if (row_stride <= 0) {
        row_stride = nmo_image_calc_bytes_per_line(desc->width, desc->bits_per_pixel);
    }
    if (row_stride <= 0) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid bitmap row stride");
    }

    const size_t min_row_bytes = (size_t)desc->width * (size_t)bytes_per_pixel;
    if ((size_t)row_stride < min_row_bytes) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Row stride smaller than image width");
    }

    nmo_result_t result = nmo_chunk_write_int(chunk, desc->bits_per_pixel);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_write_int(chunk, desc->width);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_write_int(chunk, desc->height);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_write_dword(chunk, desc->alpha_mask);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_write_dword(chunk, desc->red_mask);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_write_dword(chunk, desc->green_mask);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_write_dword(chunk, desc->blue_mask);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_write_dword(chunk, 0u);
    NMO_RETURN_IF_ERROR(result);

    uint8_t *r_plane = nmo_arena_alloc(chunk->arena, plane_size, 1);
    uint8_t *g_plane = nmo_arena_alloc(chunk->arena, plane_size, 1);
    uint8_t *b_plane = nmo_arena_alloc(chunk->arena, plane_size, 1);
    const bool has_alpha = desc->alpha_mask != 0u;
    uint8_t *a_plane = has_alpha ? nmo_arena_alloc(chunk->arena, plane_size, 1) : NULL;

    if (!r_plane || !g_plane || !b_plane || (has_alpha && !a_plane)) {
        return make_error(NMO_ERR_NOMEM, "Failed to allocate bitmap planes");
    }

    nmo_mask_shifts_t shifts;
    nmo_image_calculate_mask_shifts(desc->red_mask,
                                    desc->green_mask,
                                    desc->blue_mask,
                                    desc->alpha_mask,
                                    &shifts);

    for (int y = 0; y < desc->height; ++y) {
        const size_t plane_offset = (size_t)y * (size_t)desc->width;
        const uint8_t *row = desc->image_data +
                             (size_t)(desc->height - 1 - y) * (size_t)row_stride;

        for (int x = 0; x < desc->width; ++x) {
            const uint8_t *pixel_ptr = row + (size_t)x * (size_t)bytes_per_pixel;
            uint32_t pixel = 0;

            switch (bytes_per_pixel) {
                case 1:
                    pixel = pixel_ptr[0];
                    break;
                case 2:
                    pixel = (uint32_t)pixel_ptr[0] |
                            ((uint32_t)pixel_ptr[1] << 8);
                    break;
                case 3:
                    pixel = (uint32_t)pixel_ptr[0] |
                            ((uint32_t)pixel_ptr[1] << 8) |
                            ((uint32_t)pixel_ptr[2] << 16);
                    break;
                case 4:
                    memcpy(&pixel, pixel_ptr, sizeof(uint32_t));
                    break;
                default:
                    return make_error(NMO_ERR_NOT_SUPPORTED, "Unsupported pixel format");
            }

            r_plane[plane_offset + (size_t)x] =
                nmo_image_extract_channel(pixel, desc->red_mask, &shifts, 0);
            g_plane[plane_offset + (size_t)x] =
                nmo_image_extract_channel(pixel, desc->green_mask, &shifts, 1);
            b_plane[plane_offset + (size_t)x] =
                nmo_image_extract_channel(pixel, desc->blue_mask, &shifts, 2);
            if (has_alpha) {
                a_plane[plane_offset + (size_t)x] =
                    nmo_image_extract_channel(pixel, desc->alpha_mask, &shifts, 3);
            }
        }
    }

    result = nmo_chunk_write_buffer(chunk, r_plane, plane_size);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_write_buffer(chunk, g_plane, plane_size);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_write_buffer(chunk, b_plane, plane_size);
    NMO_RETURN_IF_ERROR(result);

    if (has_alpha) {
        result = nmo_chunk_write_buffer(chunk, a_plane, plane_size);
    } else {
        result = nmo_chunk_write_buffer(chunk, NULL, 0u);
    }

    return result;
}

nmo_result_t nmo_chunk_write_encoded_bitmap(nmo_chunk_t *chunk,
                                            const nmo_image_desc_t *desc,
                                            const nmo_bitmap_properties_t *props) {
    if (!chunk || !desc || !desc->image_data) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid chunk or descriptor");
    }

    if (chunk->arena == NULL) {
        return make_error(NMO_ERR_INVALID_STATE, "Chunk missing arena for allocations");
    }

    nmo_bitmap_format_t format = props ? props->format : NMO_BITMAP_FORMAT_PNG;
    if (format == NMO_BITMAP_FORMAT_RAW) {
        return nmo_chunk_write_raw_bitmap(chunk, desc);
    }

    const nmo_image_codec_t *codec = nmo_image_codec_get(format);
    if (!codec) {
        return make_error(NMO_ERR_NOT_SUPPORTED, "Bitmap codec not available");
    }

    uint8_t *rgba_pixels = NULL;
    nmo_result_t result = nmo_chunk_bitmap_convert_interleaved(desc, 4, chunk->arena, &rgba_pixels);
    NMO_RETURN_IF_ERROR(result);

    uint8_t *encoded_rgb = NULL;
    size_t encoded_rgb_size = 0;
    uint8_t *alpha_plane = NULL;
    size_t alpha_plane_size = 0;
    uint8_t alpha_constant = 0xFFu;
    int alpha_kind = NMO_BITMAP_ALPHA_CONSTANT;

    const size_t pixel_count = (size_t)desc->width * (size_t)desc->height;

    if (codec->supports_alpha) {
        result = codec->encode(rgba_pixels,
                               desc->width,
                               desc->height,
                               4,
                               props,
                               chunk->arena,
                               &encoded_rgb,
                               &encoded_rgb_size);
        NMO_RETURN_IF_ERROR(result);

        result = nmo_chunk_write_int(chunk, NMO_BITMAP_STORE_ENCODED);
        NMO_RETURN_IF_ERROR(result);
    } else {
        result = nmo_chunk_bitmap_copy_rgba_to_rgb(rgba_pixels, pixel_count, chunk->arena, &encoded_rgb);
        NMO_RETURN_IF_ERROR(result);

        nmo_bitmap_properties_t rgb_props = props ? *props : (nmo_bitmap_properties_t){0};
        rgb_props.save_alpha = false;

        result = codec->encode(encoded_rgb,
                               desc->width,
                               desc->height,
                               3,
                               &rgb_props,
                               chunk->arena,
                               &encoded_rgb,
                               &encoded_rgb_size);
        NMO_RETURN_IF_ERROR(result);

        if (nmo_chunk_bitmap_alpha_is_constant(rgba_pixels, pixel_count, &alpha_constant)) {
            alpha_kind = NMO_BITMAP_ALPHA_CONSTANT;
        } else {
            alpha_kind = NMO_BITMAP_ALPHA_PLANE;
            result = nmo_chunk_bitmap_extract_alpha_plane(rgba_pixels, pixel_count, chunk->arena, &alpha_plane);
            NMO_RETURN_IF_ERROR(result);
            alpha_plane_size = pixel_count;
        }

        result = nmo_chunk_write_int(chunk, NMO_BITMAP_STORE_ENCODED_WITH_ALPHA);
        NMO_RETURN_IF_ERROR(result);
    }

    char ext_bytes[4];
    nmo_chunk_bitmap_extension_bytes(ext_bytes, nmo_chunk_bitmap_default_extension(codec));
    uint32_t extension_tag = nmo_chunk_bitmap_extension_tag(ext_bytes);
    result = nmo_chunk_write_dword(chunk, extension_tag);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_int(chunk, desc->width);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_write_int(chunk, desc->height);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_buffer(chunk, encoded_rgb, encoded_rgb_size);
    NMO_RETURN_IF_ERROR(result);

    if (!codec->supports_alpha) {
        result = nmo_chunk_write_int(chunk, alpha_kind);
        NMO_RETURN_IF_ERROR(result);

        if (alpha_kind == NMO_BITMAP_ALPHA_CONSTANT) {
            result = nmo_chunk_write_byte(chunk, alpha_constant);
            NMO_RETURN_IF_ERROR(result);
        } else {
            result = nmo_chunk_write_buffer(chunk, alpha_plane, alpha_plane_size);
            NMO_RETURN_IF_ERROR(result);
        }
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_bitmap_legacy(nmo_chunk_t *chunk,
                                           const nmo_image_desc_t *desc,
                                           const nmo_bitmap_properties_t *props) {
    if (!chunk || !desc || !desc->image_data) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid chunk or descriptor");
    }

    if (chunk->arena == NULL) {
        return make_error(NMO_ERR_INVALID_STATE, "Chunk missing arena for allocations");
    }

    NMO_RETURN_IF_ERROR(nmo_chunk_bitmap_check_desc(desc));

    char extension_buffer[4] = {0};
    const char *normalized_ext = NULL;
    const nmo_image_codec_t *codec = nmo_chunk_bitmap_resolve_codec(props, extension_buffer, &normalized_ext);
    if (!codec || !normalized_ext) {
        return make_error(NMO_ERR_NOT_SUPPORTED, "Legacy bitmap codec unavailable");
    }

    uint8_t *rgba_pixels = NULL;
    nmo_result_t result = nmo_chunk_bitmap_convert_interleaved(desc, 4, chunk->arena, &rgba_pixels);
    NMO_RETURN_IF_ERROR(result);

    const size_t pixel_count = (size_t)desc->width * (size_t)desc->height;
    uint8_t *encode_pixels = rgba_pixels;
    int encode_channels = 4;

    if (!codec->supports_alpha) {
        result = nmo_chunk_bitmap_copy_rgba_to_rgb(rgba_pixels, pixel_count, chunk->arena, &encode_pixels);
        NMO_RETURN_IF_ERROR(result);
        encode_channels = 3;
    }

    nmo_bitmap_properties_t effective_props = {0};
    if (props) {
        effective_props = *props;
    }
    effective_props.format = codec->format;
    effective_props.extension = normalized_ext;
    effective_props.save_alpha = codec->supports_alpha;

    uint8_t *encoded_data = NULL;
    size_t encoded_size = 0;
    result = codec->encode(encode_pixels,
                           desc->width,
                           desc->height,
                           encode_channels,
                           &effective_props,
                           chunk->arena,
                           &encoded_data,
                           &encoded_size);
    NMO_RETURN_IF_ERROR(result);

    if (encoded_size > (size_t)INT32_MAX - 5u) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Legacy bitmap payload too large");
    }

    size_t total_size = encoded_size + 5u;
    result = nmo_chunk_write_int(chunk, (int32_t)total_size);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_write_int(chunk, (int32_t)total_size);
    NMO_RETURN_IF_ERROR(result);

    char signature[5];
    nmo_chunk_bitmap_build_signature(signature, normalized_ext);

    return nmo_chunk_bitmap_write_legacy_payload(chunk, signature, encoded_data, encoded_size);
}

nmo_result_t nmo_chunk_read_raw_bitmap(nmo_chunk_t *chunk,
                                       nmo_image_desc_t *out_desc,
                                       uint8_t **out_pixels) {
    if (!chunk || !out_desc || !out_pixels) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid chunk or output arguments");
    }

    *out_pixels = NULL;
    memset(out_desc, 0, sizeof(*out_desc));

    int32_t original_bpp = 0;
    nmo_result_t result = nmo_chunk_read_int(chunk, &original_bpp);
    NMO_RETURN_IF_ERROR(result);

    if (original_bpp == 0) {
        return nmo_result_ok();
    }

    int32_t width = 0;
    int32_t height = 0;
    uint32_t alpha_mask = 0;
    uint32_t red_mask = 0;
    uint32_t green_mask = 0;
    uint32_t blue_mask = 0;
    uint32_t compression = 0;

    result = nmo_chunk_read_int(chunk, &width);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_int(chunk, &height);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_dword(chunk, &alpha_mask);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_dword(chunk, &red_mask);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_dword(chunk, &green_mask);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_dword(chunk, &blue_mask);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_dword(chunk, &compression);
    NMO_RETURN_IF_ERROR(result);

    if (compression != 0u) {
        return make_error(NMO_ERR_NOT_SUPPORTED, "Compressed raw bitmaps are not supported");
    }

    if (width <= 0 || height <= 0) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid bitmap dimensions");
    }

    if (height > 0 && width > INT32_MAX / height) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Bitmap dimensions overflow");
    }

    const size_t plane_size = (size_t)width * (size_t)height;
    if (plane_size == 0 || plane_size > UINT32_MAX) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid plane size while reading");
    }

    uint8_t *r_plane = NULL;
    uint8_t *g_plane = NULL;
    uint8_t *b_plane = NULL;
    uint8_t *a_plane = NULL;
    size_t r_size = 0;
    size_t g_size = 0;
    size_t b_size = 0;
    size_t a_size = 0;

    result = nmo_chunk_read_buffer(chunk, (void **)&r_plane, &r_size);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_buffer(chunk, (void **)&g_plane, &g_size);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_buffer(chunk, (void **)&b_plane, &b_size);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_buffer(chunk, (void **)&a_plane, &a_size);
    NMO_RETURN_IF_ERROR(result);

    if (r_size != plane_size || g_size != plane_size || b_size != plane_size) {
        return make_error(NMO_ERR_CORRUPT, "Bitmap plane size mismatch");
    }

    const bool has_alpha_plane = (a_size == plane_size && a_plane != NULL);

    const int32_t bytes_per_line = nmo_image_calc_bytes_per_line(width, 32);
    if (bytes_per_line <= 0) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid output row stride");
    }

    const size_t total_size = (size_t)bytes_per_line * (size_t)height;
    uint8_t *pixels = nmo_arena_alloc(chunk->arena, total_size, 16);
    if (!pixels) {
        return make_error(NMO_ERR_NOMEM, "Failed to allocate ARGB bitmap buffer");
    }

    for (int row_index = 0; row_index < height; ++row_index) {
        const size_t plane_offset = (size_t)row_index * (size_t)width;
        const size_t dest_row = (size_t)(height - 1 - row_index);
        uint32_t *dst = (uint32_t *)(pixels + dest_row * (size_t)bytes_per_line);

        for (int x = 0; x < width; ++x) {
            uint8_t a = has_alpha_plane ? a_plane[plane_offset + (size_t)x] : 0xFFu;
            uint8_t r = r_plane[plane_offset + (size_t)x];
            uint8_t g = g_plane[plane_offset + (size_t)x];
            uint8_t b = b_plane[plane_offset + (size_t)x];
            dst[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                     ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    nmo_image_desc_init_argb32(out_desc, width, height);
    out_desc->bits_per_pixel = 32;
    out_desc->bytes_per_line = bytes_per_line;
    out_desc->alpha_mask = 0xFF000000u;
    out_desc->red_mask = 0x00FF0000u;
    out_desc->green_mask = 0x0000FF00u;
    out_desc->blue_mask = 0x000000FFu;
    out_desc->image_data = pixels;
    *out_pixels = pixels;

    (void)original_bpp;

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_encoded_bitmap(nmo_chunk_t *chunk,
                                           nmo_image_desc_t *out_desc,
                                           uint8_t **out_pixels) {
    if (!chunk || !out_desc || !out_pixels) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid chunk or output arguments");
    }

    *out_pixels = NULL;
    memset(out_desc, 0, sizeof(*out_desc));

    int32_t storage_type = 0;
    nmo_result_t result = nmo_chunk_read_int(chunk, &storage_type);
    NMO_RETURN_IF_ERROR(result);

    if (storage_type == NMO_BITMAP_STORE_NONE) {
        return nmo_result_ok();
    }

    uint32_t extension_tag = 0;
    result = nmo_chunk_read_dword(chunk, &extension_tag);
    NMO_RETURN_IF_ERROR(result);

    char ext_bytes[4];
    nmo_chunk_bitmap_tag_to_string(extension_tag, ext_bytes);

    int32_t width = 0;
    int32_t height = 0;
    result = nmo_chunk_read_int(chunk, &width);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_int(chunk, &height);
    NMO_RETURN_IF_ERROR(result);

    if (width <= 0 || height <= 0) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid bitmap dimensions");
    }

    void *encoded_data = NULL;
    size_t encoded_size = 0;
    result = nmo_chunk_read_buffer(chunk, &encoded_data, &encoded_size);
    NMO_RETURN_IF_ERROR(result);
    if (!encoded_data || encoded_size == 0) {
        return make_error(NMO_ERR_CORRUPT, "Encoded bitmap payload missing");
    }

    int desired_channels = (storage_type == NMO_BITMAP_STORE_ENCODED) ? 4 : 3;
    const nmo_image_codec_t *codec = nmo_image_codec_find_by_extension(ext_bytes);
    if (!codec) {
        return make_error(NMO_ERR_NOT_SUPPORTED, "Unknown bitmap extension");
    }

    int decoded_width = 0;
    int decoded_height = 0;
    int decoded_channels = 0;
    uint8_t *decoded_pixels = NULL;
    result = codec->decode(encoded_data,
                           encoded_size,
                           desired_channels,
                           chunk->arena,
                           &decoded_width,
                           &decoded_height,
                           &decoded_pixels,
                           &decoded_channels);
    NMO_RETURN_IF_ERROR(result);

    if (decoded_width != width || decoded_height != height) {
        return make_error(NMO_ERR_CORRUPT, "Decoded bitmap dimensions mismatch");
    }

    const size_t pixel_count = (size_t)width * (size_t)height;
    const size_t total_bytes = pixel_count * 4u;
    uint8_t *final_pixels = nmo_arena_alloc(chunk->arena, total_bytes, 16);
    if (!final_pixels) {
        return make_error(NMO_ERR_NOMEM, "Failed to allocate decoded bitmap buffer");
    }

    uint32_t *dst = (uint32_t *)final_pixels;

    if (storage_type == NMO_BITMAP_STORE_ENCODED_WITH_ALPHA) {
        int32_t alpha_kind = 0;
        result = nmo_chunk_read_int(chunk, &alpha_kind);
        NMO_RETURN_IF_ERROR(result);

        uint8_t constant_alpha = 0xFFu;
        uint8_t *alpha_plane = NULL;

        if (alpha_kind == NMO_BITMAP_ALPHA_CONSTANT) {
            result = nmo_chunk_read_byte(chunk, &constant_alpha);
            NMO_RETURN_IF_ERROR(result);
        } else if (alpha_kind == NMO_BITMAP_ALPHA_PLANE) {
            void *alpha_data = NULL;
            size_t alpha_size = 0;
            result = nmo_chunk_read_buffer(chunk, &alpha_data, &alpha_size);
            NMO_RETURN_IF_ERROR(result);
            if (!alpha_data || alpha_size != pixel_count) {
                return make_error(NMO_ERR_CORRUPT, "Invalid alpha plane data");
            }
            alpha_plane = (uint8_t *)alpha_data;
        } else {
            return make_error(NMO_ERR_CORRUPT, "Unknown alpha storage kind");
        }

        for (size_t i = 0; i < pixel_count; ++i) {
            uint8_t r = decoded_pixels[i * 3u + 0u];
            uint8_t g = decoded_pixels[i * 3u + 1u];
            uint8_t b = decoded_pixels[i * 3u + 2u];
            uint8_t a = alpha_plane ? alpha_plane[i] : constant_alpha;
            dst[i] = nmo_chunk_bitmap_pack_argb(r, g, b, a);
        }
    } else {
        if (decoded_channels == 4) {
            for (size_t i = 0; i < pixel_count; ++i) {
                uint8_t r = decoded_pixels[i * 4u + 0u];
                uint8_t g = decoded_pixels[i * 4u + 1u];
                uint8_t b = decoded_pixels[i * 4u + 2u];
                uint8_t a = decoded_pixels[i * 4u + 3u];
                dst[i] = nmo_chunk_bitmap_pack_argb(r, g, b, a);
            }
        } else {
            for (size_t i = 0; i < pixel_count; ++i) {
                uint8_t r = decoded_pixels[i * 3u + 0u];
                uint8_t g = decoded_pixels[i * 3u + 1u];
                uint8_t b = decoded_pixels[i * 3u + 2u];
                dst[i] = nmo_chunk_bitmap_pack_argb(r, g, b, 0xFFu);
            }
        }
    }

    nmo_image_desc_init_argb32(out_desc, width, height);
    out_desc->image_data = final_pixels;
    *out_pixels = final_pixels;

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_bitmap_legacy(nmo_chunk_t *chunk,
                                          nmo_image_desc_t *out_desc,
                                          uint8_t **out_pixels) {
    if (!chunk || !out_desc || !out_pixels) {
        return make_error(NMO_ERR_INVALID_ARGUMENT, "Invalid chunk or output arguments");
    }

    *out_pixels = NULL;
    memset(out_desc, 0, sizeof(*out_desc));

    int32_t total_a = 0;
    int32_t total_b = 0;
    nmo_result_t result = nmo_chunk_read_int(chunk, &total_a);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_int(chunk, &total_b);
    NMO_RETURN_IF_ERROR(result);

    if (total_a != total_b) {
        return make_error(NMO_ERR_CORRUPT, "Legacy bitmap size mismatch");
    }

    if (total_a <= 0) {
        return nmo_result_ok();
    }

    if (total_a < 5) {
        return make_error(NMO_ERR_CORRUPT, "Legacy bitmap payload too small");
    }

    const uint8_t *payload = NULL;
    result = nmo_chunk_bitmap_map_bytes(chunk, (size_t)total_a, &payload);
    NMO_RETURN_IF_ERROR(result);

    if (!payload) {
        return make_error(NMO_ERR_CORRUPT, "Legacy bitmap payload missing");
    }

    char signature[5];
    memcpy(signature, payload, 5);
    if (signature[0] != 'C' || signature[1] != 'K') {
        return make_error(NMO_ERR_CORRUPT, "Legacy bitmap signature invalid");
    }

    char extension[4] = {0};
    nmo_chunk_bitmap_extension_from_signature((const uint8_t *)signature, extension);
    if (extension[0] == '\0') {
        return make_error(NMO_ERR_CORRUPT, "Legacy bitmap extension missing");
    }

    const nmo_image_codec_t *codec = nmo_image_codec_find_by_extension(extension);
    if (!codec) {
        return make_error(NMO_ERR_NOT_SUPPORTED, "Legacy bitmap codec not available");
    }

    size_t encoded_size = (size_t)total_a - 5u;
    const uint8_t *encoded_data = payload + 5;
    if (encoded_size == 0) {
        return make_error(NMO_ERR_CORRUPT, "Legacy bitmap payload empty");
    }

    int decoded_width = 0;
    int decoded_height = 0;
    int decoded_channels = 0;
    uint8_t *decoded_pixels = NULL;
    result = codec->decode(encoded_data,
                           encoded_size,
                           4,
                           chunk->arena,
                           &decoded_width,
                           &decoded_height,
                           &decoded_pixels,
                           &decoded_channels);
    NMO_RETURN_IF_ERROR(result);

    if (decoded_width <= 0 || decoded_height <= 0) {
        return make_error(NMO_ERR_CORRUPT, "Legacy bitmap decoded dimensions invalid");
    }

    size_t pixel_count = (size_t)decoded_width * (size_t)decoded_height;
    uint8_t *final_pixels = nmo_arena_alloc(chunk->arena, pixel_count * 4u, 16);
    if (!final_pixels) {
        return make_error(NMO_ERR_NOMEM, "Failed to allocate decoded bitmap buffer");
    }

    uint32_t *dst = (uint32_t *)final_pixels;
    if (decoded_channels >= 4) {
        for (size_t i = 0; i < pixel_count; ++i) {
            uint8_t r = decoded_pixels[i * 4u + 0u];
            uint8_t g = decoded_pixels[i * 4u + 1u];
            uint8_t b = decoded_pixels[i * 4u + 2u];
            uint8_t a = decoded_pixels[i * 4u + 3u];
            dst[i] = nmo_chunk_bitmap_pack_argb(r, g, b, a);
        }
    } else if (decoded_channels == 3) {
        for (size_t i = 0; i < pixel_count; ++i) {
            uint8_t r = decoded_pixels[i * 3u + 0u];
            uint8_t g = decoded_pixels[i * 3u + 1u];
            uint8_t b = decoded_pixels[i * 3u + 2u];
            dst[i] = nmo_chunk_bitmap_pack_argb(r, g, b, 0xFFu);
        }
    } else if (decoded_channels == 1) {
        for (size_t i = 0; i < pixel_count; ++i) {
            uint8_t gray = decoded_pixels[i];
            dst[i] = nmo_chunk_bitmap_pack_argb(gray, gray, gray, 0xFFu);
        }
    } else {
        return make_error(NMO_ERR_NOT_SUPPORTED, "Unsupported decoded channel count");
    }

    nmo_image_desc_init_argb32(out_desc, decoded_width, decoded_height);
    out_desc->image_data = final_pixels;
    *out_pixels = final_pixels;
    return nmo_result_ok();
}
