#include "core/nmo_hex.h"

#include <stdlib.h>

void nmo_hex_write_byte(char out[2], uint8_t value, bool uppercase) {
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    out[0] = digits[(value >> 4) & 0x0F];
    out[1] = digits[value & 0x0F];
}

char *nmo_hex_bytes_to_string(const void *bytes, size_t len, bool uppercase) {
    const uint8_t *b = (const uint8_t *)bytes;
    if (!b && len != 0) {
        return NULL;
    }

    /* Overflow protection: len * 2 + 1 must not wrap */
    if (len > (SIZE_MAX - 1) / 2) {
        return NULL;
    }

    char *hex = (char *)malloc(len * 2 + 1);
    if (!hex) {
        return NULL;
    }

    for (size_t i = 0; i < len; ++i) {
        nmo_hex_write_byte(&hex[i * 2], b[i], uppercase);
    }
    hex[len * 2] = '\0';
    return hex;
}
