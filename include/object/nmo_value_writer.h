#ifndef NMO_VALUE_WRITER_H
#define NMO_VALUE_WRITER_H

#include "object/nmo_object_edit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef nmo_parameter_write_options_t nmo_value_write_options_t;

NMO_API nmo_value_write_options_t nmo_value_write_options_default(void);

NMO_API nmo_status_t nmo_value_writer_set_parameter_value(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const char *value_str,
    const nmo_value_write_options_t *options);

NMO_API nmo_status_t nmo_value_writer_set_parameter_bytes(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count,
    const nmo_value_write_options_t *options);

#ifdef __cplusplus
}
#endif

#endif /* NMO_VALUE_WRITER_H */
