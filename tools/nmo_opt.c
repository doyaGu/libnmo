/**
 * @file nmo_opt.c
 * @brief Declarative option parser implementation
 */

#include "nmo_opt.h"
#include "nmo_tool_common.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Find the definition index matching an argv token.
 * Supports both "--key value" and "--key=value" forms.
 * If the token contains '=' after a long option name, *eq_value is set
 * to point at the character after '='; otherwise *eq_value is NULL.
 * Returns def index, or (size_t)-1 if not found.
 */
static size_t find_def(const char *token,
                       const nmo_opt_def_t *defs, size_t def_count,
                       const char **eq_value)
{
    *eq_value = NULL;
    for (size_t i = 0; i < def_count; i++) {
        if (strcmp(token, defs[i].long_name) == 0) return i;
        if (defs[i].short_name && strcmp(token, defs[i].short_name) == 0) return i;
        /* Support --key=value form for long options */
        size_t llen = strlen(defs[i].long_name);
        if (strncmp(token, defs[i].long_name, llen) == 0 && token[llen] == '=') {
            *eq_value = token + llen + 1;
            return i;
        }
    }
    return (size_t)-1;
}

int nmo_opt_parse(int argc, char **argv,
                  const nmo_opt_def_t *defs, size_t def_count,
                  nmo_opt_result_t *result)
{
    /* Initialize output */
    for (size_t i = 0; i < def_count; i++) {
        memset(&result->vals[i], 0, sizeof(nmo_opt_val_t));
    }
    result->pos_count = 0;

    bool stop_options = false; /* After --, everything is positional */

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        /* -- stops option parsing */
        if (!stop_options && strcmp(arg, "--") == 0) {
            stop_options = true;
            continue;
        }

        /* Positional argument */
        if (stop_options || arg[0] != '-') {
            if (result->pos_capacity > 0 && result->pos_count >= result->pos_capacity) {
                fprintf(stderr, "Error: Too many positional arguments\n");
                return -1;
            }
            result->pos_args[result->pos_count++] = arg;
            continue;
        }

        /* Look up option */
        const char *eq_value = NULL;
        size_t idx = find_def(arg, defs, def_count, &eq_value);
        if (idx == (size_t)-1) {
            fprintf(stderr, "Error: Unknown option '%s'\n", arg);
            return -1;
        }

        result->vals[idx].present = true;

        switch (defs[idx].type) {
        case NMO_OPT_FLAG:
            if (eq_value) {
                fprintf(stderr, "Error: %s does not accept a value\n", defs[idx].long_name);
                return -1;
            }
            result->vals[idx].val.flag = true;
            break;

        case NMO_OPT_STRING: {
            const char *val_str;
            if (eq_value) {
                val_str = eq_value;
            } else if (i + 1 >= argc) {
                fprintf(stderr, "Error: %s requires a value\n", arg);
                return -1;
            } else {
                val_str = argv[++i];
            }
            result->vals[idx].val.str = val_str;
            break;
        }

        case NMO_OPT_UINT: {
            const char *val_str;
            if (eq_value) {
                val_str = eq_value;
            } else if (i + 1 >= argc) {
                fprintf(stderr, "Error: %s requires a value\n", arg);
                return -1;
            } else {
                val_str = argv[++i];
            }
            uint32_t v;
            if (!nmo_tool_parse_u32(val_str, &v)) {
                fprintf(stderr, "Error: Invalid value for %s: '%s'\n",
                        defs[idx].long_name, val_str);
                return -1;
            }
            result->vals[idx].val.u = v;
            break;
        }

        case NMO_OPT_INT: {
            const char *val_str;
            if (eq_value) {
                val_str = eq_value;
            } else if (i + 1 >= argc) {
                fprintf(stderr, "Error: %s requires a value\n", arg);
                return -1;
            } else {
                val_str = argv[++i];
            }
            char *end = NULL;
            errno = 0;
            long lv = strtol(val_str, &end, 0);
            if (end == val_str || *end != '\0' || errno == ERANGE ||
                lv < INT32_MIN || lv > INT32_MAX) {
                fprintf(stderr, "Error: Invalid value for %s: '%s'\n",
                        defs[idx].long_name, val_str);
                return -1;
            }
            result->vals[idx].val.i = (int32_t)lv;
            break;
        }
        }
    }

    return 0;
}

void nmo_opt_print_help(FILE *out, const char *usage_line,
                        const nmo_opt_def_t *defs, size_t def_count)
{
    fprintf(out, "Usage: %s\n\nOptions:\n", usage_line);

    for (size_t i = 0; i < def_count; i++) {
        if (defs[i].short_name) {
            fprintf(out, "  %s, %-20s %s\n",
                    defs[i].short_name, defs[i].long_name,
                    defs[i].help ? defs[i].help : "");
        } else {
            fprintf(out, "      %-20s %s\n",
                    defs[i].long_name,
                    defs[i].help ? defs[i].help : "");
        }
    }
}
