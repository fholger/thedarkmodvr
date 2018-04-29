#include "precompiled.h"
#include "VrSupport.h"
#include "Framebuffer.h"
#include "../renderer/tr_local.h"
#include "../renderer/gl4/OpenGL4Renderer.h"
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

	for (int stereoEye = -1; stereoEye <= 1; stereoEye += 2) {
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
				if (eyeViewDef.renderView.viewEyeBuffer != 0)
					eyeViewDef.renderView.viewEyeBuffer = stereoEye;

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
	qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
	qglBlitFramebuffer( 0, 0, glConfig.vidWidth, glConfig.vidHeight, 0, 0, glConfig.windowWidth, glConfig.windowHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR );
	GLimp_SwapBuffers();

	vrSupport->FrameEnd( stereoRenderImages[0], stereoRenderImages[1] );
}
