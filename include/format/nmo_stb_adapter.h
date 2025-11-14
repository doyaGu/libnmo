/**
 * @file nmo_stb_adapter.h
 * @brief stb_image helpers integrated with libnmo arenas.
 */

#ifndef NMO_STB_ADAPTER_H
#define NMO_STB_ADAPTER_H

#include "core/nmo_arena.h"
#include "format/nmo_image.h"

#ifdef __cplusplus
extern "C" {
#endif

NMO_API void nmo_stb_set_arena(nmo_arena_t *arena);
NMO_API nmo_arena_t *nmo_stb_get_arena(void);

NMO_API uint8_t *nmo_stbi_load_from_memory(
    nmo_arena_t *arena,
    const uint8_t *buffer,
    int len,
    int *out_width,
    int *out_height,
    int *out_channels,
    int desired_channels);

NMO_API uint8_t *nmo_stbi_write_to_memory(
    nmo_arena_t *arena,
    nmo_bitmap_format_t format,
    int width,
    int height,
    int channels,
    const uint8_t *data,
    int quality,
    size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_STB_ADAPTER_H */
