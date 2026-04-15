/**
 * @file nmo_cmd_core.c
 * @brief Shared command core - reusable logic for CLI and REPL commands
 */

#include "nmo_cmd_core.h"
#include "nmo_cli_common.h"
#include "nmo_tool_common.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_edit.h"
#include "type/nmo_type_string.h"
#include "object/nmo_object_repository.h"
#include "core/nmo_guid.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_reflection.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

/* ============================================================================
 * 1. Class utilities
 * ============================================================================ */

const char *nmo_core_class_name(const nmo_cmd_ctx_t *c, nmo_class_id_t id) {
    return nmo_cli_class_name_from_id(c->ctx, id);
}

const char *nmo_core_class_name_or(const nmo_cmd_ctx_t *c, nmo_class_id_t id,
                                   char *buf, size_t sz) {
    const char *name = nmo_core_class_name(c, id);
    if (name) return name;
    snprintf(buf, sz, "Class#%u", (unsigned)id);
    return buf;
}

nmo_class_id_t nmo_core_class_id(const nmo_cmd_ctx_t *c, const char *name) {
    return nmo_cli_class_id_from_name(c->ctx, name);
}

bool nmo_core_class_derives(const nmo_cmd_ctx_t *c, nmo_class_id_t id,
                            nmo_class_id_t base) {
    return nmo_cli_class_is_derived_from(c->ctx, id, base);
}

/* ============================================================================
 * 2. Object lookup
 * ============================================================================ */

nmo_object_t *nmo_core_find_by_id(const nmo_cmd_ctx_t *c, nmo_object_id_t id) {
    nmo_object_repository_t *repo = nmo_session_get_repository(c->session);
    return nmo_object_repository_find_by_id(repo, id);
}

/* ============================================================================
 * 2b. Regex matching (lightweight, no external dependency)
 *
 * Supports: . * [] [^] ^ $ \-escapes, case-insensitive mode
 * ============================================================================ */

static char regex_fold_char(char c, bool icase) {
    if (!icase) {
        return c;
    }
    return (char)tolower((unsigned char)c);
}

static size_t regex_atom_length(const char *pattern) {
    if (!pattern || *pattern == '\0') {
        return 0;
    }
    if (pattern[0] == '\\' && pattern[1] != '\0') {
        return 2;
    }
    if (pattern[0] == '[') {
        size_t i = 1;
        if (pattern[i] == '^') {
            i++;
        }
        if (pattern[i] == ']') {
            i++;
        }
        while (pattern[i] && pattern[i] != ']') {
            if (pattern[i] == '\\' && pattern[i + 1]) {
                i += 2;
            } else {
                i++;
            }
        }
        if (pattern[i] == ']') {
            return i + 1;
        }
        return 0;
    }
    return 1;
}

static bool regex_class_match(const char *pattern, size_t len, char c,
                              bool icase) {
    bool negate = false;
    size_t i = 1;
    if (i < len && pattern[i] == '^') {
        negate = true;
        i++;
    }

    bool matched = false;
    char target = regex_fold_char(c, icase);

    while (i < len && pattern[i] != ']') {
        if (pattern[i] == '\\' && (i + 1) < len) {
            i++;
            if (regex_fold_char(pattern[i], icase) == target) {
                matched = true;
            }
            i++;
            continue;
        }

        if ((i + 2) < len && pattern[i + 1] == '-') {
            char start = regex_fold_char(pattern[i], icase);
            char end = regex_fold_char(pattern[i + 2], icase);
            if (start <= target && target <= end) {
                matched = true;
            }
            i += 3;
            continue;
        }

        if (regex_fold_char(pattern[i], icase) == target) {
            matched = true;
        }
        i++;
    }

    return negate ? !matched : matched;
}

static bool regex_match_here(const char *text, const char *pattern,
                             bool icase) {
    if (*pattern == '\0') {
        return true;
    }

    if (pattern[0] == '$' && pattern[1] == '\0') {
        return *text == '\0';
    }

    size_t atom_len = regex_atom_length(pattern);
    if (atom_len == 0) {
        return false;
    }

    bool star = pattern[atom_len] == '*';
    const char *next = pattern + atom_len + (star ? 1 : 0);

    if (star) {
        const char *t = text;
        while (*t) {
            bool atom_match = false;
            if (pattern[0] == '.') {
                atom_match = true;
            } else if (pattern[0] == '[') {
                atom_match = regex_class_match(pattern, atom_len, *t, icase);
            } else if (pattern[0] == '\\' && atom_len >= 2) {
                atom_match = regex_fold_char(pattern[1], icase) ==
                             regex_fold_char(*t, icase);
            } else {
                atom_match = regex_fold_char(pattern[0], icase) ==
                             regex_fold_char(*t, icase);
            }

            if (!atom_match) {
                break;
            }

            if (regex_match_here(t + 1, next, icase)) {
                return true;
            }
            t++;
        }
        return regex_match_here(text, next, icase);
    }

    if (*text == '\0') {
        return false;
    }

    if (pattern[0] == '.') {
        return regex_match_here(text + 1, next, icase);
    }

    if (pattern[0] == '[') {
        if (!regex_class_match(pattern, atom_len, *text, icase)) {
            return false;
        }
        return regex_match_here(text + 1, next, icase);
    }

    if (pattern[0] == '\\' && atom_len >= 2) {
        if (regex_fold_char(pattern[1], icase) !=
            regex_fold_char(*text, icase)) {
            return false;
        }
        return regex_match_here(text + 1, next, icase);
    }

    if (regex_fold_char(pattern[0], icase) !=
        regex_fold_char(*text, icase)) {
        return false;
    }

    return regex_match_here(text + 1, next, icase);
}

bool nmo_core_regex_match(const char *text, const char *pattern, bool icase) {
    if (!pattern) {
        return false;
    }

    if (pattern[0] == '^') {
        return regex_match_here(text ? text : "", pattern + 1, icase);
    }

    const char *t = text ? text : "";
    do {
        if (regex_match_here(t, pattern, icase)) {
            return true;
        }
    } while (*t++);

    return false;
}

/* ============================================================================
 * 3. Object iteration
 * ============================================================================ */

int nmo_core_iter_objects(const nmo_cmd_ctx_t *c,
                          const nmo_core_object_filter_t *filter,
                          nmo_core_object_fn visitor, void *user,
                          nmo_core_iter_result_t *result) {
    nmo_object_t **objects = NULL;
    size_t count = 0;
    if (nmo_session_get_objects(c->session, &objects, &count) != NMO_OK) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_core_iter_result_t r = { .total = count, .matched = 0, .visited = 0 };

    for (size_t i = 0; i < count; i++) {
        nmo_object_t *obj = objects[i];

        if (filter) {
            /* Class filter */
            if (filter->class_id) {
                nmo_class_id_t cid = nmo_object_get_class_id(obj);
                if (filter->class_derived) {
                    if (!nmo_core_class_derives(c, cid, filter->class_id))
                        continue;
                } else {
                    if (cid != filter->class_id) continue;
                }
            }

            /* Object ID filter */
            if (filter->object_id &&
                nmo_object_get_id(obj) != filter->object_id)
                continue;

            /* Name wildcard */
            if (filter->name_pattern) {
                const char *name = nmo_object_get_name(obj);
                if (!nmo_tool_match_wildcard_ci(filter->name_pattern,
                                               name ? name : ""))
                    continue;
            }

            /* Name substring */
            if (filter->name_substr) {
                const char *name = nmo_object_get_name(obj);
                if (!name || !strstr(name, filter->name_substr)) continue;
            }

            /* Name regex */
            if (filter->name_regex) {
                const char *name = nmo_object_get_name(obj);
                if (!nmo_core_regex_match(name ? name : "",
                                         filter->name_regex,
                                         filter->regex_icase))
                    continue;
            }

            /* DSL filter */
            if (filter->dsl_filter) {
                nmo_dsl_eval_context_t eval_ctx;
                if (!nmo_core_dsl_setup_ctx(c, obj, &eval_ctx)) continue;
                nmo_dsl_value_t val = {0};
                nmo_status_t st = nmo_dsl_eval_expr(filter->dsl_filter,
                                                    &eval_ctx, &val);
                bool match = (st == NMO_OK) && nmo_core_dsl_is_truthy(&val);
                nmo_dsl_value_destroy(&val);
                if (!match) continue;
            }
        }

        r.matched++;
        if (visitor) {
            int rc = visitor(i, obj, c, user);
            r.visited++;
            if (rc != 0) break;
        }
    }

    if (result) *result = r;
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * 4. Reference iteration
 * ============================================================================ */

int nmo_core_iter_refs(const nmo_cmd_ctx_t *c,
                       nmo_object_id_t obj_id,
                       unsigned dir,
                       nmo_core_ref_fn visitor, void *user,
                       nmo_core_ref_result_t *result) {
    nmo_object_repository_t *repo = nmo_session_get_repository(c->session);

    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(c->session);
    if (!graph) return NMO_CLI_EXIT_INTERNAL_ERROR;

    nmo_core_ref_result_t r = {0};

    /* Outgoing edges */
    if (dir & NMO_CORE_REFS_OUT) {
        nmo_ref_edge_t *edges = NULL;
        size_t count = 0;
        nmo_ref_graph_get_object_edges(graph, obj_id, NMO_REF_DIR_OUTGOING,
                                       &edges, &count);
        r.outgoing = count;
        if (visitor) {
            for (size_t i = 0; i < count; i++) {
                nmo_core_ref_info_t info = {0};
                info.edge = &edges[i];
                info.is_incoming = false;
                info.peer = nmo_object_repository_find_by_id(repo,
                                                             edges[i].to);
                if (info.peer) {
                    info.peer_name = nmo_object_get_name(info.peer);
                    info.peer_class_name = nmo_core_class_name(
                        c, nmo_object_get_class_id(info.peer));
                }
                if (visitor(&info, c, user) != 0) break;
            }
        }
    }

    /* Incoming edges */
    if (dir & NMO_CORE_REFS_IN) {
        nmo_ref_edge_t *edges = NULL;
        size_t count = 0;
        nmo_ref_graph_get_object_edges(graph, obj_id, NMO_REF_DIR_INCOMING,
                                       &edges, &count);
        r.incoming = count;
        if (visitor) {
            for (size_t i = 0; i < count; i++) {
                nmo_core_ref_info_t info = {0};
                info.edge = &edges[i];
                info.is_incoming = true;
                info.peer = nmo_object_repository_find_by_id(repo,
                                                             edges[i].from);
                if (info.peer) {
                    info.peer_name = nmo_object_get_name(info.peer);
                    info.peer_class_name = nmo_core_class_name(
                        c, nmo_object_get_class_id(info.peer));
                }
                if (visitor(&info, c, user) != 0) break;
            }
        }
    }

    if (result) *result = r;
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * 5. DSL evaluation helpers
 * ============================================================================ */

bool nmo_core_dsl_is_truthy(const nmo_dsl_value_t *val) {
    switch (val->kind) {
        case NMO_DSL_VALUE_BOOL:   return val->as.b;
        case NMO_DSL_VALUE_INT:    return val->as.i != 0;
        case NMO_DSL_VALUE_UINT:   return val->as.u != 0;
        case NMO_DSL_VALUE_REAL:   return val->as.r != 0.0;
        case NMO_DSL_VALUE_STRING: return val->as.s != NULL &&
                                          val->as.s[0] != '\0';
        case NMO_DSL_VALUE_NULL:   return false;
        default:                   return true;
    }
}

bool nmo_core_dsl_setup_ctx(const nmo_cmd_ctx_t *c, nmo_object_t *obj,
                            nmo_dsl_eval_context_t *out) {
    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    if (!chunk) return false;

    nmo_type_id_t type_id = nmo_type_registry_class_id_to_type_id(
        c->registry, (uint32_t)nmo_object_get_class_id(obj));
    const nmo_type_descriptor_t *type_desc = NULL;
    if (type_id != NMO_TYPE_ID_INVALID) {
        type_desc = nmo_type_registry_get_by_id(c->registry, type_id);
    }

    size_t data_size = 0;
    const void *instance = nmo_chunk_get_data(chunk, &data_size);

    memset(out, 0, sizeof(*out));
    out->registry = c->registry;
    out->root_type = type_desc;
    out->root_instance = (void *)instance;
    out->current_type = type_desc;
    out->current_instance = instance;
    return true;
}

nmo_status_t nmo_core_dsl_eval(const nmo_cmd_ctx_t *c, nmo_object_t *obj,
                               const char *expr, nmo_dsl_value_t *result) {
    nmo_dsl_eval_context_t eval_ctx;
    if (!nmo_core_dsl_setup_ctx(c, obj, &eval_ctx)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return nmo_dsl_eval_one(c->registry, &eval_ctx, expr, result);
}

bool nmo_core_dsl_format(const nmo_dsl_value_t *value, char *buf,
                         size_t buf_size) {
    if (!value || !buf || buf_size == 0) {
        return false;
    }

    switch (value->kind) {
        case NMO_DSL_VALUE_NULL:
            snprintf(buf, buf_size, "null");
            return true;

        case NMO_DSL_VALUE_BOOL:
            snprintf(buf, buf_size, "%s", value->as.b ? "true" : "false");
            return true;

        case NMO_DSL_VALUE_INT:
            snprintf(buf, buf_size, "%lld", (long long)value->as.i);
            return true;

        case NMO_DSL_VALUE_UINT:
            snprintf(buf, buf_size, "%llu", (unsigned long long)value->as.u);
            return true;

        case NMO_DSL_VALUE_REAL:
            snprintf(buf, buf_size, "%g", value->as.r);
            return true;

        case NMO_DSL_VALUE_STRING:
            if (value->as.s) {
                snprintf(buf, buf_size, "\"%s\"", value->as.s);
            } else {
                snprintf(buf, buf_size, "\"\"");
            }
            return true;

        case NMO_DSL_VALUE_BYREF: {
            if (!value->as.byref.ptr) {
                snprintf(buf, buf_size, "null");
                return true;
            }
            nmo_guid_t g = value->as.byref.type
                ? value->as.byref.type->guid : value->as.byref.guid;
            size_t sz = value->as.byref.size;
            if (sz == 0 && value->as.byref.type)
                sz = (size_t)value->as.byref.type->size;
            const void *p = value->as.byref.ptr;

            /* Decode common primitive types */
            if (nmo_guid_equals(g, CKPGUID_BOOL) && sz >= 1) {
                snprintf(buf, buf_size, "%s",
                         (*(const uint8_t *)p) ? "true" : "false");
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_INT) && sz >= 4) {
                snprintf(buf, buf_size, "%d", *(const int32_t *)p);
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_UINT32) && sz >= 4) {
                snprintf(buf, buf_size, "%u", *(const uint32_t *)p);
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_FLOAT) && sz >= 4) {
                snprintf(buf, buf_size, "%g", (double)*(const float *)p);
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_DOUBLE) && sz >= 8) {
                snprintf(buf, buf_size, "%g", *(const double *)p);
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_STRING)) {
                const char *s = *(const char *const *)p;
                snprintf(buf, buf_size, "%s", s ? s : "(null)");
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_ID) && sz >= 4) {
                snprintf(buf, buf_size, "#%u", *(const uint32_t *)p);
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_INT64) && sz >= 8) {
                snprintf(buf, buf_size, "%lld",
                         (long long)*(const int64_t *)p);
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_UINT64) && sz >= 8) {
                snprintf(buf, buf_size, "%llu",
                         (unsigned long long)*(const uint64_t *)p);
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_INT16) && sz >= 2) {
                snprintf(buf, buf_size, "%d", (int)*(const int16_t *)p);
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_UINT16) && sz >= 2) {
                snprintf(buf, buf_size, "%u",
                         (unsigned)*(const uint16_t *)p);
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_INT8) && sz >= 1) {
                snprintf(buf, buf_size, "%d", (int)*(const int8_t *)p);
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_UINT8) && sz >= 1) {
                snprintf(buf, buf_size, "%u",
                         (unsigned)*(const uint8_t *)p);
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_VECTOR) && sz >= 12) {
                const float *v = (const float *)p;
                snprintf(buf, buf_size, "(%g, %g, %g)",
                         (double)v[0], (double)v[1], (double)v[2]);
                return true;
            }
            if (nmo_guid_equals(g, CKPGUID_GUID) && sz >= 8) {
                const nmo_guid_t *gv = (const nmo_guid_t *)p;
                snprintf(buf, buf_size, "{%08X-%08X}", gv->d1, gv->d2);
                return true;
            }
            /* Fallback: show hex bytes for small values */
            if (sz <= 8 && sz > 0) {
                const uint8_t *bytes = (const uint8_t *)p;
                char *pos = buf;
                size_t rem = buf_size;
                int n = snprintf(pos, rem, "0x");
                pos += n; rem -= (size_t)n;
                for (size_t bi = 0; bi < sz && rem > 2; bi++) {
                    n = snprintf(pos, rem, "%02X", bytes[bi]);
                    pos += n; rem -= (size_t)n;
                }
                return true;
            }
            snprintf(buf, buf_size, "<byref:%zu bytes>", sz);
            return true;
        }

        case NMO_DSL_VALUE_OBJECT:
            snprintf(buf, buf_size, "<object:%p>", value->as.object.instance);
            return true;

        case NMO_DSL_VALUE_SEQ: {
            uint64_t count = nmo_dsl_seq_count(value->as.seq);
            snprintf(buf, buf_size, "<seq:%llu>",
                     (unsigned long long)count);
            return true;
        }

        case NMO_DSL_VALUE_TYPE:
            snprintf(buf, buf_size, "<type>");
            return true;

        default:
            snprintf(buf, buf_size, "<unknown>");
            return false;
    }
}

/* ============================================================================
 * 6. DSL error display with source context
 * ============================================================================ */

void nmo_core_dsl_print_error(FILE *stream, const char *source,
                              const char *prefix) {
    /* Get last error detail */
    char detail[256];
    size_t detail_len = nmo_last_error_message_copy(detail, sizeof(detail));

    /* Try to parse "line:col: message" from the error detail */
    uint32_t err_line = 0;
    uint32_t err_col = 0;
    const char *err_msg = NULL;

    if (detail_len > 0) {
        char *p = detail;
        char *end = NULL;
        unsigned long l = strtoul(p, &end, 10);
        if (end && end != p && *end == ':') {
            p = end + 1;
            unsigned long c = strtoul(p, &end, 10);
            if (end && end != p && *end == ':') {
                err_line = (uint32_t)l;
                err_col = (uint32_t)c;
                /* Skip ": " */
                err_msg = end + 1;
                while (*err_msg == ' ') err_msg++;
            }
        }
    }

    /* Print the main error line */
    if (err_msg && err_msg[0]) {
        if (err_line > 0 && err_col > 0) {
            fprintf(stream, "%s at line %u, column %u: %s\n",
                    prefix, err_line, err_col, err_msg);
        } else {
            fprintf(stream, "%s: %s\n", prefix, err_msg);
        }
    } else if (detail_len > 0) {
        fprintf(stream, "%s: %s\n", prefix, detail);
    } else {
        fprintf(stream, "%s\n", prefix);
    }

    /* Show source context with caret if we have position info */
    if (source && err_line > 0 && err_col > 0) {
        /* Find the start of the error line in source */
        const char *line_start = source;
        for (uint32_t i = 1; i < err_line && *line_start; i++) {
            while (*line_start && *line_start != '\n') line_start++;
            if (*line_start == '\n') line_start++;
        }

        /* Find the end of this line */
        const char *line_end = line_start;
        while (*line_end && *line_end != '\n' && *line_end != '\r') line_end++;

        size_t line_len = (size_t)(line_end - line_start);
        if (line_len > 0) {
            fprintf(stream, "  %.*s\n", (int)line_len, line_start);
            /* Print caret at err_col (1-based) */
            fprintf(stream, "  ");
            for (uint32_t i = 1; i < err_col; i++) {
                fputc(' ', stream);
            }
            fprintf(stream, "^\n");
        }
    }
}

/* ============================================================================
 * 7. Field mutation
 * ============================================================================ */

int nmo_core_set_fields(nmo_cmd_ctx_t *c, nmo_object_id_t object_id,
                        const nmo_field_set_entry_t *entries, size_t entry_count,
                        bool dry_run, nmo_field_set_result_t *out_result)
{
    nmo_field_set_result_t result = {0, 0};

    /* Resolve object -> state + type descriptor once */
    nmo_object_repository_t *repo = nmo_session_get_repository(c->session);
    if (!repo) {
        fprintf(stderr, "Error: No object repository\n");
        if (out_result) *out_result = result;
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, object_id);
    if (!obj) {
        fprintf(stderr, "Error: Object #%u not found\n", object_id);
        if (out_result) *out_result = result;
        return NMO_CLI_EXIT_NOT_FOUND;
    }

    void *state = nmo_object_get_state(obj);
    if (!state) {
        fprintf(stderr, "Error: Object #%u has no typed state\n", object_id);
        if (out_result) *out_result = result;
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_class_id_inherited(
            c->registry, nmo_object_get_class_id(obj));
    if (!type) {
        fprintf(stderr, "Error: No type descriptor for object #%u\n", object_id);
        if (out_result) *out_result = result;
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_session_edit_t *edit = NULL;
    nmo_status_t begin_rc =
        nmo_session_edit_begin(c->session, "cli set fields", &edit);
    if (begin_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin edit: %s\n",
                nmo_error_string(begin_rc));
        if (out_result) *out_result = result;
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    for (size_t i = 0; i < entry_count; i++) {
        const char *fname = entries[i].field_name;
        const char *vstr  = entries[i].value_str;

        /* Read old value */
        char old_buf[256];
        old_buf[0] = '\0';
        nmo_type_get_field(state, type, c->registry, fname,
                           old_buf, sizeof(old_buf));

        nmo_session_field_edit_t field = {
            .field_name = fname,
            .value_str = vstr,
        };
        nmo_status_t rc =
            nmo_session_edit_set_object_fields(edit, object_id, &field, 1, NULL);
        if (rc != NMO_OK) {
            fprintf(stderr, "Error: Failed to set '%s' = '%s': %s\n",
                    fname, vstr, nmo_error_string(rc));
            result.failed++;
            continue;
        }

        /* Read new value */
        char new_buf[256];
        new_buf[0] = '\0';
        nmo_type_get_field(state, type, c->registry, fname,
                           new_buf, sizeof(new_buf));

        /* Print change */
        fprintf(c->out, "  %s: %s -> %s%s\n",
                fname, old_buf, new_buf, dry_run ? " (dry-run)" : "");

        result.applied++;
    }

    if (dry_run || result.failed > 0) {
        nmo_session_edit_rollback(edit);
    } else {
        nmo_status_t commit_rc = nmo_session_edit_commit(edit);
        if (commit_rc != NMO_OK) {
            fprintf(stderr, "Error: Failed to commit edit: %s\n",
                    nmo_error_string(commit_rc));
            result.failed++;
        }
    }

    if (out_result) *out_result = result;
    return result.failed > 0 ? NMO_CLI_EXIT_INTERNAL_ERROR : NMO_CLI_EXIT_SUCCESS;
}
