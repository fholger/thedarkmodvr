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

#include "precompiled.h"
#include "tr_local.h"
#include "glsl.h"

struct DrawElementsIndirectCommand {
	uint count;
	uint instanceCount;
	uint firstIndex;
	uint baseVertex;
	uint baseInstance;
};

struct StencilDrawData {
	float  modelMatrix[16];
	idVec4 lightOrigin;
};

void RB_StencilShadowPass_MultiDraw(const drawSurf_t* drawSurfs) {
	static StencilDrawData* drawData = ( StencilDrawData* )Mem_Alloc16( sizeof( StencilDrawData ) * 1024 );
	static DrawElementsIndirectCommand* commands = ( DrawElementsIndirectCommand * )Mem_Alloc16( sizeof( DrawElementsIndirectCommand ) * 1024 );

	int count = 0;
	for( const drawSurf_t *drawSurf = drawSurfs; drawSurf; drawSurf = drawSurf->nextOnLight ) {
		if( count == 512 )
			break; // TODO
		const srfTriangles_t *tri = drawSurf->backendGeo;
		if( vertexCache.CacheIsStatic( tri->shadowCache ) || vertexCache.CacheIsStatic( tri->indexCache ) )
			continue; // TODO
		memcpy( drawData[count].modelMatrix, drawSurf->space->modelMatrix, sizeof( drawData[0].modelMatrix ) );
		R_GlobalPointToLocal( drawSurf->space->modelMatrix, backEnd.vLight->globalLightOrigin, drawData[count].lightOrigin.ToVec3() );
		drawData[count].lightOrigin.w = 0.0f;
		commands[count].count = tri->numIndexes;
		commands[count].instanceCount = 1;
		commands[count].firstIndex = ( ( tri->indexCache >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK ) / sizeof( glIndex_t );
		commands[count].baseVertex = ( ( tri->shadowCache >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK ) / sizeof( shadowCache_t );
		commands[count].baseInstance = 0;
		++count;
	}
	//common->Printf( "Num commands: %d\n", count );
	if( count == 0 )
		return;

	stencilShadowShaderMultiDraw.Use();
	qglPushDebugGroup( GL_DEBUG_SOURCE_APPLICATION, 20001, -1, "StencilShadowPassMultiDraw" );

	globalImages->BindNull();
	GL_State( GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHFUNC_LESS );
	//GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_LESS );
	if( r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat() ) {
		qglPolygonOffset( r_shadowPolygonFactor.GetFloat(), -r_shadowPolygonOffset.GetFloat() );
		qglEnable( GL_POLYGON_OFFSET_FILL );
	}
	//qglDisable( GL_SCISSOR_TEST );

	qglStencilFunc( GL_ALWAYS, 1, 255 );
	qglStencilOpSeparate( backEnd.viewDef->isMirror ? GL_FRONT : GL_BACK, GL_KEEP, tr.stencilDecr, GL_KEEP );
	qglStencilOpSeparate( backEnd.viewDef->isMirror ? GL_BACK : GL_FRONT, GL_KEEP, tr.stencilIncr, GL_KEEP );
	GL_Cull( CT_TWO_SIDED );

	/*if( glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool() )
		qglEnable( GL_DEPTH_BOUNDS_TEST_EXT );*/

	GLint lightOriginPos = qglGetUniformLocation( stencilShadowShaderMultiDraw.program, "lightOrigin" );
	idVec4 lightOrigin;
	lightOrigin.ToVec3() = backEnd.vLight->globalLightOrigin;
	lightOrigin.w = 0;
	qglUniform4fv( lightOriginPos, 1, lightOrigin.ToFloatPtr() );
	GLint projMatrixPos = qglGetUniformLocation( stencilShadowShaderMultiDraw.program, "projectionMatrix" );
	GLint viewMatrixPos = qglGetUniformLocation( stencilShadowShaderMultiDraw.program, "viewMatrix" );
	float viewProjectionMatrix[16];
	myGlMultMatrix( backEnd.viewDef->projectionMatrix, backEnd.viewDef->worldSpace.modelViewMatrix, viewProjectionMatrix );
	qglUniformMatrix4fv( projMatrixPos, 1, GL_FALSE, backEnd.viewDef->projectionMatrix );
	qglUniformMatrix4fv( viewMatrixPos, 1, GL_FALSE, backEnd.viewDef->worldSpace.modelViewMatrix );

	static GLuint ubo = 0;
	if( !ubo ) {
		qglGenBuffersARB( 1, &ubo );
	}
	qglBindBufferARB( GL_UNIFORM_BUFFER, ubo );
	qglBufferDataARB( GL_UNIFORM_BUFFER, sizeof( StencilDrawData ) * 512, drawData, GL_DYNAMIC_DRAW );
	qglBindBufferBase( GL_UNIFORM_BUFFER, 0, ubo );

	qglVertexAttribPointer( 0, 4, GL_FLOAT, GL_FALSE, sizeof( shadowCache_t ), vertexCache.VertexPosition( 2 ) );
	vertexCache.IndexPosition( 2 );

	qglMultiDrawElementsIndirect( GL_TRIANGLES, GL_INDEX_TYPE, commands, count, 0 );

	GL_CheckErrors();

	GL_Cull( CT_FRONT_SIDED );

	if( r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat() )
		qglDisable( GL_POLYGON_OFFSET_FILL );

	/*if( glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool() )
		qglDisable( GL_DEPTH_BOUNDS_TEST_EXT );*/

	qglStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	if( !r_softShadowsQuality.GetBool() || backEnd.viewDef->renderView.viewID < TR_SCREEN_VIEW_ID )
		qglStencilFunc( GL_GEQUAL, 128, 255 );

	qglPopDebugGroup();
}