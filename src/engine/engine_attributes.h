/**
 * @file engine_attributes.h
 * @brief The attribute manager: attribute types, and the attributes objects
 *        have at run time.
 */

#ifndef NMO_ENGINE_ATTRIBUTES_H
#define NMO_ENGINE_ATTRIBUTES_H

#include "engine_graph.h"

typedef struct eng_range eng_range_t;

/* An attribute type of the engine, from a document's Attribute Manager */
typedef struct eng_attribute_type {
    bool present;
    uint32_t flags; /* CK_ATTRIBUT_FLAGS */
    char *name;
    nmo_guid_t parameter_type; /* null for an attribute without a value */
    int32_t compatible_class_id;
} eng_attribute_type_t;

/* An attribute an object has at run time, and its parameter's value */
typedef struct eng_attribute {
    nmo_object_id_t object;
    int32_t type;
    bool has_value;
    eng_param_t value;
} eng_attribute_t;

typedef struct eng_attributes {
    ENG_VEC(eng_attribute_type_t) types;
    ENG_VEC(eng_attribute_t) values;
} eng_attributes_t;

nmo_status_t eng_load_attributes(nmo_engine_t *engine, const eng_range_t *range);
void eng_free_attributes(nmo_engine_t *engine);
/* The attribute of that type the object has; NULL when it has none */
eng_attribute_t *eng_attribute_find(const nmo_engine_t *engine, nmo_object_id_t object,
                                    int32_t type);

#endif /* NMO_ENGINE_ATTRIBUTES_H */
