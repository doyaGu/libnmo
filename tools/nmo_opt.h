/**
 * @file nmo_opt.h
 * @brief Declarative option parser for CLI commands
 *
 * Replaces the 3 different option-parsing approaches with a single
 * table-driven parser. Commands declare a static array of nmo_opt_def_t
 * and call nmo_opt_parse() to get structured results.
 */

#ifndef NMO_OPT_H
#define NMO_OPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Option types
 */
typedef enum {
    NMO_OPT_FLAG,    /**< Boolean flag: --verbose, --full */
    NMO_OPT_STRING,  /**< String value: --class CKMesh */
    NMO_OPT_UINT,    /**< Unsigned integer: --depth 4, --id 2032 */
    NMO_OPT_INT,     /**< Signed integer: --offset -1 */
    NMO_OPT_FLOAT,   /**< Float value: --min-similarity 0.8 */
} nmo_opt_type_t;

/**
 * @brief Option definition (compile-time constant)
 */
typedef struct {
    const char *long_name;   /**< Long form: "--class" (required) */
    const char *short_name;  /**< Short form: "-c" (NULL if none) */
    nmo_opt_type_t type;     /**< Value type */
    const char *help;        /**< Help text for --help output */
} nmo_opt_def_t;

/**
 * @brief Parsed value for a single option
 */
typedef struct {
    bool present;       /**< Was this option found in argv? */
    union {
        bool flag;          /**< NMO_OPT_FLAG value */
        const char *str;    /**< NMO_OPT_STRING value (points into argv) */
        uint32_t u;         /**< NMO_OPT_UINT value */
        int32_t i;          /**< NMO_OPT_INT value */
        float f;            /**< NMO_OPT_FLOAT value */
    } val;
} nmo_opt_val_t;

/**
 * @brief Parse result
 */
typedef struct {
    nmo_opt_val_t *vals;     /**< Array parallel to defs[], caller-allocated */
    const char **pos_args;   /**< Positional arguments (points into argv) */
    size_t pos_capacity;     /**< Capacity of pos_args array */
    size_t pos_count;        /**< Number of positional arguments */
} nmo_opt_result_t;

/**
 * @brief Shared option definitions
 *
 * Common options spelled the same way across commands. Use them as
 * elements of a command's nmo_opt_def_t table.
 */
#define NMO_OPT_DEF_OUTPUT \
    {"--output", "-o", NMO_OPT_STRING, "Output file path"}
#define NMO_OPT_DEF_WRITE_OUTPUT \
    {"--output", "-o", NMO_OPT_STRING, "Output file (required unless --dry-run)"}
#define NMO_OPT_DEF_DRY_RUN \
    {"--dry-run", NULL, NMO_OPT_FLAG, "Preview without saving"}
#define NMO_OPT_DEF_JSON \
    {"--json", "-j", NMO_OPT_FLAG, "JSON output"}
#define NMO_OPT_DEF_CLASS_FILTER \
    {"--class", "-c", NMO_OPT_STRING, "Filter by class name"}
#define NMO_OPT_DEF_DEPTH \
    {"--depth", "-d", NMO_OPT_UINT, "Recursion depth (default: unlimited)"}
#define NMO_OPT_DEF_FAST_SAVE \
    {"--fast-save", NULL, NMO_OPT_FLAG, "Skip explicit save flush/write-through"}
#define NMO_OPT_DEF_IGNORE_ORDER \
    {"--ignore-order", NULL, NMO_OPT_FLAG, "Ignore object order"}

/**
 * @brief Initialize a parse result over caller-owned arrays.
 *
 * @p pos must be an array (not a pointer) so its capacity can be derived.
 */
#define NMO_OPT_RESULT(vals_, pos_) \
    { .vals = (vals_), .pos_args = (pos_), \
      .pos_capacity = sizeof(pos_) / sizeof((pos_)[0]) }

/** @brief String value, or NULL when the option is absent. */
static inline const char *nmo_opt_str(const nmo_opt_val_t *v)
{
    return v->present ? v->val.str : NULL;
}

/** @brief True when a flag option is present and set. */
static inline bool nmo_opt_flag(const nmo_opt_val_t *v)
{
    return v->present && v->val.flag;
}

/** @brief Unsigned value, or @p fallback when the option is absent. */
static inline uint32_t nmo_opt_uint_or(const nmo_opt_val_t *v, uint32_t fallback)
{
    return v->present ? v->val.u : fallback;
}

/** @brief Float value, or @p fallback when the option is absent. */
static inline float nmo_opt_float_or(const nmo_opt_val_t *v, float fallback)
{
    return v->present ? v->val.f : fallback;
}

/**
 * @brief Parse command-local argv against option definitions.
 *
 * Scans argv[1..argc-1] (argv[0] is the action name).
 * Recognized options fill vals[]; unrecognized tokens that don't start
 * with '-' are collected as positional arguments.
 *
 * On error (unknown --option, missing value, bad integer), prints a
 * message to stderr and returns -1.
 *
 * @param argc      Command-local argc
 * @param argv      Command-local argv
 * @param defs      Option definitions array
 * @param def_count Number of definitions
 * @param result    Output: parsed values and positional args.
 *                  result->vals must point to a nmo_opt_val_t[def_count].
 *                  result->pos_args must point to a const char*[] with
 *                  enough room (argc is a safe upper bound).
 * @return 0 on success, -1 on error
 */
int nmo_opt_parse(int argc, char **argv,
                  const nmo_opt_def_t *defs, size_t def_count,
                  nmo_opt_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OPT_H */
