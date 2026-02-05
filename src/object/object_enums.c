/**
 * @file object_enums.c
 * @brief CK2/VxMath enum and flags registration
 */

#include "object/nmo_object_enums.h"
#include "object/nmo_object_enums_defs.h"
#include "type/nmo_dynamic_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

#define NMO_ENUM_VALUE(_name, _value) {#_name, (_value), NULL}
#define NMO_FLAGS_BIT(_name, _mask) {#_name, (uint64_t)(_mask), NULL}

#define NMO_ENUM_DEF(_name, _guid_d1, _guid_d2, _default_value, _values_macro) \
    static const nmo_enum_value_def_t _name##_values[] = { _values_macro(NMO_ENUM_VALUE) }; \
    static const nmo_enum_type_def_t _name##_def = { \
        .name = #_name, \
        .description = NULL, \
        .guid = { (_guid_d1), (_guid_d2) }, \
        .values = _name##_values, \
        .value_count = sizeof(_name##_values) / sizeof(_name##_values[0]), \
        .default_value = (_default_value), \
    }

#define NMO_FLAGS_DEF(_name, _guid_d1, _guid_d2, _default_value, _bits_macro) \
    static const nmo_flags_bit_def_t _name##_bits[] = { _bits_macro(NMO_FLAGS_BIT) }; \
    static const nmo_flags_type_def_t _name##_def = { \
        .name = #_name, \
        .description = NULL, \
        .guid = { (_guid_d1), (_guid_d2) }, \
        .bits = _name##_bits, \
        .bit_count = sizeof(_name##_bits) / sizeof(_name##_bits[0]), \
        .default_value = (_default_value), \
    }

/* Generated enum/flags definitions */
#define NMO_ENUMS_EMIT_DECLS
#include "object_enums.generated.h"
#undef NMO_ENUMS_EMIT_DECLS

#define NMO_REGISTER_ENUM(_registry, _name) \
    do { \
        result = nmo_type_registry_register_enum((_registry), &_name##_def, NULL); \
        if (result != NMO_OK) { \
            NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR, \
                             "Failed to register enum '%s'", #_name); \
        } \
    } while (0)

#define NMO_REGISTER_FLAGS(_registry, _name) \
    do { \
        result = nmo_type_registry_register_flags((_registry), &_name##_def, NULL); \
        if (result != NMO_OK) { \
            NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR, \
                             "Failed to register flags '%s'", #_name); \
        } \
    } while (0)

nmo_status_t nmo_register_object_enums(nmo_type_registry_t *registry) {
    nmo_status_t result;

    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    #define NMO_ENUMS_EMIT_REGISTRATIONS
    #include "object_enums.generated.h"
    #undef NMO_ENUMS_EMIT_REGISTRATIONS

    NMO_RETURN_OK();
}
