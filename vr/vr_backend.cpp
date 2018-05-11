#include "precompiled.h"
#include "VrSupport.h"
#include "Framebuffer.h"
#include "../renderer/tr_local.h"
#include "../renderer/gl4/OpenGL4Renderer.h"
#pragma hdrstop

idCVar vr_msaa( "vr_msaa", "1", CVAR_RENDERER | CVAR_INTEGER, "set MSAA value for VR rendering" );

Framebuffer * stereoEyeFBOs[2]; 
idImage * stereoEyeImages[2];
Framebuffer *stereoRenderFBO;
Framebuffer *stereoArrayAccessFBO[2];

const void	RB_CopyRender( const void *data );

/*
====================
R_MakeStereoRenderImage
====================
*/
static void R_MakeStereoRenderImage( idImage* image )
{
	uint32_t width, height;
	vrSupport->DetermineRenderTargetSize( &width, &height );
	common->Printf( "OpenVR: Render texture size: %d x %d\n", width, height );
	image->AllocImage( width, height, TF_LINEAR, TR_CLAMP, TD_HIGH_QUALITY );
}

void RB_CreateStereoEyeFBO( int eye, Framebuffer*& framebuffer, idImage*& renderImage, Framebuffer*& arrayAccessFbo ) {
	renderImage = globalImages->ImageFromFunction( va("_stereoRender%d", eye), R_MakeStereoRenderImage );
	uint32_t width, height;
	vrSupport->DetermineRenderTargetSize( &width, &height );
	framebuffer = new Framebuffer( va("_stereoEyeFBO%d", eye), width, height );
	framebuffer->Bind();
	framebuffer->AddColorImage( renderImage, 0 );
	framebuffer->Check();

	arrayAccessFbo = new Framebuffer( va( "_stereoArrayAccessFBO%d", eye ), width, height );
	arrayAccessFbo->Bind();
	arrayAccessFbo->AddColorImageLayer( stereoRenderFBO->GetStereoColorArray(), eye, 0 );
	arrayAccessFbo->Check();
}

void RB_CreateStereoRenderFBO(Framebuffer*& framebuffer) {
	uint32_t width, height;
	vrSupport->DetermineRenderTargetSize( &width, &height );
	framebuffer = new Framebuffer( "_stereoRenderFBO", width, height );
	framebuffer->Bind();
	framebuffer->AddStereoColorArray( vr_msaa.GetInteger() );
	framebuffer->AddStereoDepthStencilArray( vr_msaa.GetInteger() );
	framebuffer->Check();
}

void RB_CopyStereoArrayToEyeTextures() {
	for( int i = 0; i < 2; ++i ) {
		qglBindFramebuffer( GL_READ_FRAMEBUFFER, stereoArrayAccessFBO[i]->GetFbo() );
		qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, stereoEyeFBOs[i]->GetFbo() );
		qglBlitFramebuffer( 0, 0, stereoArrayAccessFBO[i]->GetWidth(), stereoArrayAccessFBO[i]->GetHeight(), 0, 0, stereoEyeFBOs[i]->GetWidth(), stereoEyeFBOs[i]->GetHeight(), GL_COLOR_BUFFER_BIT, GL_LINEAR );
	}
}

void WaitForGPUFinish() {
	GLsync fenceSync = qglFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
	GLenum result = qglClientWaitSync( fenceSync, 0, 1 );
	while( result != GL_ALREADY_SIGNALED && result != GL_CONDITION_SATISFIED ) {
		result = qglClientWaitSync( fenceSync, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000 );
		if( result == GL_WAIT_FAILED ) {
			common->Warning( "glClientWaitSync failed.\n" );
			break;
		}
	}
	qglDeleteSync( fenceSync );
}

/*
====================
RB_ExecuteBackEndCommandsStereo

Called if VR support is enabled.
====================
*/
void RB_ExecuteBackEndCommandsStereo( const emptyCommand_t* allcmds ) {
	// create the stereoRenderFBOs if we haven't already
	if( stereoRenderFBO == nullptr ) {
		RB_CreateStereoRenderFBO( stereoRenderFBO );
	}
	for( int i = 0; i < 2; ++i ) {
		if( stereoEyeFBOs[i] == nullptr ) {
			RB_CreateStereoEyeFBO( i, stereoEyeFBOs[i], stereoEyeImages[i], stereoArrayAccessFBO[i] );
		}
	}

	const emptyCommand_t* cmds = allcmds;

	primaryFramebuffer = stereoRenderFBO;

	RB_SetDefaultGLState();

	while( cmds ) {
		switch( cmds->commandId ) {
		case RC_NOP:
			break;
		case RC_DRAW_VIEW:
		{
			const drawSurfsCommand_t* const dsc = ( const drawSurfsCommand_t* )cmds;
			viewDef_t& eyeViewDef = *dsc->viewDef;

			RB_DrawView( cmds );
			break;
		}
		case RC_SET_BUFFER:
			//RB_SetBuffer( cmds );
			break;
		case RC_BLOOM:
			//RB_Bloom();
			break;
		case RC_COPY_RENDER:
			//RB_CopyRender( cmds );
			break;
		case RC_SWAP_BUFFERS:
			break;
		default:
			common->Error( "RB_ExecuteBackEndCommandsStereo: bad commandId" );
			break;
		}
		cmds = ( const emptyCommand_t * )cmds->next;
	}

	RB_CopyStereoArrayToEyeTextures();

	// mirror one of the eyes to the screen
	stereoEyeFBOs[0]->Bind();
	qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
	qglBlitFramebuffer( 0, 0, glConfig.vidWidth, glConfig.vidHeight, 0, 0, glConfig.windowWidth, glConfig.windowHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR );
	GLimp_SwapBuffers();

	//WaitForGPUFinish();
	vrSupport->FrameEnd( stereoEyeImages[0], stereoEyeImages[1] );
}
