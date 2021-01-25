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
#include "OpenXRSwapchainGL.h"

#include "OpenXRBackend.h"
#include "xr_loader.h"
#include "../FrameBuffer.h"
#include "../FrameBufferManager.h"
#include "../Image.h"
#include "../Profiling.h"

void OpenXRSwapchainGL::Init( const idStr &name, int64_t format, int width, int height ) {
	if ( swapchain != nullptr ) {
		Destroy();
	}

	this->width = width;
	this->height = height;

	XrSwapchainCreateInfo createInfo = {
		XR_TYPE_SWAPCHAIN_CREATE_INFO,
		nullptr,
		0,
		XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT,
		format,
		1,
		width,
		height,
		1,
		1,
		1,
	};
	XrResult result = xrCreateSwapchain( vrBackend->Session(), &createInfo, &swapchain );
	XR_CheckResult( result, "creating swapchain", vrBackend->Instance() );

	// enumerate number of images in the swapchain
	uint32_t imageCount = 0;
	result = xrEnumerateSwapchainImages( swapchain, 0, &imageCount, nullptr );
	XR_CheckResult( result, "enumerating swapchain images", vrBackend->Instance() );
	idList<XrSwapchainImageOpenGLKHR> swapchainImages;
	swapchainImages.SetNum( imageCount );
	for ( int i = 0; i < swapchainImages.Num(); ++i ) {
		swapchainImages[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
		swapchainImages[i].next = nullptr;
	}
	result = xrEnumerateSwapchainImages( swapchain, swapchainImages.Num(), &imageCount,
		reinterpret_cast<XrSwapchainImageBaseHeader *>( swapchainImages.Ptr() ) );
	XR_CheckResult( result, "enumerating swapchain images", vrBackend->Instance() );

	// prepare framebuffer objects for images
	images.SetNum( imageCount );
	frameBuffers.SetNum( imageCount );
	for ( int i = 0; i < imageCount; ++i ) {
		images[i] = globalImages->ImageFromFunction( idStr::Fmt( "%s, image %d", name.c_str(), i), FB_RenderTexture );
		frameBuffers[i] = ::frameBuffers->CreateFromGenerator( idStr::Fmt( "%s fb %d", name.c_str(), i ), 
			[=](FrameBuffer *fbo) { InitFrameBuffer( fbo, images[i], swapchainImages[i].image, width, height ); }  );
	}
}

void OpenXRSwapchainGL::Destroy() {
	if ( swapchain == nullptr ) {
		return;
	}

	for ( FrameBuffer *fbo : frameBuffers ) {
		fbo->Destroy();
	}
	frameBuffers.Clear();
	for ( idImage *image : images ) {
		image->texnum = idImage::TEXTURE_NOT_LOADED;
		image->PurgeImage();
	}
	images.Clear();

	xrDestroySwapchain( swapchain );
	swapchain = nullptr;
	curIndex = INVALID_INDEX;
}

void OpenXRSwapchainGL::PrepareNextImage() {
	if ( curIndex != INVALID_INDEX ) {
		common->Printf( "Xr: Acquiring swapchain that was not released\n" );
		ReleaseImage();
	}

	XrResult result = xrAcquireSwapchainImage( swapchain, nullptr, &curIndex );
	XR_CheckResult( result, "acquiring swapchain image", vrBackend->Instance() );

	XrSwapchainImageWaitInfo waitInfo = {
		XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
		nullptr,
		1000000000,
	};
	result = xrWaitSwapchainImage( swapchain, &waitInfo );
	XR_CheckResult( result, "awaiting swapchain image", vrBackend->Instance() );

	currentImage.imageArrayIndex = 0;
	currentImage.swapchain = swapchain;
	currentImage.imageRect = { {0, 0}, {width, height} };
}

void OpenXRSwapchainGL::ReleaseImage() {
	if ( curIndex == INVALID_INDEX ) {
		common->Printf( "Xr: Releasing swapchain that was not acquired\n" );
		return;
	}

	// SteamVR runtime currently picks up uncleared GL errors and returns runtime errors, so make sure
	// the GL error is empty
	qglGetError();

	XrResult result = xrReleaseSwapchainImage( swapchain, nullptr );
	XR_CheckResult( result, "releasing swapchain image", vrBackend->Instance() );
	curIndex = INVALID_INDEX;
}

FrameBuffer * OpenXRSwapchainGL::CurrentFrameBuffer() const {
	if ( curIndex == INVALID_INDEX ) {
		common->Error( "OpenXRSwapchain: no image currently acquired when attempting to access framebuffer" );
	}

	return frameBuffers[curIndex];
}

idImage * OpenXRSwapchainGL::CurrentImage() const {
	if ( curIndex == INVALID_INDEX ) {
		common->Error( "OpenXRSwapchain: no image currently acquired when attempting to access image" );
	}

	return images[curIndex];
}

void OpenXRSwapchainGL::InitFrameBuffer( FrameBuffer *fbo, idImage *image, GLuint texnum, int width, int height ) {
	image->texnum = texnum;
	image->uploadWidth = width;
	image->uploadHeight = height;
	GL_SetDebugLabel( GL_TEXTURE, texnum, image->imgName );
	fbo->Init( width, height );
	fbo->AddColorRenderTexture( 0, image );
}
