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
 * Returns def index, or (size_t)-1 if not found.
 */
static size_t find_def(const char *token,
                       const nmo_opt_def_t *defs, size_t def_count)
{
    for (size_t i = 0; i < def_count; i++) {
        if (strcmp(token, defs[i].long_name) == 0) return i;
        if (defs[i].short_name && strcmp(token, defs[i].short_name) == 0) return i;
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
            result->pos_args[result->pos_count++] = arg;
            continue;
        }

        /* Look up option */
        size_t idx = find_def(arg, defs, def_count);
        if (idx == (size_t)-1) {
            fprintf(stderr, "Error: Unknown option '%s'\n", arg);
            return -1;
        }

        result->vals[idx].present = true;

        switch (defs[idx].type) {
        case NMO_OPT_FLAG:
            result->vals[idx].val.flag = true;
            break;

        case NMO_OPT_STRING:
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: %s requires a value\n", arg);
                return -1;
            }
            result->vals[idx].val.str = argv[++i];
            break;

        case NMO_OPT_UINT: {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: %s requires a value\n", arg);
                return -1;
            }
            uint32_t v;
            if (!nmo_tool_parse_u32(argv[++i], &v)) {
                fprintf(stderr, "Error: Invalid value for %s: '%s'\n",
                        defs[idx].long_name, argv[i]);
                return -1;
            }
            result->vals[idx].val.u = v;
            break;
        }

        case NMO_OPT_INT: {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: %s requires a value\n", arg);
                return -1;
            }
            const char *val_str = argv[++i];
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
