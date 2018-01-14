/*****************************************************************************
The Dark Mod GPL Source Code

This file is part of the The Dark Mod Source Code, originally based
on the Doom 3 GPL Source Code as published in 2011.

The Dark Mod Source Code is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the License,
or (at your option) any later version. For details, see LICENSE.TXT.

Project: The Dark Mod (http://www.thedarkmod.com/)

******************************************************************************/
#ifndef __OCCLUSION_CULLING_H__
#define __OCCLUSION_CULLING_H__
#include "PersistentBufferObject.h"
#include "../tr_local.h"
#include <vector>
#include <unordered_set>

struct Occluder {
	idVec4 bboxMin;
	idVec4 bboxMax;
};

class OcclusionSystem {
public:
	OcclusionSystem();

	void Init();
	void Shutdown();

	Occluder * OcclusionSystem::ReserveOccluders( uint count );

	void PrepareVisibilityBuffer();
	void BindOccluders();
	void Finish( uint count );

	const int * GetVisibilityResults() const { return visibility; }

	void SetEntityIdVisible( int entityId );
	bool IsEntityIdVisible( int entityId ) const;

private:
	bool initialized;
	PersistentBufferObject bboxBuffer;
	GLuint visibilityBuffer;
	GLuint visibilityCopyBuffer;
	int *visibility;
	std::unordered_set<int> visibleEntities;
};

extern OcclusionSystem occlusionSystem;

#endif