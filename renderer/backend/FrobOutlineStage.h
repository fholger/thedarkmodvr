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
class FrameBuffer;

class FrobOutlineStage {
public:
	void Init();
	void Shutdown();

	void DrawFrobOutline( drawSurf_t **drawSurfs, int numDrawSurfs );

private:
	GLSLProgram *silhouetteShader = nullptr;
	GLSLProgram *extrudeShader = nullptr;
	GLSLProgram *applyShader = nullptr;

	idImage *colorTex[2] = { nullptr };
	idImage *depthTex = nullptr;
	FrameBuffer *fbo[2] = { nullptr };
	FrameBuffer *drawFbo = nullptr;

	void CreateFbo( int idx );
	void CreateDrawFbo();

	void MaskObjects( idList<drawSurf_t*> &surfs );
	void MaskOutlines( idList<drawSurf_t*> &surfs );
	void DrawSoftOutline( idList<drawSurf_t*> &surfs );
	void DrawObjects( idList<drawSurf_t*> &surfs, GLSLProgram *shader );
	void ApplyBlur();
};
