/**
 * @file nmo_cmd_patch.h
 * @brief Patch command group.
 */

#ifndef NMO_CMD_PATCH_H
#define NMO_CMD_PATCH_H

#include "../nmo_cli_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_patch_apply(int argc,
                        char **argv,
                        const nmo_cli_global_opts_t *global);

int nmo_cmd_patch_diff(int argc,
                       char **argv,
                       const nmo_cli_global_opts_t *global);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_PATCH_H */
