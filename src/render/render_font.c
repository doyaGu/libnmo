/**
 * @file render_font.c
 * @brief The TrueType file of a font face from the system's font folders.
 *
 * The one place the renderer looks at the host's file system: a host with
 * its fonts elsewhere (or none) gives the renderer a loader of its own.
 */

#include "render_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../platform/platform.h"

/* dir/name: exact size */
static char *font_path(const char *dir, const char *name)
{
    size_t a = strlen(dir), b = strlen(name);
    char *path = (char *)malloc(a + 1 + b + 1);
    if (path != NULL) {
        memcpy(path, dir, a);
        path[a] = '/';
        memcpy(path + a + 1, name, b + 1);
    }
    return path;
}

static unsigned char *font_read_file(const char *path)
{
    FILE *file = nmo_platform_fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    unsigned char *data = NULL;
    if (fseek(file, 0, SEEK_END) == 0) {
        long size = ftell(file);
        data = size > 0 ? (unsigned char *)malloc((size_t)size) : NULL;
        if (data != NULL && (fseek(file, 0, SEEK_SET) != 0 ||
                             fread(data, 1, (size_t)size, file) != (size_t)size)) {
            free(data);
            data = NULL;
        }
    }
    fclose(file);
    return data;
}

/* The file of `face` in this style: "Face Bold Italic.ttf" and the like, then "Face.ttf" */
unsigned char *render_font_system(void *user_data, const char *face, bool bold, bool italic)
{
    (void)user_data;
    static const char *const dirs[] = {
        "/System/Library/Fonts/Supplemental", "/System/Library/Fonts", "/Library/Fonts",
        "C:/Windows/Fonts", "/usr/share/fonts/truetype/msttcorefonts", "/usr/share/fonts/TTF",
    };
    const char *style = bold && italic ? " Bold Italic" : bold ? " Bold" : italic ? " Italic" : "";
    size_t face_length = strlen(face), style_length = strlen(style);
    for (int plain = 0; plain < 2; plain++) {
        const char *suffix = plain ? "" : style;
        size_t suffix_length = plain ? 0 : style_length;
        char *name = (char *)malloc(face_length + suffix_length + 5);
        if (name == NULL) {
            return NULL;
        }
        memcpy(name, face, face_length);
        memcpy(name + face_length, suffix, suffix_length);
        memcpy(name + face_length + suffix_length, ".ttf", 5);
        for (size_t d = 0; d < sizeof(dirs) / sizeof(dirs[0]); d++) {
            char *path = font_path(dirs[d], name);
            unsigned char *data = path != NULL ? font_read_file(path) : NULL;
            free(path);
            if (data != NULL) {
                free(name);
                return data;
            }
        }
        free(name);
    }
    return NULL;
}
