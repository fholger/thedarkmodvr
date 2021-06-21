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
#pragma once

#include "DrawBatchExecutor.h"

class DepthStage
{
public:
	DepthStage( DrawBatchExecutor *drawBatchExecutor );

	void Init();
	void Shutdown();

	void DrawDepth( const viewDef_t *viewDef, drawSurf_t **drawSurfs, int numDrawSurfs );

private:
	struct ShaderParams;

	DrawBatchExecutor *drawBatchExecutor;
	GLSLProgram *depthShader = nullptr;
	GLSLProgram *depthShaderBindless = nullptr;
	GLSLProgram *fastDepthShader = nullptr;

	uint currentIndex = 0;
	DrawBatch<ShaderParams> drawBatch;

	void PartitionSurfaces( drawSurf_t **drawSurfs, int numDrawSurfs, idList<drawSurf_t *> &subviewSurfs,
			idList<drawSurf_t *> &opaqueSurfs, idList<drawSurf_t *> &remainingSurfs );
	void GenericDepthPass( const idList<drawSurf_t *> drawSurfs, const viewDef_t *viewDef );
	void FastDepthPass( const idList<drawSurf_t *> drawSurfs, const viewDef_t *viewDef );
	bool ShouldDrawSurf( const drawSurf_t *surf ) const;
	void DrawSurf( const drawSurf_t * drawSurf );
	void CreateDrawCommands( const drawSurf_t *surf );
	void IssueDrawCommand( const drawSurf_t *surf, const shaderStage_t *stage );

	void BeginDrawBatch();
	void ExecuteDrawCalls();
};
