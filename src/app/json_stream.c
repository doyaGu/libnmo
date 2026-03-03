#include "app/nmo_json_stream.h"
#include "core/nmo_hex.h"

#include <math.h>
#include <string.h>

enum {
    NMO_JSON_CTX_OBJECT = 1,
    NMO_JSON_CTX_ARRAY = 2
};

static bool write_char(nmo_json_stream_t *writer, char c) {
    if (!writer || !writer->out) {
        return false;
    }
    if (fputc((unsigned char)c, writer->out) == EOF) {
        writer->failed = true;
        return false;
    }
    return true;
}

static bool write_str(nmo_json_stream_t *writer, const char *s) {
    if (!writer || !writer->out || !s) {
        return false;
    }
    if (fputs(s, writer->out) < 0) {
        writer->failed = true;
        return false;
    }
    return true;
}

static bool write_indent(nmo_json_stream_t *writer, size_t spaces) {
    if (!writer) {
        return false;
    }
    for (size_t i = 0; i < spaces; ++i) {
        if (!write_char(writer, ' ')) {
            return false;
        }
    }
    return true;
}

static bool write_escaped_string(nmo_json_stream_t *writer, const char *s) {
    if (!writer || !s) {
        return false;
    }

    if (!write_char(writer, '"')) {
        return false;
    }

    const unsigned char *p = (const unsigned char *)s;
    for (; *p; ++p) {
        unsigned char c = *p;
        switch (c) {
            case '\"':
                if (!write_str(writer, "\\\"")) return false;
                break;
            case '\\':
                if (!write_str(writer, "\\\\")) return false;
                break;
            case '\b':
                if (!write_str(writer, "\\b")) return false;
                break;
            case '\f':
                if (!write_str(writer, "\\f")) return false;
                break;
            case '\n':
                if (!write_str(writer, "\\n")) return false;
                break;
            case '\r':
                if (!write_str(writer, "\\r")) return false;
                break;
            case '\t':
                if (!write_str(writer, "\\t")) return false;
                break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    (void)snprintf(buf, sizeof(buf), "\\u%04X", (unsigned)c);
                    if (!write_str(writer, buf)) return false;
                } else {
                    if (!write_char(writer, (char)c)) return false;
                }
                break;
        }
    }

    return write_char(writer, '"');
}

static nmo_json_stream_ctx_t *top_ctx(nmo_json_stream_t *writer) {
    if (!writer || writer->depth == 0) {
        return NULL;
    }
    return &writer->stack[writer->depth - 1];
}

static bool before_value(nmo_json_stream_t *writer) {
    if (!writer || writer->failed) {
        return false;
    }

    if (writer->depth == 0) {
        if (writer->wrote_root) {
            writer->failed = true;
            return false;
        }
        writer->wrote_root = true;
        return true;
    }

    nmo_json_stream_ctx_t *ctx = top_ctx(writer);
    if (!ctx) {
        writer->failed = true;
        return false;
    }

    if (ctx->type == NMO_JSON_CTX_OBJECT) {
        if (!ctx->pending_key) {
            writer->failed = true;
            return false;
        }
        ctx->pending_key = false;
        return true;
    }

    if (ctx->type == NMO_JSON_CTX_ARRAY) {
        if (!ctx->first) {
            if (!write_char(writer, ',')) {
                return false;
            }
        }
        if (writer->pretty) {
            if (!write_char(writer, '\n') ||
                !write_indent(writer, writer->depth * 2)) {
                return false;
            }
        }
        ctx->first = false;
        return true;
    }

    writer->failed = true;
    return false;
}

static bool push_ctx(nmo_json_stream_t *writer, uint8_t type) {
    if (!writer || writer->depth >= (sizeof(writer->stack) / sizeof(writer->stack[0]))) {
        if (writer) {
            writer->failed = true;
        }
        return false;
    }
    writer->stack[writer->depth++] = (nmo_json_stream_ctx_t){
        .type = type,
        .first = true,
        .pending_key = false
    };
    return true;
}

void nmo_json_stream_init(nmo_json_stream_t *writer, FILE *out, bool pretty) {
    if (!writer) {
        return;
    }
    memset(writer, 0, sizeof(*writer));
    writer->out = out;
    writer->pretty = pretty;
}

bool nmo_json_stream_ok(const nmo_json_stream_t *writer) {
    return writer && !writer->failed;
}

bool nmo_json_stream_begin_object(nmo_json_stream_t *writer) {
    if (!before_value(writer)) {
        return false;
    }
    if (!write_char(writer, '{')) {
        return false;
    }
    return push_ctx(writer, NMO_JSON_CTX_OBJECT);
}

bool nmo_json_stream_end_object(nmo_json_stream_t *writer) {
    nmo_json_stream_ctx_t *ctx = top_ctx(writer);
    if (!ctx || ctx->type != NMO_JSON_CTX_OBJECT || ctx->pending_key) {
        if (writer) {
            writer->failed = true;
        }
        return false;
    }

    if (writer->pretty && !ctx->first) {
        if (!write_char(writer, '\n') ||
            !write_indent(writer, (writer->depth - 1) * 2)) {
            return false;
        }
    }
    if (!write_char(writer, '}')) {
        return false;
    }

    writer->depth--;
    return true;
}

bool nmo_json_stream_begin_array(nmo_json_stream_t *writer) {
    if (!before_value(writer)) {
        return false;
    }
    if (!write_char(writer, '[')) {
        return false;
    }
    return push_ctx(writer, NMO_JSON_CTX_ARRAY);
}

bool nmo_json_stream_end_array(nmo_json_stream_t *writer) {
    nmo_json_stream_ctx_t *ctx = top_ctx(writer);
    if (!ctx || ctx->type != NMO_JSON_CTX_ARRAY) {
        if (writer) {
            writer->failed = true;
        }
        return false;
    }

    if (writer->pretty && !ctx->first) {
        if (!write_char(writer, '\n') ||
            !write_indent(writer, (writer->depth - 1) * 2)) {
            return false;
        }
    }
    if (!write_char(writer, ']')) {
        return false;
    }

    writer->depth--;
    return true;
}

bool nmo_json_stream_key(nmo_json_stream_t *writer, const char *key) {
    if (!writer || !key || writer->failed) {
        return false;
    }

    nmo_json_stream_ctx_t *ctx = top_ctx(writer);
    if (!ctx || ctx->type != NMO_JSON_CTX_OBJECT || ctx->pending_key) {
        writer->failed = true;
        return false;
    }

    if (!ctx->first) {
        if (!write_char(writer, ',')) {
            return false;
        }
    }
    if (writer->pretty) {
        if (!write_char(writer, '\n') ||
            !write_indent(writer, writer->depth * 2)) {
            return false;
        }
    }

    if (!write_escaped_string(writer, key)) {
        return false;
    }
    if (!write_char(writer, ':')) {
        return false;
    }
    if (writer->pretty) {
        if (!write_char(writer, ' ')) {
            return false;
        }
    }

    ctx->first = false;
    ctx->pending_key = true;
    return true;
}

bool nmo_json_stream_value_string(nmo_json_stream_t *writer, const char *value) {
    if (!before_value(writer)) {
        return false;
    }
    if (!value) {
        return write_str(writer, "null");
    }
    return write_escaped_string(writer, value);
}

bool nmo_json_stream_value_uint(nmo_json_stream_t *writer, uint64_t value) {
    char buf[32];
    (void)snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    if (!before_value(writer)) {
        return false;
    }
    return write_str(writer, buf);
}

bool nmo_json_stream_value_sint(nmo_json_stream_t *writer, int64_t value) {
    char buf[32];
    (void)snprintf(buf, sizeof(buf), "%lld", (long long)value);
    if (!before_value(writer)) {
        return false;
    }
    return write_str(writer, buf);
}

bool nmo_json_stream_value_real(nmo_json_stream_t *writer, double value) {
    char buf[64];
    if (!before_value(writer)) {
        return false;
    }
    if (!isfinite(value)) {
        return write_str(writer, "null");
    }
    (void)snprintf(buf, sizeof(buf), "%.17g", value);
    return write_str(writer, buf);
}

bool nmo_json_stream_value_bool(nmo_json_stream_t *writer, bool value) {
    if (!before_value(writer)) {
        return false;
    }
    return write_str(writer, value ? "true" : "false");
}

bool nmo_json_stream_value_null(nmo_json_stream_t *writer) {
    if (!before_value(writer)) {
        return false;
    }
    return write_str(writer, "null");
}

bool nmo_json_stream_value_hex_bytes(nmo_json_stream_t *writer,
                                     const void *bytes,
                                     size_t len,
                                     bool uppercase) {
    const uint8_t *data = (const uint8_t *)bytes;

    if (!before_value(writer)) {
        return false;
    }
    if (!bytes && len != 0) {
        writer->failed = true;
        return false;
    }

    if (!write_char(writer, '"')) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        char hex[2];
        nmo_hex_write_byte(hex, data[i], uppercase);
        if (!write_char(writer, hex[0]) ||
            !write_char(writer, hex[1])) {
            return false;
        }
    }
    return write_char(writer, '"');
}
