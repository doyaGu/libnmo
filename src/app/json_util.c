#include "app/nmo_json_util.h"
#include "core/nmo_hex.h"
#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t utf8_char_len(const unsigned char *s, size_t remaining) {
    if (!s || remaining == 0) {
        return 0;
    }

    unsigned char c = s[0];
    if (c < 0x80) {
        return 1;
    }

    if ((c & 0xE0) == 0xC0) {
        if (remaining < 2) {
            return 0;
        }
        if ((s[1] & 0xC0) != 0x80) {
            return 0;
        }
        if (c < 0xC2) {
            return 0;
        }
        return 2;
    }

    if ((c & 0xF0) == 0xE0) {
        if (remaining < 3) {
            return 0;
        }
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) {
            return 0;
        }
        if (c == 0xE0 && s[1] < 0xA0) {
            return 0;
        }
        if (c == 0xED && s[1] >= 0xA0) {
            return 0;
        }
        return 3;
    }

    if ((c & 0xF8) == 0xF0) {
        if (remaining < 4) {
            return 0;
        }
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) {
            return 0;
        }
        if (c == 0xF0 && s[1] < 0x90) {
            return 0;
        }
        if (c == 0xF4 && s[1] > 0x8F) {
            return 0;
        }
        if (c > 0xF4) {
            return 0;
        }
        return 4;
    }

    return 0;
}

static char *bytes_to_utf8_latin1(const unsigned char *bytes, size_t len) {
    if (!bytes) {
        return NULL;
    }

    size_t out_len = 0;
    for (size_t i = 0; i < len; ++i) {
        out_len += (bytes[i] < 0x80) ? 1 : 2;
    }

    char *out = (char *)malloc(out_len + 1);
    if (!out) {
        return NULL;
    }

    size_t pos = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = bytes[i];
        if (c < 0x80) {
            out[pos++] = (char)c;
        } else if (c < 0xC0) {
            out[pos++] = (char)0xC2;
            out[pos++] = (char)c;
        } else {
            out[pos++] = (char)0xC3;
            out[pos++] = (char)(c - 0x40);
        }
    }
    out[pos] = '\0';
    return out;
}

yyjson_mut_val *nmo_json_make_str_safe(yyjson_mut_doc *doc, const char *str) {
    if (!doc) {
        return NULL;
    }
    if (!str) {
        return yyjson_mut_null(doc);
    }

    const unsigned char *s = (const unsigned char *)str;
    size_t slen = strlen(str);
    bool valid = true;

    for (size_t i = 0; i < slen;) {
        unsigned char c = s[i];
        if (c < 0x80) {
            i++;
            continue;
        }
        if ((c & 0xE0) == 0xC0 && c >= 0xC2 && i + 1 < slen && (s[i + 1] & 0xC0) == 0x80) {
            i += 2;
            continue;
        }
        if ((c & 0xF0) == 0xE0 && i + 2 < slen &&
            (s[i + 1] & 0xC0) == 0x80 && (s[i + 2] & 0xC0) == 0x80) {
            if (c == 0xE0 && s[i + 1] < 0xA0) {
                valid = false;
                break;
            }
            if (c == 0xED && s[i + 1] >= 0xA0) {
                valid = false;
                break;
            }
            i += 3;
            continue;
        }
        if ((c & 0xF8) == 0xF0 && i + 3 < slen &&
            (s[i + 1] & 0xC0) == 0x80 && (s[i + 2] & 0xC0) == 0x80 && (s[i + 3] & 0xC0) == 0x80) {
            if (c == 0xF0 && s[i + 1] < 0x90) {
                valid = false;
                break;
            }
            if (c > 0xF4 || (c == 0xF4 && s[i + 1] > 0x8F)) {
                valid = false;
                break;
            }
            i += 4;
            continue;
        }
        valid = false;
        break;
    }

    if (valid) {
        return yyjson_mut_strcpy(doc, str);
    }

    const size_t cap = (slen * 3u) + 1u;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        return yyjson_mut_null(doc);
    }

    size_t out_len = 0;
    for (size_t i = 0; i < slen;) {
        unsigned char c = s[i];

        if (c < 0x80) {
            if (c == '\t' || c == '\n' || c == '\r' || c >= 0x20) {
                buf[out_len++] = (char)c;
            } else {
                buf[out_len++] = (char)0xEF;
                buf[out_len++] = (char)0xBF;
                buf[out_len++] = (char)0xBD;
            }
            i++;
            continue;
        }

        if ((c & 0xE0) == 0xC0 && c >= 0xC2 && i + 1 < slen && (s[i + 1] & 0xC0) == 0x80) {
            buf[out_len++] = (char)s[i++];
            buf[out_len++] = (char)s[i++];
            continue;
        }

        if ((c & 0xF0) == 0xE0 && i + 2 < slen &&
            (s[i + 1] & 0xC0) == 0x80 && (s[i + 2] & 0xC0) == 0x80) {
            if (c == 0xE0 && s[i + 1] < 0xA0) {
                /* Overlong */
            } else if (c == 0xED && s[i + 1] >= 0xA0) {
                /* Surrogate */
            } else {
                buf[out_len++] = (char)s[i++];
                buf[out_len++] = (char)s[i++];
                buf[out_len++] = (char)s[i++];
                continue;
            }
        }

        if ((c & 0xF8) == 0xF0 && i + 3 < slen &&
            (s[i + 1] & 0xC0) == 0x80 && (s[i + 2] & 0xC0) == 0x80 && (s[i + 3] & 0xC0) == 0x80) {
            if (c == 0xF0 && s[i + 1] < 0x90) {
                /* Overlong */
            } else if (c > 0xF4 || (c == 0xF4 && s[i + 1] > 0x8F)) {
                /* Out of range */
            } else {
                buf[out_len++] = (char)s[i++];
                buf[out_len++] = (char)s[i++];
                buf[out_len++] = (char)s[i++];
                buf[out_len++] = (char)s[i++];
                continue;
            }
        }

        buf[out_len++] = (char)0xEF;
        buf[out_len++] = (char)0xBF;
        buf[out_len++] = (char)0xBD;
        i++;
    }

    buf[out_len] = '\0';
    yyjson_mut_val *val = yyjson_mut_strcpy(doc, buf);
    free(buf);
    return val ? val : yyjson_mut_null(doc);
}

bool nmo_json_add_str_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                           const char *key, const char *str) {
    if (!doc || !obj || !key) {
        return false;
    }

    yyjson_mut_val *key_val = yyjson_mut_strcpy(doc, key);
    if (!key_val) {
        return false;
    }

    if (!str) {
        yyjson_mut_val *null_val = yyjson_mut_null(doc);
        return yyjson_mut_obj_add(obj, key_val, null_val);
    }

    const unsigned char *s = (const unsigned char *)str;
    size_t slen = strlen(str);
    bool needs_sanitize = false;
    size_t i = 0;

    while (i < slen) {
        size_t char_len = utf8_char_len(s + i, slen - i);
        if (char_len == 0) {
            needs_sanitize = true;
            break;
        }
        i += char_len;
    }

    yyjson_mut_val *str_val = NULL;
    if (!needs_sanitize) {
        str_val = yyjson_mut_strcpy(doc, str);
    } else {
        char *mapped = bytes_to_utf8_latin1(s, slen);
        if (!mapped) {
            return false;
        }
        str_val = yyjson_mut_strcpy(doc, mapped);
        free(mapped);
    }

    if (!str_val) {
        return false;
    }

    bool ok = yyjson_mut_obj_add(obj, key_val, str_val);

    if (needs_sanitize) {
        char hex_key[256];
        char len_key[256];
        if (snprintf(hex_key, sizeof(hex_key), "%s_raw_hex", key) > 0 &&
            snprintf(len_key, sizeof(len_key), "%s_raw_len", key) > 0) {
            char *hex = nmo_hex_bytes_to_string(s, slen, true);
            if (hex) {
                yyjson_mut_obj_add_strcpy(doc, obj, hex_key, hex);
                free(hex);
            }
            yyjson_mut_obj_add_uint(doc, obj, len_key, (uint64_t)slen);
        }
    }

    return ok;
}

bool nmo_json_add_str_safe_to_arr(yyjson_mut_doc *doc, yyjson_mut_val *arr,
                                  const char *str) {
    if (!doc || !arr) {
        return false;
    }
    if (!str) {
        yyjson_mut_arr_add_null(doc, arr);
        return true;
    }

    const unsigned char *s = (const unsigned char *)str;
    size_t slen = strlen(str);
    bool needs_sanitize = false;
    size_t i = 0;

    while (i < slen) {
        size_t char_len = utf8_char_len(s + i, slen - i);
        if (char_len == 0) {
            needs_sanitize = true;
            break;
        }
        i += char_len;
    }

    if (!needs_sanitize) {
        yyjson_mut_arr_add_str(doc, arr, str);
        return true;
    }

    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    char *mapped = bytes_to_utf8_latin1(s, slen);
    if (!mapped) {
        return false;
    }
    yyjson_mut_obj_add_strcpy(doc, obj, "text", mapped);
    free(mapped);

    char *hex = nmo_hex_bytes_to_string(s, slen, true);
    if (hex) {
        yyjson_mut_obj_add_strcpy(doc, obj, "raw_hex", hex);
        free(hex);
    }
    yyjson_mut_obj_add_uint(doc, obj, "raw_len", (uint64_t)slen);
    yyjson_mut_arr_add_val(arr, obj);
    return true;
}

bool nmo_json_add_int_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                           const char *key, int64_t val) {
    if (!doc || !obj || !key) {
        return false;
    }
    yyjson_mut_val *key_val = yyjson_mut_strcpy(doc, key);
    yyjson_mut_val *val_val = yyjson_mut_sint(doc, val);
    if (!key_val || !val_val) {
        return false;
    }
    return yyjson_mut_obj_add(obj, key_val, val_val);
}

bool nmo_json_add_uint_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                            const char *key, uint64_t val) {
    if (!doc || !obj || !key) {
        return false;
    }
    yyjson_mut_val *key_val = yyjson_mut_strcpy(doc, key);
    yyjson_mut_val *val_val = yyjson_mut_uint(doc, val);
    if (!key_val || !val_val) {
        return false;
    }
    return yyjson_mut_obj_add(obj, key_val, val_val);
}

bool nmo_json_add_real_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                            const char *key, double val) {
    if (!doc || !obj || !key) {
        return false;
    }
    yyjson_mut_val *key_val = yyjson_mut_strcpy(doc, key);
    yyjson_mut_val *val_val = yyjson_mut_real(doc, val);
    if (!key_val || !val_val) {
        return false;
    }
    return yyjson_mut_obj_add(obj, key_val, val_val);
}

bool nmo_json_add_bool_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                            const char *key, bool val) {
    if (!doc || !obj || !key) {
        return false;
    }
    yyjson_mut_val *key_val = yyjson_mut_strcpy(doc, key);
    yyjson_mut_val *val_val = yyjson_mut_bool(doc, val);
    if (!key_val || !val_val) {
        return false;
    }
    return yyjson_mut_obj_add(obj, key_val, val_val);
}

bool nmo_json_add_null_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                            const char *key) {
    if (!doc || !obj || !key) {
        return false;
    }
    yyjson_mut_val *key_val = yyjson_mut_strcpy(doc, key);
    yyjson_mut_val *val_val = yyjson_mut_null(doc);
    if (!key_val || !val_val) {
        return false;
    }
    return yyjson_mut_obj_add(obj, key_val, val_val);
}

bool nmo_json_add_val_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                           const char *key, yyjson_mut_val *val) {
    if (!doc || !obj || !key) {
        return false;
    }
    yyjson_mut_val *key_val = yyjson_mut_strcpy(doc, key);
    if (!key_val) {
        return false;
    }
    return yyjson_mut_obj_add(obj, key_val, val);
}

bool nmo_json_add_data_hex(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                           const void *bytes, size_t data_size,
                           size_t max_bytes, bool uppercase) {
    if (!doc || !obj) {
        return false;
    }

    if (data_size == 0) {
        return true;
    }
    if (!bytes) {
        return false;
    }

    size_t emit_size = data_size;
    if (max_bytes > 0 && emit_size > max_bytes) {
        emit_size = max_bytes;
    }

    char *hex = nmo_hex_bytes_to_string(bytes, emit_size, uppercase);
    if (!hex) {
        return false;
    }

    yyjson_mut_obj_add_strcpy(doc, obj, "data_hex", hex);
    free(hex);

    if (!nmo_json_add_uint_safe(doc, obj, "data_emit_size", (uint64_t)emit_size)) {
        return false;
    }

    if (emit_size < data_size) {
        if (!nmo_json_add_bool_safe(doc, obj, "data_truncated", true)) {
            return false;
        }
        if (!nmo_json_add_uint_safe(doc, obj, "data_total_size", (uint64_t)data_size)) {
            return false;
        }
    }

    return true;
}
