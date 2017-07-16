#include "precompiled_engine.h"
#include "../vr/VrSupport.h"
#include "../renderer/tr_local.h"
#pragma hdrstop

Framebuffer * stereoRenderFBOs[2]; 
idImage * stereoRenderImages[2];

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

void RB_CreateStereoRenderFBO( int eye, Framebuffer*& framebuffer, idImage*& renderImage ) {
	renderImage = globalImages->ImageFromFunction( va( "_stereoRender%d", eye ), R_MakeStereoRenderImage );
	uint32_t width, height;
	vrSupport->DetermineRenderTargetSize( &width, &height );
	framebuffer = new Framebuffer( va( "_stereoRenderFBO%d", eye ), width, height );
	framebuffer->Bind();
	framebuffer->AddColorImage( renderImage, 0 );
	framebuffer->AddDepthStencilBuffer( GL_DEPTH24_STENCIL8 );
	framebuffer->Check();
}

void RB_DisplayEyeView( idImage* image ) {
	primaryFramebuffer = nullptr;
	//RB_SetDefaultGLState();
	Framebuffer::BindPrimary();
	qglViewport( 0, 0, 1280, 1024 );
	qglScissor( 0, 0, 1280, 1024 );

	qglMatrixMode( GL_PROJECTION );
	qglLoadIdentity();
	qglOrtho( 0, 1, 0, 1, -1, 1 );
	qglMatrixMode( GL_MODELVIEW );
	qglLoadIdentity();

	qglClear( GL_COLOR_BUFFER_BIT );
	image->Bind();
	qglEnable( GL_TEXTURE_2D );

	// Draw a textured quad
	// FIXME: something about the view / transformation is not quite right
	qglBegin( GL_QUADS );
	qglTexCoord2f( 0, 0 ); qglVertex2f( 0, 0 );
	qglTexCoord2f( 0, 1 ); qglVertex2f( 0, 1 );
	qglTexCoord2f( 1, 1 ); qglVertex2f( 1, 1 );
	qglTexCoord2f( 1, 0 ); qglVertex2f( 1, 0 );
	qglEnd();

	qglDisable( GL_TEXTURE_2D );
	qglBindTexture( GL_TEXTURE_2D, 0 );
	qglFlush();
}

/*
====================
RB_ExecuteBackEndCommandsStereo

Called if VR support is enabled.
====================
*/
void RB_ExecuteBackEndCommandsStereo( const emptyCommand_t* allcmds ) {
	// create the stereoRenderFBOs if we haven't already
	for (int i = 0; i < 2; ++i) {
		if (stereoRenderFBOs[i] == nullptr) {
			RB_CreateStereoRenderFBO( i, stereoRenderFBOs[i], stereoRenderImages[i] );
		}
	}

	// FIXME: this is a hack
	glConfig.vidWidth = stereoRenderFBOs[0]->GetWidth();
	glConfig.vidHeight = stereoRenderFBOs[0]->GetHeight();

	for (int stereoEye = 1; stereoEye >= -1; stereoEye -= 2) {
		const int targetEye = stereoEye == RIGHT_EYE ? 1 : 0;
		const emptyCommand_t* cmds = allcmds;

		primaryFramebuffer = stereoRenderFBOs[targetEye];

		RB_SetDefaultGLState();

		while (cmds) {
			switch (cmds->commandId) {
			case RC_NOP:
				break;
			case RC_DRAW_VIEW:
			{
				const drawSurfsCommand_t* const dsc = (const drawSurfsCommand_t*)cmds;
				viewDef_t& eyeViewDef = *dsc->viewDef;

				if (eyeViewDef.renderView.viewEyeBuffer && eyeViewDef.renderView.viewEyeBuffer != stereoEye) {
					// this is the render view for the other eye
					//vrSupport->AdjustViewWithActualHeadPose( &eyeViewDef );
					break;
				}

				RB_DrawView( cmds );
				break;
			}
			case RC_SET_BUFFER:
				//RB_SetBuffer( cmds );
				break;
			case RC_COPY_RENDER:
				RB_CopyRender( cmds );
				break;
			case RC_SWAP_BUFFERS:
				//RB_SwapBuffers( cmds );
				break;
			default:
				common->Error( "RB_ExecuteBackEndCommandsStereo: bad commandId" );
				break;
			}
			cmds = (const emptyCommand_t *)cmds->next;
		}
	}

	// mirror one of the eyes to the screen
	RB_DisplayEyeView( stereoRenderImages[1] );
	GLimp_SwapBuffers();

	vrSupport->FrameEnd( stereoRenderImages[0], stereoRenderImages[1] );
}
