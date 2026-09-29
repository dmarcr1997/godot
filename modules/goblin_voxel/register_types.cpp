/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/
/*  Goblin Kitchen fork: goblin_voxel module.                             */
/**************************************************************************/

#include "register_types.h"

#include "voxel_grid.h"

#include "core/object/class_db.h"

void initialize_goblin_voxel_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(GKVoxelGrid);
}

void uninitialize_goblin_voxel_module(ModuleInitializationLevel p_level) {
}
