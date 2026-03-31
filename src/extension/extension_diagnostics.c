/**
 * @file extension_diagnostics.c
 * @brief Extension diagnostics implementation
 */

#include "extension/nmo_extension_diagnostics.h"
#include "extension/nmo_extension_registry.h"

/* ============================================================================
 * Dependency Resolution
 * ============================================================================ */

static int dependency_category_matches(
    nmo_plugin_category_t required,
    nmo_plugin_category_t actual)
{
    /* Preserve legacy caller behavior: CUSTOM means "any category". */
    if (required == NMO_PLUGIN_CUSTOM_DLL) {
        return 1;
    }

    return required == actual;
}

const char *nmo_extension_category_label(nmo_plugin_category_t category) {
    switch (category) {
        case NMO_PLUGIN_MANAGER_DLL:       return "Manager";
        case NMO_PLUGIN_BEHAVIOR_DLL:      return "Behavior";
        case NMO_PLUGIN_RENDER_DLL:        return "Render";
        case NMO_PLUGIN_SOUND_DLL:         return "Sound";
        case NMO_PLUGIN_INPUT_DLL:         return "Input";
        case NMO_PLUGIN_OBJECT_READER_DLL: return "ObjectReader";
        case NMO_PLUGIN_CUSTOM_DLL:        return "Custom";
        default:                           return "Unknown";
    }
}

int nmo_extension_check_dependency(
    const nmo_extension_registry_t *registry,
    nmo_plugin_category_t category,
    nmo_guid_t guid,
    uint32_t min_version,
    nmo_extension_dependency_result_t *out_result)
{
    if (registry == NULL) {
        if (out_result) {
            out_result->guid = guid;
            out_result->required_version = min_version;
            out_result->found_version = 0;
            out_result->satisfied = 0;
        }
        return 0;
    }

    const nmo_extension_plugin_info_t *info = nmo_extension_registry_find(registry, guid);
    int category_ok = (info != NULL) && dependency_category_matches(category, info->category);
    int version_ok = (info != NULL) && (min_version == 0 || info->version >= min_version);
    int satisfied = category_ok && version_ok;

    if (out_result) {
        out_result->guid = guid;
        out_result->required_version = min_version;
        out_result->found_version = info ? info->version : 0;
        out_result->satisfied = satisfied;
    }

    return satisfied;
}

size_t nmo_extension_check_dependencies(
    const nmo_extension_registry_t *registry,
    const nmo_plugin_category_t *categories,
    const nmo_guid_t *guids,
    const uint32_t *min_versions,
    size_t count,
    nmo_extension_dependency_result_t *out_results)
{
    if (guids == NULL || count == 0) {
        return 0;
    }

    size_t unsatisfied = 0;

    for (size_t i = 0; i < count; i++) {
        nmo_plugin_category_t cat = categories ? categories[i] : NMO_PLUGIN_CUSTOM_DLL;
        uint32_t ver = min_versions ? min_versions[i] : 0;

        nmo_extension_dependency_result_t *result = out_results ? &out_results[i] : NULL;

        if (!nmo_extension_check_dependency(registry, cat, guids[i], ver, result)) {
            unsatisfied++;
        }
    }

    return unsatisfied;
}
