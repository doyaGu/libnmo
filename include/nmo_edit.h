#ifndef NMO_EDIT_H
#define NMO_EDIT_H

/**
 * @file nmo_edit.h
 * @brief Umbrella header of the edit component (library nmo_edit).
 *
 * Edit plans, script edits, behavior rewrites (replace and fold), the semantic
 * validator, the probe analyzer and behavior execution live outside the core
 * library. Link nmo_edit in addition to nmo (CMake: nmo::edit, pkg-config:
 * libnmo-edit). The Lua and project components build on it.
 */

#include "nmo.h"

#include "edit/nmo_edit_plan.h"
#include "edit/nmo_edit_plan_json.h"
#include "edit/nmo_script_edit.h"
#include "edit/nmo_behavior_edit.h"
#include "edit/nmo_probe_analyzer.h"
#include "edit/nmo_semantic_validator.h"
#include "edit/nmo_behavior_execute.h"

#endif /* NMO_EDIT_H */
