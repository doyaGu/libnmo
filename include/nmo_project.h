#ifndef NMO_PROJECT_H
#define NMO_PROJECT_H

/**
 * @file nmo_project.h
 * @brief Umbrella header of the project authoring component (library nmo_project).
 *
 * Project plans, scene, asset and script authoring, the plan executor and the
 * manifest reader live outside the core library. Link nmo_project in addition
 * to nmo (CMake: nmo::project, pkg-config: libnmo-project).
 */

#include "nmo.h"
#include "nmo_edit.h"

#include "project/nmo_project_plan.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_scene_authoring.h"
#include "project/nmo_script_authoring.h"
#include "project/nmo_project_validator.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_manifest_json.h"

#endif /* NMO_PROJECT_H */
