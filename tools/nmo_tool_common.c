#include "nmo_tool_common.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int nmo_tool_stricmp(const char *a, const char *b) {
    if (a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    while (*a && *b) {
        int da = tolower((unsigned char)*a);
        int db = tolower((unsigned char)*b);
        if (da != db) {
            return da - db;
        }
        ++a;
        ++b;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

bool nmo_tool_streq_ci(const char *a, const char *b) {
    return nmo_tool_stricmp(a, b) == 0;
}

bool nmo_tool_match_wildcard_ci(const char *pattern, const char *value) {
    if (!pattern || !*pattern) {
        return true;
    }
    if (!value) {
        value = "";
    }

    char pc = *pattern;
    if (pc == '*') {
        pattern++;
        if (!*pattern) {
            return true;
        }
        while (*value) {
            if (nmo_tool_match_wildcard_ci(pattern, value)) {
                return true;
            }
            value++;
        }
        return nmo_tool_match_wildcard_ci(pattern, value);
    }
    if (pc == '?') {
        if (!*value) {
            return false;
        }
        return nmo_tool_match_wildcard_ci(pattern + 1, value + 1);
    }
    if (tolower((unsigned char)pc) != tolower((unsigned char)*value)) {
        return false;
    }
    return nmo_tool_match_wildcard_ci(pattern + 1, value + 1);
}

char *nmo_tool_strdup(const char *src) {
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len + 1);
    return copy;
}

bool nmo_tool_parse_u32_dec(const char *text, uint32_t *out) {
    if (!text || !out) {
        return false;
    }
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > 0xFFFFFFFFu) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

bool nmo_tool_parse_u32(const char *text, uint32_t *out) {
    if (!text || !out) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || value > 0xFFFFFFFFu) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

bool nmo_tool_parse_size_dec(const char *text, size_t *out) {
    if (!text || !out) {
        return false;
    }
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    *out = (size_t)value;
    return true;
}

bool nmo_tool_parse_size(const char *text, size_t *out) {
    if (!text || !out) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    if (value > (unsigned long long)SIZE_MAX) {
        return false;
    }
    *out = (size_t)value;
    return true;
}
