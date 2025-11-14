#include "format/nmo_image_codec.h"

#include "format/nmo_stb_adapter.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#define NMO_MAX_CODEC_EXTENSIONS 4

static nmo_image_codec_t g_codecs[NMO_BITMAP_FORMAT_COUNT];
static uint8_t g_codec_registered[NMO_BITMAP_FORMAT_COUNT];
static int g_codec_defaults_registered = 0;

static inline void nmo_image_codec_ensure_defaults(void);

static nmo_result_t nmo_image_codec_encode_stb(nmo_bitmap_format_t format,
                                               const uint8_t *pixels,
                                               int width,
                                               int height,
                                               int channels,
                                               const nmo_bitmap_properties_t *props,
                                               nmo_arena_t *arena,
                                               uint8_t **out_data,
                                               size_t *out_size) {
    if (!pixels || width <= 0 || height <= 0 || channels <= 0 || channels > 4 ||
        !arena || !out_data || !out_size) {
        return nmo_result_error(NMO_ERROR(NULL,
                                          NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments for image encode"));
    }

    int quality = props ? props->quality : 0;
    size_t encoded_size = 0;
    uint8_t *buffer = nmo_stbi_write_to_memory(arena,
                                               format,
                                               width,
                                               height,
                                               channels,
                                               pixels,
                                               quality,
                                               &encoded_size);
    if (!buffer || encoded_size == 0) {
        return nmo_result_error(NMO_ERROR(NULL,
                                          NMO_ERR_INVALID_FORMAT,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to encode bitmap"));
    }

    *out_data = buffer;
    *out_size = encoded_size;
    return nmo_result_ok();
}

static nmo_result_t nmo_image_codec_decode_stb(const uint8_t *encoded_data,
                                               size_t encoded_size,
                                               int desired_channels,
                                               nmo_arena_t *arena,
                                               int *out_width,
                                               int *out_height,
                                               uint8_t **out_pixels,
                                               int *out_channels) {
    if (!encoded_data || encoded_size == 0 || !arena || !out_width ||
        !out_height || !out_pixels || !out_channels) {
        return nmo_result_error(NMO_ERROR(NULL,
                                          NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments for image decode"));
    }

    if (encoded_size > (size_t)INT_MAX) {
        return nmo_result_error(NMO_ERROR(NULL,
                                          NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Encoded bitmap too large"));
    }

    int width = 0;
    int height = 0;
    int found_channels = 0;
    uint8_t *pixels = nmo_stbi_load_from_memory(arena,
                                                encoded_data,
                                                (int)encoded_size,
                                                &width,
                                                &height,
                                                &found_channels,
                                                desired_channels);
    if (!pixels) {
        return nmo_result_error(NMO_ERROR(NULL,
                                          NMO_ERR_INVALID_FORMAT,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to decode bitmap"));
    }

    *out_width = width;
    *out_height = height;
    *out_pixels = pixels;
    *out_channels = desired_channels ? desired_channels : found_channels;
    return nmo_result_ok();
}

static nmo_result_t nmo_image_codec_encode_png(const uint8_t *pixels,
                                               int width,
                                               int height,
                                               int channels,
                                               const nmo_bitmap_properties_t *props,
                                               nmo_arena_t *arena,
                                               uint8_t **out_data,
                                               size_t *out_size) {
    return nmo_image_codec_encode_stb(NMO_BITMAP_FORMAT_PNG,
                                      pixels,
                                      width,
                                      height,
                                      channels,
                                      props,
                                      arena,
                                      out_data,
                                      out_size);
}

static nmo_result_t nmo_image_codec_encode_bmp(const uint8_t *pixels,
                                               int width,
                                               int height,
                                               int channels,
                                               const nmo_bitmap_properties_t *props,
                                               nmo_arena_t *arena,
                                               uint8_t **out_data,
                                               size_t *out_size) {
    return nmo_image_codec_encode_stb(NMO_BITMAP_FORMAT_BMP,
                                      pixels,
                                      width,
                                      height,
                                      channels,
                                      props,
                                      arena,
                                      out_data,
                                      out_size);
}

static nmo_result_t nmo_image_codec_encode_tga(const uint8_t *pixels,
                                               int width,
                                               int height,
                                               int channels,
                                               const nmo_bitmap_properties_t *props,
                                               nmo_arena_t *arena,
                                               uint8_t **out_data,
                                               size_t *out_size) {
    return nmo_image_codec_encode_stb(NMO_BITMAP_FORMAT_TGA,
                                      pixels,
                                      width,
                                      height,
                                      channels,
                                      props,
                                      arena,
                                      out_data,
                                      out_size);
}

static nmo_result_t nmo_image_codec_encode_jpg(const uint8_t *pixels,
                                               int width,
                                               int height,
                                               int channels,
                                               const nmo_bitmap_properties_t *props,
                                               nmo_arena_t *arena,
                                               uint8_t **out_data,
                                               size_t *out_size) {
    return nmo_image_codec_encode_stb(NMO_BITMAP_FORMAT_JPG,
                                      pixels,
                                      width,
                                      height,
                                      channels,
                                      props,
                                      arena,
                                      out_data,
                                      out_size);
}

static nmo_result_t nmo_image_codec_decode_common(const uint8_t *encoded_data,
                                                  size_t encoded_size,
                                                  int desired_channels,
                                                  nmo_arena_t *arena,
                                                  int *out_width,
                                                  int *out_height,
                                                  uint8_t **out_pixels,
                                                  int *out_channels) {
    return nmo_image_codec_decode_stb(encoded_data,
                                      encoded_size,
                                      desired_channels,
                                      arena,
                                      out_width,
                                      out_height,
                                      out_pixels,
                                      out_channels);
}

static void nmo_image_codec_register_builtin_entry(nmo_image_codec_t codec) {
    if (codec.format <= NMO_BITMAP_FORMAT_RAW ||
        codec.format >= NMO_BITMAP_FORMAT_COUNT) {
        return;
    }

    g_codecs[codec.format] = codec;
    g_codec_registered[codec.format] = 1;
}

static inline const char *nmo_image_codec_extension_or_default(const char *const *extensions) {
    if (!extensions) {
        return NULL;
    }

    for (size_t i = 0; i < NMO_MAX_CODEC_EXTENSIONS; ++i) {
        if (extensions[i]) {
            return extensions[i];
        }
    }
    return NULL;
}

static void nmo_image_codec_register_builtin(void) {
    if (g_codec_defaults_registered) {
        return;
    }

    g_codec_defaults_registered = 1;

    const char *png_ext[] = { "PNG", "png", NULL, NULL };
    const char *bmp_ext[] = { "BMP", "bmp", NULL, NULL };
    const char *tga_ext[] = { "TGA", "tga", NULL, NULL };
    const char *jpg_ext[] = { "JPG", "JPEG", "jpg", NULL };

    nmo_image_codec_register_builtin_entry((nmo_image_codec_t){
        .format = NMO_BITMAP_FORMAT_PNG,
        .name = "PNG",
        .extensions = { png_ext[0], png_ext[1], png_ext[2], png_ext[3] },
        .supports_alpha = true,
        .encode = nmo_image_codec_encode_png,
        .decode = nmo_image_codec_decode_common,
    });

    nmo_image_codec_register_builtin_entry((nmo_image_codec_t){
        .format = NMO_BITMAP_FORMAT_BMP,
        .name = "BMP",
        .extensions = { bmp_ext[0], bmp_ext[1], bmp_ext[2], bmp_ext[3] },
        .supports_alpha = false,
        .encode = nmo_image_codec_encode_bmp,
        .decode = nmo_image_codec_decode_common,
    });

    nmo_image_codec_register_builtin_entry((nmo_image_codec_t){
        .format = NMO_BITMAP_FORMAT_TGA,
        .name = "TGA",
        .extensions = { tga_ext[0], tga_ext[1], tga_ext[2], tga_ext[3] },
        .supports_alpha = true,
        .encode = nmo_image_codec_encode_tga,
        .decode = nmo_image_codec_decode_common,
    });

    nmo_image_codec_register_builtin_entry((nmo_image_codec_t){
        .format = NMO_BITMAP_FORMAT_JPG,
        .name = "JPG",
        .extensions = { jpg_ext[0], jpg_ext[1], jpg_ext[2], jpg_ext[3] },
        .supports_alpha = false,
        .encode = nmo_image_codec_encode_jpg,
        .decode = nmo_image_codec_decode_common,
    });
}

static inline void nmo_image_codec_ensure_defaults(void) {
    if (!g_codec_defaults_registered) {
        nmo_image_codec_register_defaults();
    }
}

NMO_API nmo_result_t nmo_image_codec_register(const nmo_image_codec_t *codec) {
    if (!codec) {
        return nmo_result_error(NMO_ERROR(NULL,
                                          NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Codec pointer is NULL"));
    }

    if (codec->format <= NMO_BITMAP_FORMAT_RAW ||
        codec->format >= NMO_BITMAP_FORMAT_COUNT ||
        !codec->encode || !codec->decode) {
        return nmo_result_error(NMO_ERROR(NULL,
                                          NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid codec description"));
    }

    g_codecs[codec->format] = *codec;
    g_codec_registered[codec->format] = 1;
    return nmo_result_ok();
}

NMO_API const nmo_image_codec_t *nmo_image_codec_get(nmo_bitmap_format_t format) {
    if (format <= NMO_BITMAP_FORMAT_RAW || format >= NMO_BITMAP_FORMAT_COUNT) {
        return NULL;
    }
    nmo_image_codec_ensure_defaults();
    if (!g_codec_registered[format]) {
        return NULL;
    }
    return &g_codecs[format];
}

static int nmo_image_codec_extension_equals(const char *lhs, const char *rhs) {
    if (!lhs || !rhs) {
        return 0;
    }

    while (*lhs && *rhs) {
        if (tolower((unsigned char)*lhs) != tolower((unsigned char)*rhs)) {
            return 0;
        }
        ++lhs;
        ++rhs;
    }

    return *lhs == '\0' && *rhs == '\0';
}

NMO_API const nmo_image_codec_t *nmo_image_codec_find_by_extension(const char *extension) {
    if (!extension) {
        return NULL;
    }

    nmo_image_codec_ensure_defaults();

    for (size_t i = 0; i < NMO_BITMAP_FORMAT_COUNT; ++i) {
        if (!g_codec_registered[i]) {
            continue;
        }

        const nmo_image_codec_t *codec = &g_codecs[i];
        for (size_t e = 0; e < NMO_MAX_CODEC_EXTENSIONS; ++e) {
            const char *ext = codec->extensions[e];
            if (ext && nmo_image_codec_extension_equals(ext, extension)) {
                return codec;
            }
        }
    }

    return NULL;
}

NMO_API void nmo_image_codec_register_defaults(void) {
    nmo_image_codec_register_builtin();
}
