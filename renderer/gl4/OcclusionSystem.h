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
	idVec3 bboxMin;
	idVec3 bboxMax;
	uint32_t entityId;
	uint32_t padding;
};

class OcclusionSystem {
public:
	OcclusionSystem();

	void Init();
	void Shutdown();

	Occluder * OcclusionSystem::ReserveOccluders( uint count );

	void BeginFrame();
	void EndFrame();  // run after frontend and backend are finished!

	void PrepareVisibilityBuffer();
	void BindOccluders();
	void CompressOutput();
	void Finish( uint count );

	void TransferResults();

	bool WasEntityCulledLastFrame( int entityId ) const;

	void SetEntityIdTested( int entityId );

private:
	bool initialized;
	PersistentBufferObject bboxBuffer;
	GLuint visibilityBuffer;
	GLuint visibilityCompressBuffer;
	GLuint visibilityCopyBuffer;
	uint32_t *visibilityResults;
	uint32_t *testedEntities;
	uint32_t *lastFrameCulled;
};

extern OcclusionSystem occlusionSystem;

#endif