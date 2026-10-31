/**
 * @file movie.c
 * @brief The frames of a texture's AVI movie (CKMovieReader's AVI reader).
 *
 * The reader decodes Microsoft Video 1 (CRAM), 16-bit, the codec of the
 * game's movies; frames decode in order, each over the one before it.
 */

#include "../engine_internal.h"

#include <stdlib.h>
#include <string.h>

struct eng_movie {
    unsigned char *file;
    size_t size;
    int32_t width, height;
    /* The frames' chunks in the movi list: offset and size of each */
    ENG_VEC(size_t) offsets;
    ENG_VEC(size_t) sizes;
    uint16_t *pixels; /* the last decoded frame, top row first, RGB555 */
    uint8_t *rgba;
    int32_t decoded;  /* that frame's index; -1 for none */
};

static uint32_t movie_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The chunks of [begin, end): the video frames ("##dc", "##db") in order, into lists too */
static bool movie_scan(eng_movie_t *movie, size_t begin, size_t end)
{
    size_t at = begin;
    while (at + 8 <= end) {
        const unsigned char *chunk = movie->file + at;
        size_t size = movie_u32(chunk + 4);
        size_t data = at + 8;
        if (size > end - data) {
            break;
        }
        if (memcmp(chunk, "LIST", 4) == 0 && size >= 4) {
            if (!movie_scan(movie, data + 4, data + size)) {
                return false;
            }
        } else if (chunk[2] == 'd' && (chunk[3] == 'c' || chunk[3] == 'b')) {
            size_t *offset = NULL, *length = NULL;
            if (!ENG_VEC_PUSH(movie->offsets, offset) || !ENG_VEC_PUSH(movie->sizes, length)) {
                return false;
            }
            *offset = data;
            *length = size;
        }
        at = data + size + (size & 1u);
    }
    return true;
}

/* The video stream's format (strf's BITMAPINFOHEADER): MS Video 1 at 16 bits only */
static bool movie_format(eng_movie_t *movie)
{
    for (size_t at = 12; at + 48 <= movie->size; at++) {
        const unsigned char *p = movie->file + at;
        if (memcmp(p, "strh", 4) == 0 && memcmp(p + 8, "vids", 4) == 0) {
            const unsigned char *strf = NULL;
            for (size_t k = at + 8; k + 48 <= movie->size && strf == NULL; k++) {
                if (memcmp(movie->file + k, "strf", 4) == 0) {
                    strf = movie->file + k + 8;
                }
            }
            if (strf == NULL) {
                return false;
            }
            int32_t width = (int32_t)movie_u32(strf + 4), height = (int32_t)movie_u32(strf + 8);
            uint16_t bits = (uint16_t)(strf[14] | strf[15] << 8);
            bool cram = memcmp(strf + 16, "CRAM", 4) == 0 || memcmp(strf + 16, "MSVC", 4) == 0 ||
                        memcmp(strf + 16, "msvc", 4) == 0 || memcmp(strf + 16, "cram", 4) == 0;
            if (!cram || bits != 16 || width <= 0 || width % 4 != 0 || height == 0) {
                return false;
            }
            movie->width = width;
            movie->height = height < 0 ? -height : height;
            return movie->height % 4 == 0;
        }
    }
    return false;
}

eng_movie_t *eng_movie_open(nmo_engine_t *engine, const char *name)
{
    eng_movie_t *movie = (eng_movie_t *)calloc(1, sizeof(*movie));
    if (movie == NULL) {
        return NULL;
    }
    movie->decoded = -1;
    movie->file = eng_read_path_file(engine, name, 0, &movie->size);
    bool ok = movie->file != NULL && movie->size >= 12 && memcmp(movie->file, "RIFF", 4) == 0 &&
              memcmp(movie->file + 8, "AVI ", 4) == 0 && movie_format(movie) &&
              movie_scan(movie, 12, movie->size) && movie->offsets.count > 0;
    size_t count = ok ? (size_t)movie->width * (size_t)movie->height : 0;
    movie->pixels = ok ? (uint16_t *)calloc(count, sizeof(uint16_t)) : NULL;
    movie->rgba = ok ? (uint8_t *)malloc(count * 4u) : NULL;
    if (movie->pixels == NULL || movie->rgba == NULL) {
        eng_movie_close(movie);
        return NULL;
    }
    return movie;
}

void eng_movie_close(eng_movie_t *movie)
{
    if (movie == NULL) {
        return;
    }
    free(movie->file);
    free(movie->offsets.data);
    free(movie->sizes.data);
    free(movie->pixels);
    free(movie->rgba);
    free(movie);
}

/* One MS Video 1 frame over the last: 4x4 blocks, bottom row of blocks first */
static void movie_decode(eng_movie_t *movie, const unsigned char *data, size_t size)
{
    const int32_t stride = movie->width;
    int32_t wide = movie->width / 4, high = movie->height / 4;
    int32_t total = wide * high, skip = 0;
    size_t at = 0;
    for (int32_t by = high; by > 0; by--) {
        int32_t block = (by * 4 - 1) * stride;
        for (int32_t bx = wide; bx > 0; bx--, block += 4) {
            if (skip > 0) {
                skip--;
                total--;
                continue;
            }
            if (at + 2 > size) {
                return;
            }
            uint8_t a = data[at], b = data[at + 1];
            at += 2;
            if (a == 0 && b == 0 && total == 0) {
                return;
            }
            uint16_t colors[8];
            if ((b & 0xFC) == 0x84) {
                skip = ((b - 0x84) << 8) + a - 1;
            } else if (b < 0x80) {
                /* Two colors by the flags, or eight: two per 2x2 quadrant */
                uint32_t flags = (uint32_t)b << 8 | a;
                int count = 2;
                if (at + 4 <= size && (data[at + 1] & 0x80) != 0) {
                    count = 8;
                }
                if (at + (size_t)count * 2u > size) {
                    return;
                }
                for (int c = 0; c < count; c++, at += 2) {
                    colors[c] = (uint16_t)(data[at] | data[at + 1] << 8);
                }
                int32_t pixel = block;
                for (int y = 0; y < 4; y++, pixel -= stride + 4) {
                    for (int x = 0; x < 4; x++, flags >>= 1) {
                        int pick = (int)((flags & 1u) ^ 1u);
                        movie->pixels[pixel++] =
                            count == 8 ? colors[((y & 2) << 1) + (x & 2) + pick] : colors[pick];
                    }
                }
            } else {
                uint16_t color = (uint16_t)(b << 8 | a);
                int32_t pixel = block;
                for (int y = 0; y < 4; y++, pixel -= stride + 4) {
                    for (int x = 0; x < 4; x++) {
                        movie->pixels[pixel++] = color;
                    }
                }
            }
            total--;
        }
    }
}

bool eng_movie_frame(eng_movie_t *movie, int32_t frame, const uint8_t **out_rgba,
                     int32_t *out_width, int32_t *out_height)
{
    int32_t count = (int32_t)movie->offsets.count;
    frame = frame < 0 ? 0 : frame >= count ? count - 1 : frame;
    if (frame < movie->decoded) {
        movie->decoded = -1; /* from the start again: each frame draws over the one before */
        memset(movie->pixels, 0, (size_t)movie->width * (size_t)movie->height * sizeof(uint16_t));
    }
    while (movie->decoded < frame) {
        movie->decoded++;
        movie_decode(movie, movie->file + movie->offsets.data[movie->decoded],
                     movie->sizes.data[movie->decoded]);
    }
    size_t pixels = (size_t)movie->width * (size_t)movie->height;
    for (size_t i = 0; i < pixels; i++) {
        uint16_t c = movie->pixels[i];
        uint8_t r = (uint8_t)((c >> 10) & 31u), g = (uint8_t)((c >> 5) & 31u), b = (uint8_t)(c & 31u);
        movie->rgba[i * 4] = (uint8_t)(r << 3 | r >> 2);
        movie->rgba[i * 4 + 1] = (uint8_t)(g << 3 | g >> 2);
        movie->rgba[i * 4 + 2] = (uint8_t)(b << 3 | b >> 2);
        movie->rgba[i * 4 + 3] = 0xFF;
    }
    *out_rgba = movie->rgba;
    *out_width = movie->width;
    *out_height = movie->height;
    return true;
}
