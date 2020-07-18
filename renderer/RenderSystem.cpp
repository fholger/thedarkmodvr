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
#pragma hdrstop

#include "tr_local.h"
#include "FrameBuffer.h"
#include "glsl.h"
#include "Profiling.h"
#include "backend/RenderBackend.h"
#include "FrameBufferManager.h"
#include "vr/VrBackend.h"

idRenderSystemLocal	tr;
idRenderSystem	*renderSystem = &tr;

idCVarBool r_tonemap( "r_tonemap", "1", CVAR_RENDERER | CVAR_ARCHIVE, "Use the tonemap correction (gamma, brightness, etc)" );

/*
=====================
R_PerformanceCounters

This prints both front and back end counters, so it should
only be called when the back end thread is idle.
=====================
*/
static void R_PerformanceCounters( void ) {
	if ( r_showPrimitives.GetInteger() != 0 ) {

		int numUsed = -1;
		int bytesUsed = globalImages->SumOfUsedImages(&numUsed);

		if ( r_showPrimitives.GetInteger() > 1 ) {
			common->Printf( "v:%i ds:%i t:%i/%i v:%i/%i st:%i sv:%i images:%d ml:%i\n",
				tr.pc.c_numViews,
				backEnd.pc.c_drawElements + backEnd.pc.c_shadowElements,
				backEnd.pc.c_drawIndexes / 3,
				(backEnd.pc.c_drawIndexes - backEnd.pc.c_drawRefIndexes) / 3,
				backEnd.pc.c_drawVertexes,
				(backEnd.pc.c_drawVertexes - backEnd.pc.c_drawRefVertexes),
				backEnd.pc.c_shadowIndexes / 3,
				backEnd.pc.c_shadowVertexes,
				numUsed,
				backEnd.pc.c_matrixLoads
			);
		} else {
			common->Printf( "views:%i draws:%i tris:%i (shdw:%i) (vbo:%i) image:%d MB\n",
			                tr.pc.c_numViews,
			                backEnd.pc.c_drawElements + backEnd.pc.c_shadowElements,
			                ( backEnd.pc.c_drawIndexes + backEnd.pc.c_shadowIndexes ) / 3,
			                backEnd.pc.c_shadowIndexes / 3,
			                backEnd.pc.c_vboIndexes / 3,
			                bytesUsed >> 20
			              );
		}
	}

	if ( r_showDynamic.GetBool() ) {
		common->Printf( "callback:%i md5:%i dfrmVerts:%i dfrmTris:%i tangTris:%i guis:%i\n",
		                tr.pc.c_entityDefCallbacks,
		                tr.pc.c_generateMd5,
		                tr.pc.c_deformedVerts,
		                tr.pc.c_deformedIndexes / 3,
		                tr.pc.c_tangentIndexes / 3,
		                tr.pc.c_guiSurfs
		              );
	}

	if ( r_showCull.GetBool() ) {
		common->Printf( "%i sin %i sclip  %i sout %i bin %i bout\n",
		                tr.pc.c_sphere_cull_in, tr.pc.c_sphere_cull_clip, tr.pc.c_sphere_cull_out,
		                tr.pc.c_box_cull_in, tr.pc.c_box_cull_out );
	}

	if ( r_showAlloc.GetBool() ) {
		common->Printf( "alloc:%i free:%i\n", tr.pc.c_alloc, tr.pc.c_free );
	}

	if ( r_showInteractions.GetBool() ) {
		common->Printf( "createInteractions:%i createLightTris:%i createShadowVolumes:%i\n",
		                tr.pc.c_createInteractions, tr.pc.c_createLightTris, tr.pc.c_createShadowVolumes );
	}
	if ( r_showDefs.GetBool() ) {
		common->Printf( "viewEntities:%i  shadowEntities:%i  viewLights:%i\n", tr.pc.c_visibleViewEntities,
		                tr.pc.c_shadowViewEntities, tr.pc.c_viewLights );
	}
	if ( r_showUpdates.GetBool() ) {
		common->Printf( "entityUpdates:%i  entityRefs:%i  lightUpdates:%i  lightRefs:%i\n",
		                tr.pc.c_entityUpdates, tr.pc.c_entityReferences,
		                tr.pc.c_lightUpdates, tr.pc.c_lightReferences );
	}
	if ( r_showMemory.GetBool() ) {
		int m0 = frameData ? frameData->frameMemoryAllocated.load() : 0;
		int	m1 = frameData ? frameData->memoryHighwater : 0;
		common->Printf( "frameData: %i (%i)\n", m0, m1 );
	}
	if ( r_showSmp.GetBool() )
	{ common->Printf( "%c", backEnd.pc.waitedFor ); }


	memset( &tr.pc, 0, sizeof( tr.pc ) );
	memset( &backEnd.pc, 0, sizeof( backEnd.pc ) );
}

/*
====================
R_IssueRenderCommands

Called by R_EndFrame each frame
====================
*/
void R_IssueRenderCommands( frameData_t *frameData ) {
	emptyCommand_t *cmds = frameData->cmdHead;
	if ( cmds->commandId == RC_NOP
	        && !cmds->next ) {
		// nothing to issue
		return;
	}

	// r_skipBackEnd allows the entire time of the back end
	// to be removed from performance measurements, although
	// nothing will be drawn to the screen.  If the prints
	// are going to a file, or r_skipBackEnd is later disabled,
	// usefull data can be received.

	// r_skipRender is usually more usefull, because it will still
	// draw 2D graphics
	if ( !r_skipBackEnd.GetBool() ) {
		RB_ExecuteBackEndCommands( cmds );
	}
	R_ClearCommandChain( frameData );
}

/*
============
R_GetCommandBuffer

Returns memory for a command buffer (stretchPicCommand_t (??? - duzenko),
drawSurfsCommand_t, etc) and links it to the end of the
current command chain.
============
*/
void *R_GetCommandBuffer( int bytes ) {
	emptyCommand_t	*cmd;

	cmd = ( emptyCommand_t * )R_FrameAlloc( bytes );
	cmd->next = NULL;
	frameData->cmdTail->next = cmd;
	frameData->cmdTail = cmd;

	return ( void * )cmd;
}


/*
====================
R_ClearCommandChain

Called after every buffer submission
and by R_ToggleSmpFrame
====================
*/
void R_ClearCommandChain( frameData_t *frameData ) {
	// clear the command chain
	frameData->cmdHead = frameData->cmdTail = ( emptyCommand_t * )R_FrameAlloc( sizeof( *frameData->cmdHead ) );
	frameData->cmdHead->commandId = RC_NOP;
	frameData->cmdHead->next = NULL;
}

/*
=================
R_ViewStatistics
=================
*/
static void R_ViewStatistics( viewDef_t &parms ) {
	// report statistics about this view
	if ( !r_showSurfaces.GetBool() ) {
		return;
	}
	common->Printf( "view:%i surfs:%i (%i)\n", tr.pc.c_numViews, parms.numDrawSurfs, tr.pc.c_noshadowSurfs );
}

/*
=============
R_AddDrawViewCmd

This is the main 3D rendering command.  A single scene may
have multiple views if a mirror, portal, or dynamic texture is present.
=============
*/
void	R_AddDrawViewCmd( viewDef_t &parms ) {
	drawSurfsCommand_t	*cmd;

	cmd = ( drawSurfsCommand_t * )R_GetCommandBuffer( sizeof( *cmd ) );
	cmd->commandId = RC_DRAW_VIEW;

	cmd->viewDef = &parms;

	if ( parms.viewEntitys ) {
		// save the command for r_lockSurfaces debugging
		tr.lockSurfacesCmd = *cmd;
	}
	tr.pc.c_numViews++;

	R_ViewStatistics( parms );
}


//=================================================================================


/*
======================
R_LockSurfaceScene

r_lockSurfaces allows a developer to move around
without changing the composition of the scene, including
culling.  The only thing that is modified is the
view position and axis, no front end work is done at all


Add the stored off command again, so the new rendering will use EXACTLY
the same surfaces, including all the culling, even though the transformation
matricies have been changed.  This allow the culling tightness to be
evaluated interactively.
======================
*/
void R_LockSurfaceScene( viewDef_t &parms ) {
	drawSurfsCommand_t	*cmd;
	viewEntity_t			*vModel;

	// set the matrix for world space to eye space
	R_SetViewMatrix( parms );
	tr.lockSurfacesCmd.viewDef->worldSpace = parms.worldSpace;

	// update the view origin and axis, and all
	// the entity matricies
	for ( vModel = tr.lockSurfacesCmd.viewDef->viewEntitys ; vModel ; vModel = vModel->next ) {
		myGlMultMatrix( vModel->modelMatrix,
		                tr.lockSurfacesCmd.viewDef->worldSpace.modelViewMatrix,
		                vModel->modelViewMatrix );
	}

	// add the stored off surface commands again
	cmd = ( drawSurfsCommand_t * )R_GetCommandBuffer( sizeof( *cmd ) );
	*cmd = tr.lockSurfacesCmd;
}

/*
=============
R_CheckCvars

See if some cvars that we watch have changed
=============
*/
static void R_CheckCvars( void ) {
	globalImages->CheckCvars();

	// if ARB shaders are not available, then GLSL must be used
	if ( !glConfig.arbAssemblyShadersAvailable && !r_useGLSL.GetBool() ) {
		common->Printf("ARB assembly shaders not supported. Forcing r_useGLSL on.\n");
		r_useGLSL.SetBool(true);
	}

	// GL debug messages
	if( r_glDebugOutput.IsModified() && GLAD_GL_KHR_debug ) {
		r_glDebugOutput.ClearModified();
		if( r_glDebugOutput.GetBool() ) {
			qglEnable( GL_DEBUG_OUTPUT );
		} else {
			qglDisable( GL_DEBUG_OUTPUT );
		}
		if( r_glDebugOutput.GetInteger() == 2 ) {
			qglEnable( GL_DEBUG_OUTPUT_SYNCHRONOUS );
		} else {
			qglDisable( GL_DEBUG_OUTPUT_SYNCHRONOUS );
		}
	}

	// revelator: autoset depth bits to the max of what the gfx card supports, in case someone tries to supply an invalid bit depth.
	// unsupported bit depth will be forced back to the max the card supports.
	/*if ( glConfig.depthBits != r_fboDepthBits.GetInteger() ) {
		if ( r_fboDepthBits.GetInteger() > glConfig.depthBits ) {
			common->Printf( "Unsupported bit depth %d attempted: Your card only supports: %d bit depth, defaults restored\n", r_fboDepthBits.GetInteger(), glConfig.depthBits );
			r_fboDepthBits.SetInteger( glConfig.depthBits );
		}
		r_fboDepthBits.SetModified();
	}*/

	// check for changes to logging state
	//GLimp_EnableLogging( r_logFile.GetInteger() != 0 );
}

/*
=============
SetColor

This can be used to pass general information to the current material, not
just colors
=============
*/
void idRenderSystemLocal::SetColor( const idVec4 &rgba ) {
	guiModel->SetColor( rgba[0], rgba[1], rgba[2], rgba[3] );
}


/*
=============
SetColor4
=============
*/
void idRenderSystemLocal::SetColor4( float r, float g, float b, float a ) {
	guiModel->SetColor( r, g, b, a );
}

/*
=============
DrawStretchPic
=============
*/
void idRenderSystemLocal::DrawStretchPic( const idDrawVert *verts, const glIndex_t *indexes, int vertCount, int indexCount, const idMaterial *material,
        bool clip, float min_x, float min_y, float max_x, float max_y ) {
	guiModel->DrawStretchPic( verts, indexes, vertCount, indexCount, material,
	                          clip, min_x, min_y, max_x, max_y );
}

/*
=============
DrawStretchPic

x/y/w/h are in the 0,0 to 640,480 range
=============
*/
void idRenderSystemLocal::DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, const idMaterial *material ) {
	guiModel->DrawStretchPic( x, y, w, h, s1, t1, s2, t2, material );
}

/*
=============
DrawStretchTri

x/y/w/h are in the 0,0 to 640,480 range
=============
*/
void idRenderSystemLocal::DrawStretchTri( idVec2 p1, idVec2 p2, idVec2 p3, idVec2 t1, idVec2 t2, idVec2 t3, const idMaterial *material ) {
	tr.guiModel->DrawStretchTri( p1, p2, p3, t1, t2, t3, material );
}

/*
=============
GlobalToNormalizedDeviceCoordinates
=============
*/
void idRenderSystemLocal::GlobalToNormalizedDeviceCoordinates( const idVec3 &global, idVec3 &ndc ) {
	R_GlobalToNormalizedDeviceCoordinates( global, ndc );
}

/*
=============
GlobalToNormalizedDeviceCoordinates
=============
*/
void idRenderSystemLocal::GetGLSettings( int &width, int &height ) {
	width = glConfig.vidWidth;
	height = glConfig.vidHeight;
}

/*
=====================
idRenderSystemLocal::DrawSmallChar

small chars are drawn at native screen resolution
=====================
*/
void idRenderSystemLocal::DrawSmallChar( int x, int y, int ch, const idMaterial *material ) {
	int row, col;
	float frow, fcol;
	float size;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -SMALLCHAR_HEIGHT ) {
		return;
	}

	row = ch >> 4;
	col = ch & 15;

	frow = row * 0.0625f;
	fcol = col * 0.0625f;
	size = 0.0625f;
	
	extern idCVarBool con_legacyFont;
	if ( con_legacyFont ) 
		DrawStretchPic( x, y, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT,
	                fcol, frow,
	                fcol + size, frow + size,
	                material );
	else {
		float fontAspect = (float)material->GetImageWidth() / material->GetImageHeight();
		float screenAspect = (float)glConfig.vidWidth / glConfig.vidHeight;
		float virtualAspect = (float)SCREEN_WIDTH / SCREEN_HEIGHT;
		float charAspect = fontAspect / screenAspect * virtualAspect;
		float charWidth = charAspect * SMALLCHAR_HEIGHT, charHeight = SMALLCHAR_HEIGHT;
		if ( charWidth > SMALLCHAR_WIDTH ) {
			charWidth = SMALLCHAR_WIDTH;
			charHeight = charWidth / charAspect;
		}
		DrawStretchPic( x, y, charWidth, charHeight, fcol, frow, fcol + size, frow + size, material );
	}
}

/*
==================
idRenderSystemLocal::DrawSmallString[Color]

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.

Coordinates are at 640 by 480 virtual resolution
==================
*/
void idRenderSystemLocal::DrawSmallStringExt( int x, int y, const char *string, const idVec4 &setColor, bool forceColor, const idMaterial *material ) {
	idVec4		color;
	const unsigned char	*s;
	int			xx;

	// draw the colored text
	s = ( const unsigned char * )string;
	xx = x;
	SetColor( setColor );
	while ( *s ) {
		if ( idStr::IsColor( ( const char * )s ) ) {
			if ( !forceColor ) {
				if ( *( s + 1 ) == C_COLOR_DEFAULT ) {
					SetColor( setColor );
				} else {
					color = idStr::ColorForIndex( *( s + 1 ) );
					color[3] = setColor[3];
					SetColor( color );
				}
			}
			s += 2;
			continue;
		}
		DrawSmallChar( xx, y, *s, material );
		xx += SMALLCHAR_WIDTH;
		s++;
	}
	SetColor( colorWhite );
}

/*
=====================
idRenderSystemLocal::DrawBigChar
=====================
*/
void idRenderSystemLocal::DrawBigChar( int x, int y, int ch, const idMaterial *material ) {
	int row, col;
	float frow, fcol;
	float size;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -BIGCHAR_HEIGHT ) {
		return;
	}

	row = ch >> 4;
	col = ch & 15;

	frow = row * 0.0625f;
	fcol = col * 0.0625f;
	size = 0.0625f;

	DrawStretchPic( x, y, BIGCHAR_WIDTH, BIGCHAR_HEIGHT,
	                fcol, frow,
	                fcol + size, frow + size,
	                material );
}

/*
==================
idRenderSystemLocal::DrawBigString[Color]

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.

Coordinates are at 640 by 480 virtual resolution
==================
*/
void idRenderSystemLocal::DrawBigStringExt( int x, int y, const char *string, const idVec4 &setColor, bool forceColor, const idMaterial *material ) {
	idVec4		color;
	const char	*s;
	int			xx;

	// draw the colored text
	s = string;
	xx = x;
	SetColor( setColor );
	while ( *s ) {
		if ( idStr::IsColor( s ) ) {
			if ( !forceColor ) {
				if ( *( s + 1 ) == C_COLOR_DEFAULT ) {
					SetColor( setColor );
				} else {
					color = idStr::ColorForIndex( *( s + 1 ) );
					color[3] = setColor[3];
					SetColor( color );
				}
			}
			s += 2;
			continue;
		}
		DrawBigChar( xx, y, *s, material );
		xx += BIGCHAR_WIDTH;
		s++;
	}
	SetColor( colorWhite );
}

//======================================================================================

/*
====================
BeginFrame
====================
*/
void idRenderSystemLocal::BeginFrame( int windowWidth, int windowHeight ) {
	setBufferCommand_t	*cmd;

	if ( !glConfig.isInitialized ) {
		return;
	}

	guiModel->Clear();

	// for the larger-than-window tiled rendering screenshots
	if ( tiledViewport[0] ) {
		windowWidth = tiledViewport[0];
		windowHeight = tiledViewport[1];
	}
	glConfig.vidWidth = windowWidth;
	glConfig.vidHeight = windowHeight;

	renderCrops[0].x = 0;
	renderCrops[0].y = 0;

/*	if ( r_useFbo.GetBool() ) { // duzenko #4425: allow virtual resolution
		renderCrops[0].width = windowWidth * r_fboResolution.GetFloat();
		renderCrops[0].height = windowHeight * r_fboResolution.GetFloat();
	} else */{
		renderCrops[0].width = windowWidth;
		renderCrops[0].height = windowHeight;
	}
	currentRenderCrop = 0;

	// screenFraction is just for quickly testing fill rate limitations
	if ( r_screenFraction.GetInteger() != 100 ) {
		int	w = SCREEN_WIDTH * r_screenFraction.GetInteger() / 100.0f;
		int h = SCREEN_HEIGHT * r_screenFraction.GetInteger() / 100.0f;
		CropRenderSize( w, h );
	}

	// this is the ONLY place this is modified
	frameCount++;

	// just in case we did a common->Error while this
	// was set
	guiRecursionLevel = 0;

	// set the time for shader effects in 2D rendering
	frameShaderTime = eventLoop->Milliseconds() * 0.001;

	//
	// draw buffer stuff
	//
	cmd = ( setBufferCommand_t * )R_GetCommandBuffer( sizeof( *cmd ) );
	cmd->commandId = RC_SET_BUFFER;
	cmd->frameCount = frameCount;

	if ( r_frontBuffer.GetBool() ) {
		cmd->buffer = ( int )GL_FRONT;
	} else {
		cmd->buffer = ( int )GL_BACK;
	}
}

void idRenderSystemLocal::WriteDemoPics() {
	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_GUI_MODEL );
	guiModel->WriteToDemo( session->writeDemo );
}

void idRenderSystemLocal::DrawDemoPics() {
	demoGuiModel->EmitFullScreen();
}

/*
=============
EndFrame

Returns the number of msec spent in the back end
=============
*/
void idRenderSystemLocal::EndFrame( int *frontEndMsec, int *backEndMsec ) {
	static idFile *smpTimingsLogFile = nullptr;

	if ( !glConfig.isInitialized ) {
		return;
	}

	try {
		ProfilingBeginFrame();
		common->SetErrorIndirection( true );
		vr->BeginFrame();
		double startLoop = Sys_GetClockTicks();
		session->ActivateFrontend();
		double endSignal = Sys_GetClockTicks();
		frameBuffers->BeginFrame();
		// start the back end up again with the new command list
		R_IssueRenderCommands( backendFrameData );
		vr->EndFrame();
		renderBackend->EndFrame();
		double endRender = Sys_GetClockTicks();
		session->WaitForFrontendCompletion();
		double endWait = Sys_GetClockTicks();
		common->SetErrorIndirection( false );
		ProfilingEndFrame();

		if ( r_logSmpTimings.GetBool() ) {
			if ( !smpTimingsLogFile ) {
				idStr fileName;
				uint64_t currentTime = Sys_GetTimeMicroseconds();
				sprintf( fileName, "smp_timings_%.20llu.txt", currentTime );
				smpTimingsLogFile = fileSystem->OpenFileWrite( fileName, "fs_savepath", "" );
			}
			const double TO_MICROS = 1000000 / Sys_ClockTicksPerSecond();
			static double lastEndTime = Sys_GetClockTicks();
			double signalFrontend = ( endSignal - startLoop ) * TO_MICROS;
			double render = ( endRender - endSignal ) * TO_MICROS;
			double waitForFrontend = ( endWait - endRender ) * TO_MICROS;
			double framePrep = ( startLoop - lastEndTime ) * TO_MICROS;
			double totalFrameTime = ( endWait - lastEndTime ) * TO_MICROS;
			lastEndTime = endWait;

			smpTimingsLogFile->Printf( "Frame %.7d: preparation %.2f - total frame time %.2f us\n", frameCount, framePrep, totalFrameTime );
			smpTimingsLogFile->Printf( "  Backend: signal frontend %.2f us - render %.2f us - wait for frontend %.2f us\n", signalFrontend, render, waitForFrontend );
			session->LogFrontendTimings( *smpTimingsLogFile );
		}
	} catch ( std::shared_ptr<ErrorReportedException> e ) {
		session->WaitForFrontendCompletion();
		common->SetErrorIndirection( false );
		if ( e->IsFatalError() )
		{ common->DoFatalError( e->ErrorMessage(), e->ErrorCode() ); }
		else
		{ common->DoError( e->ErrorMessage(), e->ErrorCode() ); }
	}

	session->ExecuteDelayedFrameCommands();

	// check for dynamic changes that require some initialization
	R_CheckCvars();

#ifdef DEBUG
	// check for errors
	GL_CheckErrors();
#endif

	// use the other buffers next frame, because another CPU
	// may still be rendering into the current buffers
	R_ToggleSmpFrame();

	// we can now release the vertexes used this frame
	vertexCache.EndFrame();

	if ( session->writeDemo ) {
		session->writeDemo->WriteInt( DS_RENDER );
		session->writeDemo->WriteInt( DC_END_FRAME );
		if ( r_showDemo.GetBool() ) {
			common->Printf( "write DC_END_FRAME\n" );
		}
	}

	// save out timing information
	if ( frontEndMsec ) {
		*frontEndMsec = pc.frontEndMsec;
	}
	if ( backEndMsec ) {
		*backEndMsec = backEnd.pc.msec;
	}

	// print any other statistics and clear all of them
	R_PerformanceCounters();
}

/*
=====================
RenderViewToViewport

Converts from SCREEN_WIDTH / SCREEN_HEIGHT coordinates to current cropped pixel coordinates
=====================
*/
void idRenderSystemLocal::RenderViewToViewport( const renderView_t &renderView, idScreenRect &viewport ) {
	renderCrop_t &rc = renderCrops[currentRenderCrop];

	float wRatio = ( float ) rc.width / SCREEN_WIDTH;
	float hRatio = ( float ) rc.height / SCREEN_HEIGHT;

	viewport.x1 = idMath::Ftoi( rc.x + renderView.x * wRatio );
	viewport.x2 = idMath::Ftoi( rc.x + floor( ( renderView.x + renderView.width ) * wRatio + 0.5f ) - 1 );
	viewport.y1 = idMath::Ftoi( ( rc.y + rc.height ) - floor( ( renderView.y + renderView.height ) * hRatio + 0.5f ) );
	viewport.y2 = idMath::Ftoi( ( rc.y + rc.height ) - floor( renderView.y * hRatio + 0.5f ) - 1 );
}

static int RoundDownToPowerOfTwo( int v ) {
	int	i;

	for ( i = 0 ; i < 20 ; i++ ) {
		if ( ( 1 << i ) == v ) {
			return v;
		}
		if ( ( 1 << i ) > v ) {
			return 1 << ( i - 1 );
		}
	}
	return 1 << i;
}

/*
================
CropRenderSize

This automatically halves sizes until it fits in the current window size,
so if you specify a power of two size for a texture copy, it may be shrunk
down, but still valid.
================
*/
void	idRenderSystemLocal::CropRenderSize( int width, int height, bool makePowerOfTwo, bool forceDimensions ) {
	if ( !glConfig.isInitialized )
	{ return; }

	// close any gui drawing before changing the size
	guiModel->EmitFullScreen();
	guiModel->Clear();

	if ( width < 1 || height < 1 )
	{ common->Error( "CropRenderSize: bad sizes" ); }

	if ( session->writeDemo ) {
		session->writeDemo->WriteInt( DS_RENDER );
		session->writeDemo->WriteInt( DC_CROP_RENDER );
		session->writeDemo->WriteInt( width );
		session->writeDemo->WriteInt( height );
		session->writeDemo->WriteInt( makePowerOfTwo );

		if ( r_showDemo.GetBool() )
		{ common->Printf( "write DC_CROP_RENDER\n" ); }
	}

	// convert from virtual SCREEN_WIDTH/SCREEN_HEIGHT coordinates to physical OpenGL pixels
	renderView_t renderView;
	renderView.x = 0;
	renderView.y = 0;
	renderView.width = width;
	renderView.height = height;

	idScreenRect	r;
	RenderViewToViewport( renderView, r );

	width = r.x2 - r.x1 + 1;
	height = r.y2 - r.y1 + 1;

	if ( forceDimensions ) {
		// just give exactly what we ask for
		width = renderView.width;
		height = renderView.height;
	}

#if 0 // duzenko 5068, remove the makePowerOfTwo param after 2.08 release
	// if makePowerOfTwo, drop to next lower power of two after scaling to physical pixels
	if ( makePowerOfTwo ) {
		width = RoundDownToPowerOfTwo( width );
		height = RoundDownToPowerOfTwo( height );
		// FIXME: megascreenshots with offset viewports don't work right with this yet
	}
#endif

	// we might want to clip these to the crop window instead
	while ( width > glConfig.vidWidth )
	{ width >>= 1; }
	while ( height > glConfig.vidHeight )
	{ height >>= 1; }

	if ( currentRenderCrop == MAX_RENDER_CROPS )
	{ common->Error( "idRenderSystemLocal::CropRenderSize: currentRenderCrop == MAX_RENDER_CROPS" ); }

	currentRenderCrop++;

	renderCrop_t &rc = renderCrops[currentRenderCrop];
	rc.x = 0;
	rc.y = 0;
	rc.width = width;
	rc.height = height;
}

void idRenderSystemLocal::GetCurrentRenderCropSize( int &width, int &height ) {
	renderCrop_t *rc = &renderCrops[currentRenderCrop];

	width = rc->width;
	height = rc->height;
}

/*
================
UnCrop
================
*/
void idRenderSystemLocal::UnCrop() {
	if ( !glConfig.isInitialized ) {
		return;
	}

	if ( currentRenderCrop < 1 ) {
		common->Error( "idRenderSystemLocal::UnCrop: currentRenderCrop < 1" );
	}

	// close any gui drawing
	guiModel->EmitFullScreen();
	guiModel->Clear();

	currentRenderCrop--;

	if ( session->writeDemo ) {
		session->writeDemo->WriteInt( DS_RENDER );
		session->writeDemo->WriteInt( DC_UNCROP_RENDER );

		if ( r_showDemo.GetBool() ) {
			common->Printf( "write DC_UNCROP\n" );
		}
	}
}

/*
================
PostProcess
================
*/
void idRenderSystemLocal::PostProcess() {
	if ( !r_tonemap ) return;
	bloomCommand_t* cmd = (bloomCommand_t*)R_GetCommandBuffer( sizeof( *cmd ) );
	cmd->commandId = RC_BLOOM;
	cmd->screenRect.Clear();
}

/*
================
CaptureRenderToImage
================
*/
void idRenderSystemLocal::CaptureRenderToImage( idImage &image ) {
	if ( !glConfig.isInitialized ) {
		return;
	}
	guiModel->EmitFullScreen();
	guiModel->Clear();

	if ( session->writeDemo ) {
		session->writeDemo->WriteInt( DS_RENDER );
		session->writeDemo->WriteInt( DC_CAPTURE_RENDER );
		session->writeDemo->WriteHashString( image.imgName );

		if ( r_showDemo.GetBool() )
		{ common->Printf( "write DC_CAPTURE_RENDER: %s\n", image.imgName ); }
	}

	renderCrop_t &rc = renderCrops[currentRenderCrop];

	copyRenderCommand_t &cmd = *( copyRenderCommand_t * )R_GetCommandBuffer( sizeof( cmd ) );
	cmd.commandId = RC_COPY_RENDER;
	cmd.x = rc.x;
	cmd.y = rc.y;
	cmd.imageWidth = rc.width;
	cmd.imageHeight = rc.height;
	cmd.image = &image;
	cmd.buffer = NULL;
}

void idRenderSystemLocal::CaptureRenderToBuffer( unsigned char *buffer, bool usePbo ) {
	if ( session->IsFrontend() ) {
		common->Error( "CaptureRenderToBuffer called from frontend thread, not supported." );
	}
	if ( !glConfig.isInitialized ) { 
		return; 
	}
	renderCrop_t rc = renderCrops[currentRenderCrop];
	/*if ( r_useFbo.GetBool() && !usePbo ) { // 4676 duzenko FIXME usePbo has double function
		rc.width /= r_fboResolution.GetFloat();
		rc.height /= r_fboResolution.GetFloat();
	}*/
	rc.width = ( rc.width + 3 ) & ~3; //opengl wants width padded to 4x

	//guiModel->EmitFullScreen();
	//guiModel->Clear();

	copyRenderCommand_t &cmd = *( copyRenderCommand_t * )R_GetCommandBuffer( sizeof( cmd ) );
	cmd.commandId = RC_COPY_RENDER;
	cmd.buffer = buffer;
	cmd.usePBO = usePbo;
	cmd.image = NULL;
	cmd.x = rc.x;
	cmd.y = rc.y;
	cmd.imageWidth = rc.width;
	cmd.imageHeight = rc.height;

	R_IssueRenderCommands( frameData );
}

/*
==============
CaptureRenderToFile

==============
*/
void idRenderSystemLocal::CaptureRenderToFile( const char *fileName, bool fixAlpha ) {
	if ( session->IsFrontend() ) {
		common->Error( "CaptureRenderToFile called from frontend thread, not supported." );
	}

	if ( !glConfig.isInitialized ) {
		return;
	}
	renderCrop_t *rc = &renderCrops[currentRenderCrop];
	guiModel->EmitFullScreen();
	guiModel->Clear();
	R_IssueRenderCommands( frameData );

	// calculate pitch of buffer that will be returned by qglReadPixels()
	int alignment;
	qglGetIntegerv( GL_PACK_ALIGNMENT, &alignment );

	int pitch = rc->width * 4 + alignment - 1;
	pitch = pitch - pitch % alignment;
	byte *data = ( byte * )R_StaticAlloc( pitch * rc->height );

	// GL_RGBA/GL_UNSIGNED_BYTE seems to be the safest option
	qglReadPixels( rc->x, rc->y, rc->width, rc->height, GL_RGBA, GL_UNSIGNED_BYTE, data );

	byte *data2 = ( byte * )R_StaticAlloc( rc->width * rc->height * 4 );

	for ( int y = 0 ; y < rc->height ; y++ ) {
		for ( int x = 0 ; x < rc->width ; x++ ) {
			int idx = y * pitch + x * 4;
			int idx2 = ( y * rc->width + x ) * 4;

			data2[ idx2 + 0 ] = data[ idx + 0 ];
			data2[ idx2 + 1 ] = data[ idx + 1 ];
			data2[ idx2 + 2 ] = data[ idx + 2 ];
			data2[ idx2 + 3 ] = 0xff;
		}
	}
	R_WriteTGA( fileName, data2, rc->width, rc->height, true );

	R_StaticFree( data );
	R_StaticFree( data2 );
}

/*
==============
AllocRenderWorld
==============
*/
idRenderWorld *idRenderSystemLocal::AllocRenderWorld() {
	idRenderWorldLocal *rw;
	rw = new idRenderWorldLocal;
	worlds.Append( rw );
	return rw;
}

/*
==============
FreeRenderWorld
==============
*/
void idRenderSystemLocal::FreeRenderWorld( idRenderWorld *rw ) {
	if ( primaryWorld == rw ) {
		primaryWorld = NULL;
	}
	worlds.Remove( static_cast<idRenderWorldLocal *>( rw ) );
	delete rw;
}

/*
==============
PrintMemInfo
==============
*/
void idRenderSystemLocal::PrintMemInfo( MemInfo_t *mi ) {
	// sum up image totals
	globalImages->PrintMemInfo( mi );

	// sum up model totals
	renderModelManager->PrintMemInfo( mi );

	// compute render totals

}

/*
===============
idRenderSystemLocal::UploadImage
===============
*/
bool idRenderSystemLocal::UploadImage( const char *imageName, const byte *data, int width, int height ) {
	idImage *image = globalImages->GetImage( imageName );
	if ( !image ) {
		return false;
	}
	image->UploadScratch( data, width, height );
	image->SetImageFilterAndRepeat();
	return true;
}
