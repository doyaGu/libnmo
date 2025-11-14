#ifndef NMO_IMAGE_CODEC_H
#define NMO_IMAGE_CODEC_H

#include <stdbool.h>
#include <stddef.h>

#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "format/nmo_image.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef nmo_result_t (*nmo_image_codec_encode_fn)(const uint8_t *pixels,
                                                  int width,
                                                  int height,
                                                  int channels,
                                                  const nmo_bitmap_properties_t *props,
                                                  nmo_arena_t *arena,
                                                  uint8_t **out_data,
                                                  size_t *out_size);

typedef nmo_result_t (*nmo_image_codec_decode_fn)(const uint8_t *encoded_data,
                                                  size_t encoded_size,
                                                  int desired_channels,
                                                  nmo_arena_t *arena,
                                                  int *out_width,
                                                  int *out_height,
                                                  uint8_t **out_pixels,
                                                  int *out_channels);

typedef struct nmo_image_codec {
    nmo_bitmap_format_t format;
    const char *name;
    const char *extensions[4];
    bool supports_alpha;
    nmo_image_codec_encode_fn encode;
    nmo_image_codec_decode_fn decode;
} nmo_image_codec_t;

NMO_API nmo_result_t nmo_image_codec_register(const nmo_image_codec_t *codec);
NMO_API const nmo_image_codec_t *nmo_image_codec_get(nmo_bitmap_format_t format);
NMO_API const nmo_image_codec_t *nmo_image_codec_find_by_extension(const char *extension);
NMO_API void nmo_image_codec_register_defaults(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_IMAGE_CODEC_H */
