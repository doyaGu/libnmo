/**
 * @file ckclass_stubs.c
 * @brief Temporary stubs for CKClass hierarchy functions
 * 
 * These are minimal implementations to allow linking during Phase 6.1.
 * Will be replaced with full implementation once schema_v2 is complete.
 */

#include <stdint.h>
#include <string.h>

/**
 * @brief Temporary stub: Get class name by ID
 */
const char *nmo_ckclass_get_name_by_id(uint32_t class_id) {
    (void)class_id;
    /* Return placeholder - actual implementation deferred */
    return "Unknown";
}

/**
 * @brief Temporary stub: Get parent class name
 */
const char *nmo_ckclass_get_parent(const char *class_name) {
    (void)class_name;
    /* Return NULL (no parent) - actual implementation deferred */
    return NULL;
}

/**
 * @brief Temporary stub: Get class ID by name
 */
uint32_t nmo_ckclass_get_id_by_name(const char *class_name) {
    (void)class_name;
    /* Return 0 (invalid) - actual implementation deferred */
    return 0;
}
