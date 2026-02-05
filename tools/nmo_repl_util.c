#include "nmo_repl_util.h"

#include "nmo_tool_common.h"

#include "app/nmo_context.h"
#include "type/nmo_type_system.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *nmo_repl_basename(const char *path) {
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

void nmo_repl_print_prompt(const nmo_repl_context_t *repl) {
    const char *file_label = "";
    if (repl && repl->filename && repl->filename[0] != '\0') {
        file_label = nmo_repl_basename(repl->filename);
    }

    if (repl && repl->has_selection) {
        size_t object_count = 0;
        nmo_object_t **objects = NULL;
        nmo_session_get_objects(repl->session, &objects, &object_count);
        if (repl->selected_index < object_count) {
            nmo_object_id_t id = nmo_object_get_id(objects[repl->selected_index]);
            if (file_label[0]) {
                printf("nmo(repl:%s idx=%zu id=%u)> ", file_label, repl->selected_index, id);
            } else {
                printf("nmo(repl idx=%zu id=%u)> ", repl->selected_index, id);
            }
            fflush(stdout);
            return;
        }
    }

    if (file_label[0]) {
        printf("nmo(repl:%s)> ", file_label);
    } else {
        printf("nmo(repl)> ");
    }
    fflush(stdout);
}

int nmo_repl_parse_command(char *line, char **argv, int max_args) {
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

void nmo_repl_get_objects(nmo_repl_context_t *repl, nmo_object_t ***objects, size_t *count) {
    nmo_session_get_objects(repl->session, objects, count);
}

const char *nmo_repl_class_name_from_id(const nmo_repl_context_t *repl,
                                        nmo_class_id_t class_id,
                                        char *buffer,
                                        size_t buffer_size) {
    const char *name = NULL;
    if (repl && repl->ctx) {
        nmo_type_registry_t *registry = nmo_context_get_type_registry(repl->ctx);
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

bool nmo_repl_class_id_from_name(const nmo_repl_context_t *repl, const char *name, nmo_class_id_t *out_class_id) {
    if (!out_class_id) {
        return false;
    }
    *out_class_id = 0;

    if (!repl || !repl->ctx || !name || !name[0]) {
        return false;
    }

    nmo_type_registry_t *registry = nmo_context_get_type_registry(repl->ctx);
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

void nmo_repl_print_object_summary(const nmo_repl_context_t *repl, size_t index, nmo_object_t *obj) {
    nmo_object_id_t obj_id = nmo_object_get_id(obj);
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const char *name = nmo_object_get_name(obj);

    char class_buf[64];
    const char *class_name = nmo_repl_class_name_from_id(repl, class_id, class_buf, sizeof(class_buf));

    printf("  [%3zu] ID=%-5u Class=%-3d %-24s %s\n",
           index,
           obj_id,
           class_id,
           class_name,
           name ? name : "(unnamed)");
}

void nmo_repl_print_object_summary_marked(const nmo_repl_context_t *repl,
                                          size_t index,
                                          nmo_object_t *obj,
                                          bool selected) {
    nmo_object_id_t obj_id = nmo_object_get_id(obj);
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const char *name = nmo_object_get_name(obj);

    char class_buf[64];
    const char *class_name = nmo_repl_class_name_from_id(repl, class_id, class_buf, sizeof(class_buf));

    printf("%c [%3zu] ID=%-5u Class=%-3d %-24s %s\n",
           selected ? '>' : ' ',
           index,
           obj_id,
           class_id,
           class_name,
           name ? name : "(unnamed)");
}

bool nmo_repl_paginate_if_needed(nmo_repl_context_t *repl, size_t printed) {
    if (!repl || repl->page_size == 0 || printed == 0) {
        return true;
    }

    if ((printed % repl->page_size) != 0) {
        return true;
    }

    printf("-- more -- (Enter to continue, q to stop) ");
    char line[8];
    if (fgets(line, sizeof(line), stdin) == NULL) {
        return false;
    }
    return !(line[0] == 'q' || line[0] == 'Q');
}

bool nmo_repl_parse_u32(const char *text, uint32_t *out) {
    return nmo_tool_parse_u32_dec(text, out);
}

bool nmo_repl_parse_size(const char *text, size_t *out) {
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

static bool regex_match_here(const char *text, const char *pattern, bool icase) {
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
                atom_match = regex_fold_char(pattern[1], icase) == regex_fold_char(*t, icase);
            } else {
                atom_match = regex_fold_char(pattern[0], icase) == regex_fold_char(*t, icase);
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
        if (regex_fold_char(pattern[1], icase) != regex_fold_char(*text, icase)) {
            return false;
        }
        return regex_match_here(text + 1, next, icase);
    }

    if (regex_fold_char(pattern[0], icase) != regex_fold_char(*text, icase)) {
        return false;
    }

    return regex_match_here(text + 1, next, icase);
}

bool nmo_repl_regex_matches(const char *text, const char *pattern, bool icase) {
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

void nmo_repl_json_write_string(FILE *out, const char *value) {
    if (!out) {
        return;
    }
    if (!value) {
        fputs("null", out);
        return;
    }

    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        unsigned char c = *p;
        switch (c) {
        case '"':
            fputs("\\\"", out);
            break;
        case '\\':
            fputs("\\\\", out);
            break;
        case '\b':
            fputs("\\b", out);
            break;
        case '\f':
            fputs("\\f", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        default:
            if (c < 0x20 || c >= 0x7F) {
                fprintf(out, "\\u%04X", (unsigned int)c);
            } else {
                fputc((int)c, out);
            }
            break;
        }
    }
    fputc('"', out);
}

static bool parse_selector_index(const char *selector, size_t *out_index) {
    if (!selector || !out_index) {
        return false;
    }

    if (!isdigit((unsigned char)selector[0])) {
        return false;
    }

    size_t index = 0;
    if (!nmo_tool_parse_size_dec(selector, &index)) {
        return false;
    }

    *out_index = index;
    return true;
}

static bool selector_is_id(const char *selector, uint32_t *out_id) {
    if (!selector || !out_id) {
        return false;
    }
    if (strncmp(selector, "id:", 3) != 0) {
        return false;
    }
    return nmo_repl_parse_u32(selector + 3, out_id);
}

static bool selector_is_name(const char *selector, const char **out_name) {
    if (!selector || !out_name) {
        return false;
    }
    if (strncmp(selector, "name:", 5) != 0) {
        return false;
    }
    *out_name = selector + 5;
    return true;
}

int nmo_repl_resolve_object_index(nmo_repl_context_t *repl,
                                  const char *selector,
                                  size_t *out_index,
                                  bool allow_default) {
    if (!repl || !out_index) {
        return -1;
    }

    if (!selector || selector[0] == '\0') {
        if (allow_default && repl->has_selection) {
            *out_index = repl->selected_index;
            return 0;
        }
        fprintf(stderr, "Error: No selector provided\n");
        return -1;
    }

    if (parse_selector_index(selector, out_index)) {
        return 0;
    }

    uint32_t id = 0;
    if (selector_is_id(selector, &id)) {
        size_t object_count = 0;
        nmo_object_t **objects = NULL;
        nmo_repl_get_objects(repl, &objects, &object_count);
        for (size_t i = 0; i < object_count; ++i) {
            if (nmo_object_get_id(objects[i]) == id) {
                *out_index = i;
                return 0;
            }
        }
        fprintf(stderr, "Error: No object with id %u\n", id);
        return -1;
    }

    const char *name = NULL;
    if (selector_is_name(selector, &name)) {
        size_t object_count = 0;
        nmo_object_t **objects = NULL;
        nmo_repl_get_objects(repl, &objects, &object_count);

        if (name && name[0] == '/' && name[strlen(name) - 1] == '/') {
            char pattern[256];
            size_t len = strlen(name);
            size_t copy_len = len >= 2 ? len - 2 : 0;
            if (copy_len >= sizeof(pattern)) {
                copy_len = sizeof(pattern) - 1;
            }
            memcpy(pattern, name + 1, copy_len);
            pattern[copy_len] = '\0';

            for (size_t i = 0; i < object_count; ++i) {
                const char *obj_name = nmo_object_get_name(objects[i]);
                if (obj_name && nmo_repl_regex_matches(obj_name, pattern, repl->regex_icase)) {
                    *out_index = i;
                    return 0;
                }
            }
            fprintf(stderr, "Error: No object name matches /%s/\n", pattern);
            return -1;
        }

        for (size_t i = 0; i < object_count; ++i) {
            const char *obj_name = nmo_object_get_name(objects[i]);
            if (obj_name && strstr(obj_name, name)) {
                *out_index = i;
                return 0;
            }
        }
        fprintf(stderr, "Error: No object name contains '%s'\n", name);
        return -1;
    }

    fprintf(stderr, "Error: Unsupported selector '%s'\n", selector);
    return -1;
}
