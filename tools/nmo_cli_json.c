/**
 * @file nmo_cli_json.c
 * @brief CLI JSON output with schema versioning
 */

#include "nmo_cli_json.h"
#include "../src/export/export_json_util_internal.h"

#include <stdlib.h>
#include <time.h>

yyjson_mut_doc *nmo_cli_json_create_doc(void) {
    return yyjson_mut_doc_new(NULL);
}

bool nmo_cli_json_create_data_doc(yyjson_mut_doc **out_doc, yyjson_mut_val **out_data) {
    if (!out_doc || !out_data) {
        return false;
    }

    *out_doc = NULL;
    *out_data = NULL;

    yyjson_mut_doc *doc = nmo_cli_json_create_doc();
    if (!doc) {
        return false;
    }

    yyjson_mut_val *data = yyjson_mut_obj(doc);
    if (!data) {
        nmo_cli_json_free_doc(doc);
        return false;
    }

    *out_doc = doc;
    *out_data = data;
    return true;
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

    yyjson_write_flag flags = (pretty ? YYJSON_WRITE_PRETTY : 0) |
                              YYJSON_WRITE_ESCAPE_UNICODE |
                              YYJSON_WRITE_ALLOW_INVALID_UNICODE;
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

    yyjson_write_flag flags = (pretty ? YYJSON_WRITE_PRETTY : 0) |
                              YYJSON_WRITE_ESCAPE_UNICODE |
                              YYJSON_WRITE_ALLOW_INVALID_UNICODE;
    return yyjson_mut_write(doc, flags, out_len);
}

void nmo_cli_json_free_doc(yyjson_mut_doc *doc) {
    if (doc) {
        yyjson_mut_doc_free(doc);
    }
}

bool nmo_cli_json_write_enveloped_and_free(yyjson_mut_doc *doc,
                                           yyjson_mut_val *data,
                                           const char *command,
                                           const char *input_file,
                                           FILE *out,
                                           bool pretty) {
    if (!doc || !out) {
        nmo_cli_json_free_doc(doc);
        return false;
    }

    yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, command, input_file);
    if (!root) {
        nmo_cli_json_free_doc(doc);
        return false;
    }

    yyjson_mut_doc_set_root(doc, root);
    bool ok = nmo_cli_json_write(doc, out, pretty);
    nmo_cli_json_free_doc(doc);
    return ok;
}

bool nmo_cli_json_add_str_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                               const char *key, const char *str) {
    return nmo_json_add_str_safe(doc, obj, key, str);
}

bool nmo_cli_json_add_str_safe_to_arr(yyjson_mut_doc *doc, yyjson_mut_val *arr,
                                      const char *str) {
    return nmo_json_add_str_safe_to_arr(doc, arr, str);
}

bool nmo_cli_json_add_int_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                               const char *key, int64_t val) {
    return nmo_json_add_int_safe(doc, obj, key, val);
}

bool nmo_cli_json_add_uint_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                const char *key, uint64_t val) {
    return nmo_json_add_uint_safe(doc, obj, key, val);
}

bool nmo_cli_json_add_real_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                const char *key, double val) {
    return nmo_json_add_real_safe(doc, obj, key, val);
}

bool nmo_cli_json_add_bool_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                const char *key, bool val) {
    return nmo_json_add_bool_safe(doc, obj, key, val);
}

bool nmo_cli_json_add_null_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                const char *key) {
    return nmo_json_add_null_safe(doc, obj, key);
}

bool nmo_cli_json_add_val_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                               const char *key, yyjson_mut_val *val) {
    return nmo_json_add_val_safe(doc, obj, key, val);
}

bool nmo_cli_json_add_data_hex(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                               const void *bytes, size_t data_size,
                               size_t max_bytes, bool uppercase) {
    return nmo_json_add_data_hex(doc, obj, bytes, data_size, max_bytes, uppercase);
}
