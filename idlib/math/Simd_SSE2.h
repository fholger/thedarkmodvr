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

#ifndef __MATH_SIMD_SSE2_H__
#define __MATH_SIMD_SSE2_H__

#include "Simd_SSE.h"

/*
===============================================================================

	SSE2 implementation of idSIMDProcessor

===============================================================================
*/

class idSIMD_SSE2 : public idSIMD_SSE {
public:
	idSIMD_SSE2();

#ifdef ENABLE_SSE_PROCESSORS
	virtual void NormalizeTangents( idDrawVert *verts, const int numVerts );
	virtual void TransformVerts( idDrawVert *verts, const int numVerts, const idJointMat *joints, const idVec4 *weights, const int *index, const int numWeights );
	virtual	void MinMax( idVec3 &min, idVec3 &max, const idDrawVert *src, const int count );
	virtual void MinMax( idVec3 &min, idVec3 &max, const idDrawVert *src, const int *indexes, const int count );
	virtual void DeriveTangents( idPlane *planes, idDrawVert *verts, const int numVerts, const int *indexes, const int numIndexes );
	virtual int  CreateVertexProgramShadowCache( idVec4 *vertexCache, const idDrawVert *verts, const int numVerts );
	virtual void TracePointCull( byte *cullBits, byte &totalOr, const float radius, const idPlane *planes, const idDrawVert *verts, const int numVerts );

	virtual void Memcpy( void* dst, const void* src, const int count );
	virtual void CalcTriFacing( const idDrawVert *verts, const int numVerts, const int *indexes, const int numIndexes, const idVec3 &lightOrigin, byte *facing );
#endif
};

#endif /* !__MATH_SIMD_SSE2_H__ */
