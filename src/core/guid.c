#include "core/nmo_guid.h"
#include <stdio.h>
#include <string.h>

int nmo_guid_equals(nmo_guid_t a, nmo_guid_t b) {
    return a.d1 == b.d1 && a.d2 == b.d2;
}

int nmo_guid_is_null(nmo_guid_t guid) {
    return guid.d1 == 0 && guid.d2 == 0;
}

uint32_t nmo_guid_hash(nmo_guid_t guid) {
    // Simple hash combining both values
    return guid.d1 ^ guid.d2;
}

static int hex_char_to_int(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int parse_hex_uint32(const char *str, uint32_t *out) {
    uint32_t value = 0;
    int count = 0;

    while (*str && count < 8) {
        int digit = hex_char_to_int(*str);
        if (digit < 0) {
            break;
        }

        value = (value << 4) | digit;
        str++;
        count++;
    }

    if (count != 8) {
        return 0; // Must be exactly 8 hex digits
    }

    *out = value;
    return 1;
}

nmo_guid_t nmo_guid_parse(const char *str) {
    if (str == NULL || str[0] == '\0') {
        return NMO_GUID_NULL;
    }

    // Require opening brace
    if (*str != '{') {
        return NMO_GUID_NULL;
    }
    str++;

    // Parse first 32 bits
    uint32_t d1, d2;
    if (!parse_hex_uint32(str, &d1)) {
        return NMO_GUID_NULL;
    }
    str += 8;

    // Require dash
    if (*str != '-') {
        return NMO_GUID_NULL;
    }
    str++;

    // Parse second 32 bits
    if (!parse_hex_uint32(str, &d2)) {
        return NMO_GUID_NULL;
    }
    str += 8;

    // Require closing brace
    if (*str != '}') {
        return NMO_GUID_NULL;
    }
    str++;

    // Should be at end of string
    if (*str != '\0') {
        return NMO_GUID_NULL;
    }

    nmo_guid_t guid = {d1, d2};
    return guid;
}

int nmo_guid_format(nmo_guid_t guid, char *buffer, size_t size) {
    if (buffer == NULL || size < 21) {
        return -1;
    }

    return snprintf(buffer, size, "{%08X-%08X}", guid.d1, guid.d2);
}
