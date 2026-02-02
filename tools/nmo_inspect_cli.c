#include "nmo_inspect_cli.h"

#include "nmo_inspect_util.h"
#include "nmo_tool_common.h"

#include "core/nmo_guid.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char *trim_start(const char *str) {
    while (*str && isspace((unsigned char)*str)) {
        ++str;
    }
    return str;
}

static size_t trim_end(const char *start, const char *end) {
    size_t len = (size_t)(end - start);
    while (len > 0 && isspace((unsigned char)start[len - 1])) {
        --len;
    }
    return len;
}

static int parse_u32_token(const char *text, uint32_t *out_value) {
    if (!text || !*text || !out_value) {
        return -1;
    }
    return nmo_tool_parse_u32(text, out_value) ? 0 : -1;
}

static int parse_size_token(const char *text, size_t *out_value) {
    if (!text || !*text || !out_value) {
        return -1;
    }
    return nmo_tool_parse_size(text, out_value) ? 0 : -1;
}

static bool append_u32(uint32_t **list, size_t *count, size_t *capacity, uint32_t value) {
    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 4 : (*capacity * 2);
        uint32_t *new_list = (uint32_t *)realloc(*list, new_capacity * sizeof(uint32_t));
        if (!new_list) {
            return false;
        }
        *list = new_list;
        *capacity = new_capacity;
    }
    (*list)[(*count)++] = value;
    return true;
}

static bool append_size(size_t **list, size_t *count, size_t *capacity, size_t value) {
    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 4 : (*capacity * 2);
        size_t *new_list = (size_t *)realloc(*list, new_capacity * sizeof(size_t));
        if (!new_list) {
            return false;
        }
        *list = new_list;
        *capacity = new_capacity;
    }
    (*list)[(*count)++] = value;
    return true;
}

static bool append_object_id(nmo_object_id_t **list, size_t *count, size_t *capacity, nmo_object_id_t value) {
    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 4 : (*capacity * 2);
        nmo_object_id_t *new_list = (nmo_object_id_t *)realloc(*list, new_capacity * sizeof(nmo_object_id_t));
        if (!new_list) {
            return false;
        }
        *list = new_list;
        *capacity = new_capacity;
    }
    (*list)[(*count)++] = value;
    return true;
}

static int parse_object_id_list(inspect_filters_t *filters, const char *value) {
    const char *ptr = value;
    while (ptr && *ptr) {
        ptr = trim_start(ptr);
        if (*ptr == '\0') {
            break;
        }
        const char *end = strchr(ptr, ',');
        if (!end) {
            end = ptr + strlen(ptr);
        }
        size_t len = trim_end(ptr, end);
        if (len > 0) {
            char buffer[64];
            if (len >= sizeof(buffer)) {
                len = sizeof(buffer) - 1;
            }
            memcpy(buffer, ptr, len);
            buffer[len] = '\0';
            uint32_t parsed = 0;
            if (parse_u32_token(buffer, &parsed) != 0) {
                return -1;
            }
            if (!append_object_id(&filters->object_ids, &filters->object_id_count, &filters->object_id_capacity, (nmo_object_id_t)parsed)) {
                return -2;
            }
        }
        ptr = (*end == ',') ? end + 1 : end;
    }
    return 0;
}

static int parse_behavior_id_list(inspect_filters_t *filters, const char *value) {
    const char *ptr = value;
    while (ptr && *ptr) {
        ptr = trim_start(ptr);
        if (*ptr == '\0') {
            break;
        }
        const char *end = strchr(ptr, ',');
        if (!end) {
            end = ptr + strlen(ptr);
        }
        size_t len = trim_end(ptr, end);
        if (len > 0) {
            char buffer[64];
            if (len >= sizeof(buffer)) {
                len = sizeof(buffer) - 1;
            }
            memcpy(buffer, ptr, len);
            buffer[len] = '\0';
            uint32_t parsed = 0;
            if (parse_u32_token(buffer, &parsed) != 0) {
                return -1;
            }
            if (!append_object_id(&filters->behavior_ids, &filters->behavior_id_count, &filters->behavior_id_capacity, (nmo_object_id_t)parsed)) {
                return -2;
            }
        }
        ptr = (*end == ',') ? end + 1 : end;
    }
    return 0;
}

static int parse_chunk_id_list(inspect_filters_t *filters, const char *value) {
    const char *ptr = value;
    while (ptr && *ptr) {
        ptr = trim_start(ptr);
        if (*ptr == '\0') {
            break;
        }
        const char *end = strchr(ptr, ',');
        if (!end) {
            end = ptr + strlen(ptr);
        }
        size_t len = trim_end(ptr, end);
        if (len > 0) {
            char buffer[64];
            if (len >= sizeof(buffer)) {
                len = sizeof(buffer) - 1;
            }
            memcpy(buffer, ptr, len);
            buffer[len] = '\0';
            uint32_t parsed = 0;
            if (parse_u32_token(buffer, &parsed) != 0) {
                return -1;
            }
            if (!append_u32(&filters->chunk_ids, &filters->chunk_id_count, &filters->chunk_id_capacity, parsed)) {
                return -2;
            }
        }
        ptr = (*end == ',') ? end + 1 : end;
    }
    return 0;
}

static int parse_chunk_index_list(inspect_filters_t *filters, const char *value) {
    const char *ptr = value;
    while (ptr && *ptr) {
        ptr = trim_start(ptr);
        if (*ptr == '\0') {
            break;
        }
        const char *end = strchr(ptr, ',');
        if (!end) {
            end = ptr + strlen(ptr);
        }
        size_t len = trim_end(ptr, end);
        if (len > 0) {
            char buffer[64];
            if (len >= sizeof(buffer)) {
                len = sizeof(buffer) - 1;
            }
            memcpy(buffer, ptr, len);
            buffer[len] = '\0';
            size_t parsed = 0;
            if (parse_size_token(buffer, &parsed) != 0) {
                return -1;
            }
            if (!append_size(&filters->chunk_indexes, &filters->chunk_index_count, &filters->chunk_index_capacity, parsed)) {
                return -2;
            }
        }
        ptr = (*end == ',') ? end + 1 : end;
    }
    return 0;
}

void nmo_inspect_filters_init(inspect_filters_t *filters) {
    if (!filters) {
        return;
    }
    memset(filters, 0, sizeof(*filters));
}

void nmo_inspect_filters_free(inspect_filters_t *filters) {
    if (!filters) {
        return;
    }
    free(filters->object_ids);
    free(filters->chunk_ids);
    free(filters->chunk_indexes);
    free(filters->behavior_ids);
    free(filters->class_name);
    free(filters->name_pattern);
    free(filters->scene_name);
    nmo_inspect_filters_init(filters);
}

void nmo_inspect_options_init(inspect_options_t *opts) {
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->format = INSPECT_FORMAT_TEXT;
    opts->color_mode = COLOR_AUTO;
    opts->truncate_length = 80;
    opts->max_rows = 0;
    nmo_inspect_filters_init(&opts->filters);
}

void nmo_inspect_options_free(inspect_options_t *opts) {
    if (!opts) {
        return;
    }
    nmo_inspect_filters_free(&opts->filters);
}

void nmo_inspect_print_usage(void) {
    printf("Usage: nmo inspect [options] <file>\n\n");
    printf("General options:\n");
    printf("  -h, --help                 Show this help\n");
    printf("  -V, --version              Show version information\n");
    printf("  -v, --verbose              Increase logging (repeatable)\n");
    printf("                             (default shows errors only; -v adds warnings/info, -vv adds debug)\n");
    printf("      --color[=mode]         Color output auto|always|never\n");
    printf("  -o, --output <path>        Write report to file\n");
    printf("  -F, --format <fmt>         text|json|json-pretty|yaml\n");
    printf("      --fail-on-warning      Exit with error if warnings exist\n");
    printf("      --strict               Treat validation issues as fatal\n");
    printf("      --no-pager             Disable pager even when interactive\n");
    printf("      --locale <name>        Override locale for formatting\n");
    printf("      --encoding <name>      Override text encoding (default UTF-8)\n");
    printf("\nModes (select what to display):\n");
    printf("      --summary              Show summary (default)\n");
    printf("      --header               Show file header information\n");
    printf("      --stats                Show detailed file statistics\n");
    printf("      --finish-stats         Show post-load indexing/reference stats\n");
    printf("      --plugins              Show plugin dependency diagnostics\n");
    printf("      --chunks               Show chunk table\n");
    printf("      --chunk-tree           Show chunk hierarchy tree\n");
    printf("      --objects              Show object list (default)\n");
    printf("      --hierarchy            Show object hierarchy\n");
    printf("      --managers             Show manager chunk summary\n");
    printf("      --behaviors            Show behavior/script objects\n");
    printf("      --parameters           Show parameter objects\n");
    printf("      --resources            Show included resources\n");
    printf("      --warnings             Show collected warnings (default)\n");
    printf("      --all                  summary+header+stats+finish-stats+plugins+objects+hierarchy+warnings\n");
    printf("      --no-summary           Disable summary section\n");
    printf("      --no-header            Disable header section\n");
    printf("      --no-warnings          Disable warnings section\n");
    printf("\nFiltering:\n");
    printf("      --object-id <list>     Only include specified object IDs\n");
    printf("      --class <name>         Only include class and descendants\n");
    printf("      --name <pattern>       Filter objects by name (supports * ?)\n");
    printf("      --manager <guid>       Filter CKParameter objects by manager GUID (MODE_MANAGER)\n");
    printf("      --root <object-id>     Start hierarchy at object ID\n");
    printf("      --scene <name>         Restrict to CKScene/CKLevel by name\n");
    printf("      --object-behaviors     Alias for --behaviors\n");
    printf("      --behavior-id <list>   Filter behaviors by object ID\n");
    printf("      --chunk-id <list>      Filter chunks by chunk class ID\n");
    printf("      --chunk-index <list>   Filter chunks by index\n");
    printf("      --chunk-depth <n>      Limit chunk tree depth\n");
    printf("      --include-hidden       Include hidden/editor-only objects\n");
    printf("\nFormatting controls:\n");
    printf("      --compact              Compact single-line text output\n");
    printf("      --max-rows <n>         Limit rows per section (0=all)\n");
    printf("      --truncate <n>         Truncate long strings (default 80)\n");
    printf("      --show-offsets         Attempt to show chunk offsets\n");
    printf("      --show-size            Include size columns\n");
    printf("      --show-guids           Show GUIDs in listings\n");
    printf("\nExamples:\n");
    printf("  nmo inspect scene.cmo\n");
    printf("  nmo inspect --chunks --chunk-tree --show-size scene.cmo\n");
    printf("  nmo inspect --objects --class CK3dEntity scene.cmo\n");
    printf("  nmo inspect --all --format json scene.cmo > report.json\n");
}

static int parse_long_option(int argc, char **argv, int *index, inspect_options_t *opts, const char *option_text) {
    const char *value = NULL;
    char name_buffer[128];
    const char *name = option_text;
    const char *eq = strchr(option_text, '=');
    if (eq) {
        size_t len = (size_t)(eq - option_text);
        if (len >= sizeof(name_buffer)) {
            nmo_inspect_log(opts, LOG_ERROR, "Option name too long: --%s", option_text);
            return -1;
        }
        memcpy(name_buffer, option_text, len);
        name_buffer[len] = '\0';
        name = name_buffer;
        if (*(eq + 1) != '\0') {
            value = eq + 1;
        }
    }

    #define REQUIRE_VALUE() \
        do { \
            if (!value || !*value) { \
                if (*index + 1 >= argc) { \
                    nmo_inspect_log(opts, LOG_ERROR, "Option --%s requires a value", name); \
                    return -1; \
                } \
                value = argv[*index + 1]; \
                if (value[0] == '-' && value[1] != '\0') { \
                    nmo_inspect_log(opts, LOG_ERROR, "Option --%s requires a value", name); \
                    return -1; \
                } \
                value = argv[++(*index)]; \
            } \
        } while (0)

    #define OPTIONAL_VALUE(default_value) \
        do { \
            if (!value || !*value) { \
                if (*index + 1 < argc) { \
                    const char *next = argv[*index + 1]; \
                    if (!(next[0] == '-' && next[1] != '\0')) { \
                        value = argv[++(*index)]; \
                        break; \
                    } \
                } \
                value = (default_value); \
            } \
        } while (0)

    if (strcmp(name, "help") == 0) {
        opts->show_help = true;
    } else if (strcmp(name, "version") == 0) {
        opts->show_version = true;
    } else if (strcmp(name, "verbose") == 0) {
        opts->verbosity++;
    } else if (strncmp(name, "color", 5) == 0) {
        OPTIONAL_VALUE("always");
        if (strcmp(value, "auto") == 0) {
            opts->color_mode = COLOR_AUTO;
        } else if (strcmp(value, "always") == 0) {
            opts->color_mode = COLOR_ALWAYS;
        } else if (strcmp(value, "never") == 0) {
            opts->color_mode = COLOR_NEVER;
        } else {
            nmo_inspect_log(opts, LOG_ERROR, "Invalid color mode: %s", value);
            return -1;
        }
    } else if (strcmp(name, "output") == 0) {
        REQUIRE_VALUE();
        opts->output_path = value;
    } else if (strcmp(name, "format") == 0) {
        REQUIRE_VALUE();
        if (strcmp(value, "text") == 0) {
            opts->format = INSPECT_FORMAT_TEXT;
        } else if (strcmp(value, "json") == 0) {
            opts->format = INSPECT_FORMAT_JSON;
        } else if (strcmp(value, "json-pretty") == 0) {
            opts->format = INSPECT_FORMAT_JSON_PRETTY;
        } else if (strcmp(value, "yaml") == 0) {
            opts->format = INSPECT_FORMAT_YAML;
        } else {
            nmo_inspect_log(opts, LOG_ERROR, "Unknown format: %s", value);
            return -1;
        }
    } else if (strcmp(name, "fail-on-warning") == 0) {
        opts->fail_on_warning = true;
    } else if (strcmp(name, "strict") == 0) {
        opts->strict_mode = true;
    } else if (strcmp(name, "no-pager") == 0) {
        opts->no_pager = true;
    } else if (strcmp(name, "locale") == 0) {
        REQUIRE_VALUE();
        opts->locale = value;
    } else if (strcmp(name, "encoding") == 0) {
        REQUIRE_VALUE();
        opts->encoding = value;
    } else if (strcmp(name, "summary") == 0) {
        opts->modes.summary = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "header") == 0) {
        opts->modes.header = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "stats") == 0) {
        opts->modes.stats = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "finish-stats") == 0) {
        opts->modes.finish_stats = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "plugins") == 0) {
        opts->modes.plugins = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "chunks") == 0) {
        opts->modes.chunks = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "chunk-tree") == 0) {
        opts->modes.chunk_tree = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "objects") == 0) {
        opts->modes.objects = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "hierarchy") == 0) {
        opts->modes.hierarchy = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "managers") == 0) {
        opts->modes.managers = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "behaviors") == 0 || strcmp(name, "object-behaviors") == 0) {
        opts->modes.behaviors = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "parameters") == 0) {
        opts->modes.parameters = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "resources") == 0) {
        opts->modes.resources = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "warnings") == 0) {
        opts->modes.warnings = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "all") == 0) {
        opts->modes.summary = true;
        opts->modes.header = true;
        opts->modes.stats = true;
        opts->modes.finish_stats = true;
        opts->modes.plugins = true;
        opts->modes.objects = true;
        opts->modes.hierarchy = true;
        opts->modes.warnings = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "no-summary") == 0) {
        opts->modes.suppress_summary = true;
    } else if (strcmp(name, "no-header") == 0) {
        opts->modes.suppress_header = true;
    } else if (strcmp(name, "no-warnings") == 0) {
        opts->modes.suppress_warnings = true;
    } else if (strcmp(name, "object-id") == 0) {
        REQUIRE_VALUE();
        if (parse_object_id_list(&opts->filters, value) != 0) {
            nmo_inspect_log(opts, LOG_ERROR, "Invalid object ID list");
            return -1;
        }
    } else if (strcmp(name, "behavior-id") == 0) {
        REQUIRE_VALUE();
        if (parse_behavior_id_list(&opts->filters, value) != 0) {
            nmo_inspect_log(opts, LOG_ERROR, "Invalid behavior ID list");
            return -1;
        }
    } else if (strcmp(name, "chunk-id") == 0) {
        REQUIRE_VALUE();
        if (parse_chunk_id_list(&opts->filters, value) != 0) {
            nmo_inspect_log(opts, LOG_ERROR, "Invalid chunk ID list");
            return -1;
        }
    } else if (strcmp(name, "chunk-index") == 0) {
        REQUIRE_VALUE();
        if (parse_chunk_index_list(&opts->filters, value) != 0) {
            nmo_inspect_log(opts, LOG_ERROR, "Invalid chunk index list");
            return -1;
        }
    } else if (strcmp(name, "chunk-depth") == 0) {
        REQUIRE_VALUE();
        size_t depth = 0;
        if (parse_size_token(value, &depth) != 0) {
            nmo_inspect_log(opts, LOG_ERROR, "Invalid chunk depth value");
            return -1;
        }
        opts->filters.chunk_depth_limit = depth;
    } else if (strcmp(name, "class") == 0) {
        REQUIRE_VALUE();
        free(opts->filters.class_name);
        opts->filters.class_name = nmo_tool_strdup(value);
        if (!opts->filters.class_name) {
            nmo_inspect_log(opts, LOG_ERROR, "Out of memory while parsing class filter");
            return -1;
        }
        opts->filters.has_class_filter = true;
    } else if (strcmp(name, "name") == 0) {
        REQUIRE_VALUE();
        free(opts->filters.name_pattern);
        opts->filters.name_pattern = nmo_tool_strdup(value);
        if (!opts->filters.name_pattern) {
            nmo_inspect_log(opts, LOG_ERROR, "Out of memory while parsing name filter");
            return -1;
        }
    } else if (strcmp(name, "manager") == 0) {
        REQUIRE_VALUE();
        nmo_guid_t parsed = nmo_guid_parse(value);
        if (nmo_guid_is_null(parsed) && !nmo_tool_streq_ci(value, "{00000000-00000000}")) {
            nmo_inspect_log(opts, LOG_ERROR, "Invalid manager GUID: %s", value);
            return -1;
        }
        opts->filters.manager_guid = parsed;
        opts->filters.has_manager_guid = true;
    } else if (strcmp(name, "root") == 0) {
        REQUIRE_VALUE();
        uint32_t id = 0;
        if (parse_u32_token(value, &id) != 0) {
            nmo_inspect_log(opts, LOG_ERROR, "Invalid root object ID");
            return -1;
        }
        opts->filters.root_object_id = (nmo_object_id_t)id;
        opts->filters.root_specified = true;
    } else if (strcmp(name, "scene") == 0) {
        REQUIRE_VALUE();
        free(opts->filters.scene_name);
        opts->filters.scene_name = nmo_tool_strdup(value);
        if (!opts->filters.scene_name) {
            nmo_inspect_log(opts, LOG_ERROR, "Out of memory while parsing scene name");
            return -1;
        }
    } else if (strcmp(name, "include-hidden") == 0) {
        opts->filters.include_hidden = true;
    } else if (strcmp(name, "compact") == 0) {
        opts->compact_output = true;
    } else if (strcmp(name, "max-rows") == 0) {
        REQUIRE_VALUE();
        size_t rows = 0;
        if (parse_size_token(value, &rows) != 0) {
            nmo_inspect_log(opts, LOG_ERROR, "Invalid max rows value");
            return -1;
        }
        opts->max_rows = rows;
    } else if (strcmp(name, "truncate") == 0) {
        REQUIRE_VALUE();
        size_t length = 0;
        if (parse_size_token(value, &length) != 0) {
            nmo_inspect_log(opts, LOG_ERROR, "Invalid truncate length");
            return -1;
        }
        opts->truncate_length = length;
    } else if (strcmp(name, "show-offsets") == 0) {
        opts->show_offsets = true;
    } else if (strcmp(name, "show-size") == 0) {
        opts->show_size = true;
    } else if (strcmp(name, "show-guids") == 0) {
        opts->show_guids = true;
    } else {
        nmo_inspect_log(opts, LOG_ERROR, "Unknown option --%s", name);
        return -1;
    }

    return 0;

    #undef REQUIRE_VALUE
    #undef OPTIONAL_VALUE
}

static int parse_short_options(int argc, char **argv, int *index, inspect_options_t *opts) {
    const char *arg = argv[*index];
    for (size_t i = 1; arg[i]; ++i) {
        char opt = arg[i];
        switch (opt) {
            case 'h':
                opts->show_help = true;
                break;
            case 'V':
                opts->show_version = true;
                break;
            case 'v':
                opts->verbosity++;
                break;
            case 'o':
            case 'F': {
                const char *value = NULL;
                if (arg[i + 1]) {
                    value = &arg[i + 1];
                    i = strlen(arg) - 1;
                } else {
                    if (*index + 1 >= argc) {
                        nmo_inspect_log(opts, LOG_ERROR, "Option -%c requires a value", opt);
                        return -1;
                    }
                    value = argv[++(*index)];
                }
                if (opt == 'o') {
                    opts->output_path = value;
                } else {
                    if (strcmp(value, "text") == 0) {
                        opts->format = INSPECT_FORMAT_TEXT;
                    } else if (strcmp(value, "json") == 0) {
                        opts->format = INSPECT_FORMAT_JSON;
                    } else if (strcmp(value, "json-pretty") == 0) {
                        opts->format = INSPECT_FORMAT_JSON_PRETTY;
                    } else if (strcmp(value, "yaml") == 0) {
                        opts->format = INSPECT_FORMAT_YAML;
                    } else {
                        nmo_inspect_log(opts, LOG_ERROR, "Unknown format: %s", value);
                        return -1;
                    }
                }
                i = strlen(arg);
                break;
            }
            default:
                nmo_inspect_log(opts, LOG_ERROR, "Unknown option -%c", opt);
                return -1;
        }
    }
    return 0;
}

int nmo_inspect_parse_args(int argc, char **argv, inspect_options_t *opts) {
    if (argc < 2) {
        nmo_inspect_print_usage();
        return -1;
    }

    bool end_of_options = false;
    for (int i = 1; i < argc; ++i) {
        if (!end_of_options && argv[i][0] == '-') {
            if (strcmp(argv[i], "--") == 0) {
                end_of_options = true;
                continue;
            }
            if (argv[i][1] == '-') {
                if (parse_long_option(argc, argv, &i, opts, argv[i] + 2) != 0) {
                    return -1;
                }
            } else {
                if (parse_short_options(argc, argv, &i, opts) != 0) {
                    return -1;
                }
            }
        } else {
            if (opts->input_path) {
                nmo_inspect_log(opts, LOG_ERROR, "Multiple input files are not supported");
                return -1;
            }
            opts->input_path = argv[i];
        }
    }

    if (!opts->input_path && !opts->show_help && !opts->show_version) {
        nmo_inspect_log(opts, LOG_ERROR, "No input file specified");
        return -1;
    }

    if (!opts->modes.modes_requested) {
        opts->modes.summary = true;
        opts->modes.objects = true;
        opts->modes.warnings = true;
    }
    if (opts->modes.suppress_summary) {
        opts->modes.summary = false;
    }
    if (opts->modes.suppress_header) {
        opts->modes.header = false;
    }
    if (opts->modes.suppress_warnings) {
        opts->modes.warnings = false;
    }
    return 0;
}
