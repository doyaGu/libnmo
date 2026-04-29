/**
 * @file value_writer.c
 * @brief Transaction-scoped typed parameter value writing facade.
 */

#include "object/nmo_value_writer.h"

nmo_value_write_options_t nmo_value_write_options_default(void)
{
    nmo_value_write_options_t options = {0};
    options.manager_entry = nmo_manager_entry_options_default();
    return options;
}

nmo_status_t nmo_value_writer_set_parameter_value(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const char *value_str,
    const nmo_value_write_options_t *options)
{
    return nmo_object_edit_set_parameter_value_ex(
        edit,
        parameter_id,
        value_str,
        (const nmo_parameter_write_options_t *)options);
}

nmo_status_t nmo_value_writer_set_parameter_bytes(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count,
    const nmo_value_write_options_t *options)
{
    return nmo_object_edit_set_parameter_bytes_ex(
        edit,
        parameter_id,
        bytes,
        byte_count,
        (const nmo_parameter_write_options_t *)options);
}
