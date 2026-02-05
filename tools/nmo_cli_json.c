/**
 * @file nmo_cli_json.c
 * @brief CLI JSON output with schema versioning
 */

#include "nmo_cli_json.h"

#include "nmo_cli_hex.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

yyjson_mut_doc *nmo_cli_json_create_doc(void) {
    return yyjson_mut_doc_new(NULL);
}

/**
 * Get current ISO 8601 timestamp
 */
static void get_iso_timestamp(char *buf, size_t buf_size) {
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    if (tm_info) {
        strftime(buf, buf_size, "%Y-%m-%dT%H:%M:%SZ", tm_info);
    } else {
        snprintf(buf, buf_size, "1970-01-01T00:00:00Z");
    }
}

yyjson_mut_val *nmo_cli_json_add_envelope(yyjson_mut_doc *doc,
                                          yyjson_mut_val *data,
                                          const char *command,
                                          const char *input_file) {
    if (!doc) {
        return NULL;
    }

    yyjson_mut_val *root = yyjson_mut_obj(doc);
    if (!root) {
        return NULL;
    }

    /* schema_version */
    yyjson_mut_obj_add_str(doc, root, "schema_version", NMO_CLI_JSON_SCHEMA_VERSION);

    /* tool */
    yyjson_mut_obj_add_str(doc, root, "tool", "nmo");

    /* command */
    if (command) {
        yyjson_mut_obj_add_str(doc, root, "command", command);
    }

    /* timestamp */
    char timestamp[32];
    get_iso_timestamp(timestamp, sizeof(timestamp));
    yyjson_mut_obj_add_strcpy(doc, root, "timestamp", timestamp);

    /* input_file (optional) */
    if (input_file) {
        nmo_cli_json_add_str_safe(doc, root, "input_file", input_file);
    }

    /* data */
    if (data) {
        yyjson_mut_obj_add_val(doc, root, "data", data);
    } else {
        yyjson_mut_obj_add_null(doc, root, "data");
    }

    return root;
}

bool nmo_cli_json_write(yyjson_mut_doc *doc, FILE *out, bool pretty) {
    if (!doc || !out) {
        return false;
    }

    yyjson_write_flag flags = pretty ? YYJSON_WRITE_PRETTY : 0;
    size_t len = 0;
    yyjson_write_err err;
    char *json = yyjson_mut_write_opts(doc, flags, NULL, &len, &err);
    if (!json) {
        fprintf(stderr, "Error: JSON write failed: %s\n", err.msg ? err.msg : "unknown error");
        return false;
    }

    size_t written = fwrite(json, 1, len, out);
    free(json);

    if (written == len) {
        fputc('\n', out);
        return true;
    }
    return false;
}

char *nmo_cli_json_write_string(yyjson_mut_doc *doc, bool pretty, size_t *out_len) {
    if (!doc) {
        return NULL;
    }

    yyjson_write_flag flags = pretty ? YYJSON_WRITE_PRETTY : 0;
    return yyjson_mut_write(doc, flags, out_len);
}

void nmo_cli_json_free_doc(yyjson_mut_doc *doc) {
    if (doc) {
        yyjson_mut_doc_free(doc);
    }
}

/**
 * @brief Check if a byte sequence is valid UTF-8 and return its length
 * @param s Pointer to the byte sequence
 * @param remaining Number of bytes remaining in string
 * @return Number of bytes in valid UTF-8 sequence, 0 if invalid
 */
static size_t utf8_char_len(const unsigned char *s, size_t remaining) {
    if (!s || remaining == 0) return 0;

    unsigned char c = s[0];

    /* ASCII (0x00-0x7F) */
    if (c < 0x80) {
        return 1;
    }

    /* 2-byte sequence (0xC0-0xDF) */
    if ((c & 0xE0) == 0xC0) {
        if (remaining < 2) return 0;
        if ((s[1] & 0xC0) != 0x80) return 0;
        /* Overlong check */
        if (c < 0xC2) return 0;
        return 2;
    }

    /* 3-byte sequence (0xE0-0xEF) */
    if ((c & 0xF0) == 0xE0) {
        if (remaining < 3) return 0;
        if ((s[1] & 0xC0) != 0x80) return 0;
        if ((s[2] & 0xC0) != 0x80) return 0;
        /* Overlong check */
        if (c == 0xE0 && s[1] < 0xA0) return 0;
        /* Surrogate check (0xD800-0xDFFF) */
        if (c == 0xED && s[1] >= 0xA0) return 0;
        return 3;
    }

    /* 4-byte sequence (0xF0-0xF7) */
    if ((c & 0xF8) == 0xF0) {
        if (remaining < 4) return 0;
        if ((s[1] & 0xC0) != 0x80) return 0;
        if ((s[2] & 0xC0) != 0x80) return 0;
        if ((s[3] & 0xC0) != 0x80) return 0;
        /* Overlong check */
        if (c == 0xF0 && s[1] < 0x90) return 0;
        /* Max codepoint check (U+10FFFF) */
        if (c == 0xF4 && s[1] > 0x8F) return 0;
        if (c > 0xF4) return 0;
        return 4;
    }

    return 0; /* Invalid lead byte */
}

/**
 * @brief Sanitize a string for JSON by replacing invalid UTF-8 bytes
 * @param str Input string
 * @param out Output buffer (must be at least 3x input length + 1 for worst case)
 * @param out_size Size of output buffer
 * @return Length of output string, or 0 on error
 */
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

bool nmo_cli_json_add_str_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                               const char *key, const char *str) {
    if (!doc || !obj || !key) return false;

    /* Create key value (copied) */
    yyjson_mut_val *key_val = yyjson_mut_strcpy(doc, key);
    if (!key_val) return false;

    if (!str) {
        yyjson_mut_val *null_val = yyjson_mut_null(doc);
        return yyjson_mut_obj_add(obj, key_val, null_val);
    }

    /* Check if string is already valid UTF-8 */
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
        /* String is already valid UTF-8 - copy it */
        str_val = yyjson_mut_strcpy(doc, str);
    } else {
        /* Preserve bytes by mapping to Latin-1 codepoints in UTF-8 */
        char *mapped = bytes_to_utf8_latin1(s, slen);
        if (!mapped) return false;
        str_val = yyjson_mut_strcpy(doc, mapped);
        free(mapped);
    }

    if (!str_val) return false;
    bool ok = yyjson_mut_obj_add(obj, key_val, str_val);

    if (needs_sanitize) {
        char hex_key[256];
        char len_key[256];
        if (snprintf(hex_key, sizeof(hex_key), "%s_raw_hex", key) > 0 &&
            snprintf(len_key, sizeof(len_key), "%s_raw_len", key) > 0) {
            char *hex = nmo_cli_bytes_to_hex(s, slen, true);
            if (hex) {
                yyjson_mut_obj_add_strcpy(doc, obj, hex_key, hex);
                free(hex);
            }
            yyjson_mut_obj_add_uint(doc, obj, len_key, (uint64_t)slen);
        }
    }

    return ok;
}

bool nmo_cli_json_add_str_safe_to_arr(yyjson_mut_doc *doc, yyjson_mut_val *arr,
                                      const char *str) {
    if (!doc || !arr) return false;
    if (!str) {
        yyjson_mut_arr_add_null(doc, arr);
        return true;
    }

    /* Check if string is already valid UTF-8 */
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
        /* String is already valid UTF-8 */
        yyjson_mut_arr_add_str(doc, arr, str);
        return true;
    }

    /* Add object with text + raw bytes for lossless export */
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    char *mapped = bytes_to_utf8_latin1(s, slen);
    if (!mapped) return false;
    yyjson_mut_obj_add_strcpy(doc, obj, "text", mapped);
    free(mapped);

    char *hex = nmo_cli_bytes_to_hex(s, slen, true);
    if (hex) {
        yyjson_mut_obj_add_strcpy(doc, obj, "raw_hex", hex);
        free(hex);
    }
    yyjson_mut_obj_add_uint(doc, obj, "raw_len", (uint64_t)slen);
    yyjson_mut_arr_add_val(arr, obj);
    return true;
}

/* Helper functions to add various value types with a copied key */

bool nmo_cli_json_add_int_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                               const char *key, int64_t val) {
    if (!doc || !obj || !key) return false;
    yyjson_mut_val *key_val = yyjson_mut_strcpy(doc, key);
    yyjson_mut_val *val_val = yyjson_mut_sint(doc, val);
    if (!key_val || !val_val) return false;
    return yyjson_mut_obj_add(obj, key_val, val_val);
}

bool nmo_cli_json_add_uint_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                const char *key, uint64_t val) {
    if (!doc || !obj || !key) return false;
    yyjson_mut_val *key_val = yyjson_mut_strcpy(doc, key);
    yyjson_mut_val *val_val = yyjson_mut_uint(doc, val);
    if (!key_val || !val_val) return false;
    return yyjson_mut_obj_add(obj, key_val, val_val);
}

bool nmo_cli_json_add_real_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                const char *key, double val) {
    if (!doc || !obj || !key) return false;
    yyjson_mut_val *key_val = yyjson_mut_strcpy(doc, key);
    yyjson_mut_val *val_val = yyjson_mut_real(doc, val);
    if (!key_val || !val_val) return false;
    return yyjson_mut_obj_add(obj, key_val, val_val);
}

bool nmo_cli_json_add_bool_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                const char *key, bool val) {
    if (!doc || !obj || !key) return false;
    yyjson_mut_val *key_val = yyjson_mut_strcpy(doc, key);
    yyjson_mut_val *val_val = yyjson_mut_bool(doc, val);
    if (!key_val || !val_val) return false;
    return yyjson_mut_obj_add(obj, key_val, val_val);
}

bool nmo_cli_json_add_null_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                const char *key) {
    if (!doc || !obj || !key) return false;
    yyjson_mut_val *key_val = yyjson_mut_strcpy(doc, key);
    yyjson_mut_val *val_val = yyjson_mut_null(doc);
    if (!key_val || !val_val) return false;
    return yyjson_mut_obj_add(obj, key_val, val_val);
}

bool nmo_cli_json_add_val_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                               const char *key, yyjson_mut_val *val) {
    if (!doc || !obj || !key) return false;
    yyjson_mut_val *key_val = yyjson_mut_strcpy(doc, key);
    if (!key_val) return false;
    return yyjson_mut_obj_add(obj, key_val, val);
}

bool nmo_cli_json_add_data_hex(yyjson_mut_doc *doc, yyjson_mut_val *obj,
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

    char *hex = nmo_cli_bytes_to_hex(bytes, emit_size, uppercase);
    if (!hex) {
        return false;
    }

    yyjson_mut_obj_add_strcpy(doc, obj, "data_hex", hex);
    free(hex);

    if (!nmo_cli_json_add_uint_safe(doc, obj, "data_emit_size", (uint64_t)emit_size)) {
        return false;
    }

    if (emit_size < data_size) {
        if (!nmo_cli_json_add_bool_safe(doc, obj, "data_truncated", true)) {
            return false;
        }
        if (!nmo_cli_json_add_uint_safe(doc, obj, "data_total_size", (uint64_t)data_size)) {
            return false;
        }
    }

    return true;
}
