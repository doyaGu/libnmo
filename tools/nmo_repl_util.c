#include "nmo_repl_util.h"

#include "nmo_cmd_ctx.h"
#include "nmo_cmd_core.h"
#include "nmo_tool_common.h"

#include "core/nmo_path.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void nmo_repl_print_prompt(const nmo_repl_context_t *repl) {
    const char *file_label = "";
    if (repl && repl->filename && repl->filename[0] != '\0') {
        file_label = nmo_path_basename(repl->filename);
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

void nmo_repl_print_object_summary(const nmo_repl_context_t *repl, size_t index, nmo_object_t *obj) {
    nmo_object_id_t obj_id = nmo_object_get_id(obj);
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const char *name = nmo_object_get_name(obj);

    nmo_cmd_ctx_t c;
    nmo_cmd_ctx_init_from_repl(&c, repl->ctx, repl->session, false);

    char class_buf[64];
    const char *class_name = nmo_core_class_name_or(&c, class_id, class_buf, sizeof(class_buf));

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

    nmo_cmd_ctx_t c;
    nmo_cmd_ctx_init_from_repl(&c, repl->ctx, repl->session, false);

    char class_buf[64];
    const char *class_name = nmo_core_class_name_or(&c, class_id, class_buf, sizeof(class_buf));

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
                if (obj_name && nmo_core_regex_match(obj_name, pattern, repl->regex_icase)) {
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
