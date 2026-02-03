#include "nmo_debug_util.h"

#include "nmo_tool_common.h"

#include "app/nmo_context.h"
#include "type/type_system.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *nmo_debug_basename(const char *path) {
    if (!path || !*path) {
        return "";
    }

    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *sep = slash;
    if (!sep || (bslash && bslash > sep)) {
        sep = bslash;
    }
    return sep ? (sep + 1) : path;
}

void nmo_debug_print_prompt(const nmo_debug_context_t *dbg) {
    const char *file_label = "";
    if (dbg && dbg->filename && dbg->filename[0] != '\0') {
        file_label = nmo_debug_basename(dbg->filename);
    }

    if (dbg && dbg->has_selection) {
        size_t object_count = 0;
        nmo_object_t **objects = NULL;
        nmo_session_get_objects(dbg->session, &objects, &object_count);
        if (dbg->selected_index < object_count) {
            nmo_object_id_t id = nmo_object_get_id(objects[dbg->selected_index]);
            if (file_label[0]) {
                printf("nmo(debug:%s idx=%zu id=%u)> ", file_label, dbg->selected_index, id);
            } else {
                printf("nmo(debug idx=%zu id=%u)> ", dbg->selected_index, id);
            }
            fflush(stdout);
            return;
        }
    }

    if (file_label[0]) {
        printf("nmo(debug:%s)> ", file_label);
    } else {
        printf("nmo(debug)> ");
    }
    fflush(stdout);
}

int nmo_debug_parse_command(char *line, char **argv, int max_args) {
    int argc = 0;
    char *p = line;
    bool in_quotes = false;
    char *arg_start = NULL;

    while (*p && argc < max_args) {
        if (isspace((unsigned char)*p) && !in_quotes) {
            if (arg_start) {
                *p = '\0';
                argv[argc++] = arg_start;
                arg_start = NULL;
            }
        } else if (*p == '"') {
            if (!in_quotes) {
                in_quotes = true;
                arg_start = p + 1;
            } else {
                *p = '\0';
                argv[argc++] = arg_start;
                arg_start = NULL;
                in_quotes = false;
            }
        } else if (!arg_start) {
            arg_start = p;
        }
        p++;
    }

    if (arg_start && argc < max_args) {
        argv[argc++] = arg_start;
    }

    return argc;
}

void nmo_debug_get_objects(nmo_debug_context_t *dbg, nmo_object_t ***objects, size_t *count) {
    nmo_session_get_objects(dbg->session, objects, count);
}

const char *nmo_debug_class_name_from_id(const nmo_debug_context_t *dbg,
                                        nmo_class_id_t class_id,
                                        char *buffer,
                                        size_t buffer_size) {
    const char *name = NULL;
    if (dbg && dbg->ctx) {
        nmo_type_registry_t *registry = nmo_context_get_type_registry(dbg->ctx);
        if (registry) {
            nmo_type_id_t type_id = nmo_type_registry_class_id_to_type_id(registry, (uint32_t)class_id);
            if (type_id != NMO_TYPE_ID_INVALID) {
                name = nmo_type_registry_type_id_to_name(registry, type_id);
            }
        }
    }

    if (name) {
        return name;
    }

    if (buffer && buffer_size) {
        snprintf(buffer, buffer_size, "Class#%u", (unsigned int)class_id);
        return buffer;
    }
    return "(unknown class)";
}

bool nmo_debug_class_id_from_name(const nmo_debug_context_t *dbg, const char *name, nmo_class_id_t *out_class_id) {
    if (!out_class_id) {
        return false;
    }
    *out_class_id = 0;

    if (!dbg || !dbg->ctx || !name || !name[0]) {
        return false;
    }

    nmo_type_registry_t *registry = nmo_context_get_type_registry(dbg->ctx);
    if (!registry) {
        return false;
    }

    nmo_type_id_t type_id = nmo_type_registry_name_to_type_id(registry, name);
    if (type_id == NMO_TYPE_ID_INVALID) {
        return false;
    }

    uint32_t class_id_u32 = 0;
    nmo_status_t rc = nmo_type_registry_type_id_to_class_id(registry, type_id, &class_id_u32);
    if (rc != NMO_OK || class_id_u32 == 0) {
        return false;
    }

    *out_class_id = (nmo_class_id_t)class_id_u32;
    return true;
}

void nmo_debug_print_object_summary(const nmo_debug_context_t *dbg, size_t index, nmo_object_t *obj) {
    nmo_object_id_t obj_id = nmo_object_get_id(obj);
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const char *name = nmo_object_get_name(obj);

    char class_buf[64];
    const char *class_name = nmo_debug_class_name_from_id(dbg, class_id, class_buf, sizeof(class_buf));

    printf("  [%3zu] ID=%-5u Class=%-3d %-24s %s\n",
           index,
           obj_id,
           class_id,
           class_name,
           name ? name : "(unnamed)");
}

void nmo_debug_print_object_summary_marked(const nmo_debug_context_t *dbg,
                                          size_t index,
                                          nmo_object_t *obj,
                                          bool selected) {
    nmo_object_id_t obj_id = nmo_object_get_id(obj);
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const char *name = nmo_object_get_name(obj);

    char class_buf[64];
    const char *class_name = nmo_debug_class_name_from_id(dbg, class_id, class_buf, sizeof(class_buf));

    printf("%c [%3zu] ID=%-5u Class=%-3d %-24s %s\n",
           selected ? '>' : ' ',
           index,
           obj_id,
           class_id,
           class_name,
           name ? name : "(unnamed)");
}

bool nmo_debug_paginate_if_needed(nmo_debug_context_t *dbg, size_t printed) {
    if (!dbg || dbg->page_size == 0 || printed == 0) {
        return true;
    }

    if ((printed % dbg->page_size) != 0) {
        return true;
    }

    printf("-- more -- (Enter to continue, q to stop) ");
    char line[8];
    if (fgets(line, sizeof(line), stdin) == NULL) {
        return false;
    }
    return !(line[0] == 'q' || line[0] == 'Q');
}

bool nmo_debug_parse_u32(const char *text, uint32_t *out) {
    return nmo_tool_parse_u32_dec(text, out);
}

bool nmo_debug_parse_size(const char *text, size_t *out) {
    return nmo_tool_parse_size_dec(text, out);
}

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

static bool regex_class_match(const char *pattern, size_t len, char c, bool icase) {
    bool negate = false;
    size_t i = 1;
    if (i < len && pattern[i] == '^') {
        negate = true;
        i++;
    }

    bool matched = false;
    char target = regex_fold_char(c, icase);

    while (i < len && pattern[i] != ']') {
        char start = pattern[i];
        if (start == '\\' && i + 1 < len) {
            start = pattern[i + 1];
            i += 2;
        } else {
            i++;
        }

        if (i + 1 < len && pattern[i] == '-' && pattern[i + 1] != ']') {
            i++;
            char end = pattern[i];
            if (end == '\\' && i + 1 < len) {
                end = pattern[i + 1];
                i += 2;
            } else {
                i++;
            }
            char start_f = regex_fold_char(start, icase);
            char end_f = regex_fold_char(end, icase);
            if (start_f > end_f) {
                char tmp = start_f;
                start_f = end_f;
                end_f = tmp;
            }
            if (target >= start_f && target <= end_f) {
                matched = true;
            }
        } else {
            char start_f = regex_fold_char(start, icase);
            if (target == start_f) {
                matched = true;
            }
        }
    }

    return negate ? !matched : matched;
}

static bool regex_atom_match(const char *pattern, size_t len, char c, bool icase) {
    if (len == 0 || c == '\0') {
        return false;
    }
    if (pattern[0] == '.') {
        return true;
    }
    if (pattern[0] == '\\') {
        if (len < 2) {
            return false;
        }
        return regex_fold_char(pattern[1], icase) == regex_fold_char(c, icase);
    }
    if (pattern[0] == '[') {
        return regex_class_match(pattern, len, c, icase);
    }
    return regex_fold_char(pattern[0], icase) == regex_fold_char(c, icase);
}

static bool regex_match_here(const char *pattern, const char *text, bool icase) {
    if (!pattern) {
        return false;
    }
    if (pattern[0] == '\0') {
        return true;
    }
    if (pattern[0] == '$' && pattern[1] == '\0') {
        return *text == '\0';
    }

    size_t atom_len = regex_atom_length(pattern);
    if (atom_len == 0) {
        return false;
    }

    char quant = pattern[atom_len];
    if (quant == '*' || quant == '+' || quant == '?') {
        const char *t = text;
        size_t min = 0;
        if (quant == '+') {
            if (!regex_atom_match(pattern, atom_len, *t, icase)) {
                return false;
            }
            t++;
            min = 1;
        }

        while (regex_atom_match(pattern, atom_len, *t, icase)) {
            t++;
        }

        if (quant == '?') {
            if (regex_atom_match(pattern, atom_len, *text, icase)) {
                if (regex_match_here(pattern + atom_len + 1, text + 1, icase)) {
                    return true;
                }
            }
            return regex_match_here(pattern + atom_len + 1, text, icase);
        }

        while ((size_t)(t - text) >= min) {
            if (regex_match_here(pattern + atom_len + 1, t, icase)) {
                return true;
            }
            t--;
        }
        return false;
    }

    if (regex_atom_match(pattern, atom_len, *text, icase)) {
        return regex_match_here(pattern + atom_len, text + 1, icase);
    }

    return false;
}

bool nmo_debug_regex_matches(const char *text, const char *pattern, bool icase) {
    if (!text || !pattern) {
        return false;
    }

    const char *pat = pattern;
    if (*pat == '^') {
        return regex_match_here(pat + 1, text, icase);
    }

    do {
        if (regex_match_here(pat, text, icase)) {
            return true;
        }
    } while (*text++ != '\0');

    return false;
}

void nmo_debug_json_write_string(FILE *out, const char *value) {
    if (!out) {
        return;
    }
    if (!value) {
        fputs("null", out);
        return;
    }

    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        switch (*p) {
            case '"': fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\b': fputs("\\b", out); break;
            case '\f': fputs("\\f", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default:
                if (*p < 0x20) {
                    fprintf(out, "\\u%04x", (unsigned int)*p);
                } else {
                    fputc(*p, out);
                }
                break;
        }
    }
    fputc('"', out);
}

int nmo_debug_resolve_object_index(nmo_debug_context_t *dbg,
                                  const char *selector,
                                  size_t *out_index,
                                  bool allow_default) {
    if (!out_index) {
        return -1;
    }

    if (!selector || selector[0] == '\0') {
        if (allow_default && dbg->has_selection) {
            *out_index = dbg->selected_index;
            return 0;
        }
        fprintf(stderr, "No object selected. Use 'select <index>' or provide a selector.\n");
        return -1;
    }

    const char *value = selector;
    bool by_id = false;
    bool by_index = false;
    bool by_name = false;

    if (strncmp(selector, "id:", 3) == 0) {
        by_id = true;
        value = selector + 3;
    } else if (strncmp(selector, "idx:", 4) == 0 || strncmp(selector, "index:", 6) == 0) {
        by_index = true;
        value = selector + (selector[3] == ':' ? 4 : 6);
    } else if (strncmp(selector, "name:", 5) == 0) {
        by_name = true;
        value = selector + 5;
    }

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    nmo_debug_get_objects(dbg, &objects, &object_count);

    if (by_name) {
        bool use_regex = false;
        const char *pattern = value;
        size_t pattern_len = strlen(value);
        if (pattern_len >= 2 && value[0] == '/' && value[pattern_len - 1] == '/') {
            use_regex = true;
            pattern = value + 1;
            pattern_len -= 2;
        }

        size_t found = 0;
        size_t last = 0;
        for (size_t i = 0; i < object_count; ++i) {
            const char *name = nmo_object_get_name(objects[i]);
            if (!name) {
                continue;
            }

            bool matches = false;
            if (use_regex) {
                if (pattern_len == 0 || pattern_len > 1024) {
                    fprintf(stderr, "Invalid regex pattern length.\n");
                    return -1;
                }

                char *pattern_buf = (char *)malloc(pattern_len + 1);
                if (!pattern_buf) {
                    fprintf(stderr, "Out of memory building regex pattern.\n");
                    return -1;
                }
                memcpy(pattern_buf, pattern, pattern_len);
                pattern_buf[pattern_len] = '\0';
                matches = nmo_debug_regex_matches(name, pattern_buf, dbg->regex_icase);
                free(pattern_buf);
            } else {
                matches = strstr(name, value) != NULL;
            }

            if (matches) {
                nmo_debug_print_object_summary(dbg, i, objects[i]);
                found++;
                last = i;
                if (!nmo_debug_paginate_if_needed(dbg, found)) {
                    break;
                }
            }
        }

        if (found == 1) {
            *out_index = last;
            return 0;
        }
        if (found == 0) {
            fprintf(stderr, "No objects matched name '%s'.\n", value);
        } else {
            fprintf(stderr, "Multiple matches (%zu). Use idx:<index> or id:<id>.\n", found);
        }
        return -1;
    }

    if (by_id) {
        uint32_t id = 0;
        if (!nmo_debug_parse_u32(value, &id)) {
            fprintf(stderr, "Invalid id: %s\n", value);
            return -1;
        }
        for (size_t i = 0; i < object_count; ++i) {
            if (nmo_object_get_id(objects[i]) == id) {
                *out_index = i;
                return 0;
            }
        }
        fprintf(stderr, "Object id %u not found.\n", id);
        return -1;
    }

    if (by_index || !by_id) {
        size_t index = 0;
        if (!nmo_debug_parse_size(value, &index)) {
            fprintf(stderr, "Invalid index: %s\n", value);
            return -1;
        }
        if (index >= object_count) {
            fprintf(stderr, "Index %zu out of range (0-%zu).\n", index, object_count ? object_count - 1 : 0);
            return -1;
        }
        *out_index = index;
        return 0;
    }

    return -1;
}
