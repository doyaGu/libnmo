#include "core/nmo_utils.h"

#include "core/nmo_hex.h"

#include <stdlib.h>
#include <string.h>

char *nmo_text_strdup_or_empty(const char *value) {
    const char *src = value ? value : "";
    size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    if (len > 0) {
        memcpy(copy, src, len);
    }
    copy[len] = '\0';
    return copy;
}

char *nmo_text_escape_bytes(const char *value) {
    if (!value) {
        return nmo_text_strdup_or_empty("");
    }

    size_t len = strlen(value);
    size_t max_len = len * 4 + 1;
    char *out = (char *)malloc(max_len);
    if (!out) {
        return nmo_text_strdup_or_empty("");
    }

    size_t pos = 0;
    const unsigned char *p = (const unsigned char *)value;
    for (; *p; ++p) {
        unsigned char c = *p;
        if (c >= 0x20 && c <= 0x7E) {
            out[pos++] = (char)c;
        } else {
            if (pos + 4 >= max_len) {
                break;
            }
            out[pos++] = '\\';
            out[pos++] = 'x';
            char hex[2];
            nmo_hex_write_byte(hex, (uint8_t)c, true);
            out[pos++] = hex[0];
            out[pos++] = hex[1];
        }
    }
    out[pos] = '\0';
    return out;
}
