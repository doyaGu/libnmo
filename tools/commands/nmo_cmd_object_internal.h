/**
 * @file nmo_cmd_object_internal.h
 * @brief Internal shared helpers for object-family read command cores
 */

#ifndef NMO_CMD_OBJECT_INTERNAL_H
#define NMO_CMD_OBJECT_INTERNAL_H

#include "../nmo_cmd_ctx.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_object_show_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);
int nmo_cmd_object_show_class_in_session(nmo_cmd_ctx_t *ctx,
                                         int argc,
                                         char **argv,
                                         uint32_t required_base_class,
                                         const char *type_label);
int nmo_cmd_object_list_class_in_session(nmo_cmd_ctx_t *ctx,
                                         int argc,
                                         char **argv,
                                         const char *class_name);
int nmo_cmd_object_find_class_in_session(nmo_cmd_ctx_t *ctx,
                                         int argc,
                                         char **argv,
                                         const char *class_name);
int nmo_cmd_object_refs_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);
int nmo_cmd_object_refgraph_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_OBJECT_INTERNAL_H */
