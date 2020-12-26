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

#ifdef WIN32
#include "OpenXRSwapchainDX.h"
#include "OpenXRBackend.h"
#include "xr_loader.h"
#include "../FrameBuffer.h"
#include "../FrameBufferManager.h"
#include "../Profiling.h"

void OpenXRSwapchainDX::Init( const idStr &name, int64_t format, int width, int height ) {
	if ( swapchain != nullptr ) {
		Destroy();
	}

	this->width = width;
	this->height = height;

	D3D11_TEXTURE2D_DESC td;
	td.Width = width;
	td.Height = height;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.ArraySize = 1;
	td.Format = (DXGI_FORMAT)format;
	td.MipLevels = 1;
	td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	td.CPUAccessFlags = 0;
	td.MiscFlags = 0;
	td.SampleDesc.Count = 1;
	td.SampleDesc.Quality = 0;
	if ( FAILED ( d3d11Helper->device->CreateTexture2D( &td, nullptr, texture.GetAddressOf() ) ) ) {
		common->Error( "Failed to create DX texture" );
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srv;
	srv.Format = td.Format;
	srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MipLevels = 1;
	srv.Texture2D.MostDetailedMip = 0;
	if ( FAILED( d3d11Helper->device->CreateShaderResourceView( texture.Get(), &srv, shaderResourceView.GetAddressOf() ) ) ) {
		common->Error( "Could not create shader resource view" );
	}

	GLuint texnum;
	qglGenTextures( 1, &texnum );
	handle = qwglDXRegisterObjectNV( d3d11Helper->glDeviceHandle, texture.Get(), texnum, GL_TEXTURE_2D, WGL_ACCESS_READ_WRITE_NV );
	if ( handle == nullptr ) {
		GLenum error = qglGetError();
		common->Error( "Could not acquire handle for DX texture: %d", error );
	}

	image = globalImages->ImageFromFunction( idStr::Fmt( "%s, image", name.c_str()), FB_RenderTexture );
	frameBuffer = frameBuffers->CreateFromGenerator( idStr::Fmt( "%s fb", name.c_str() ), 
		[=](FrameBuffer *fbo) { InitFrameBuffer( fbo, image, texnum, width, height ); }  );

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
	XrResult result = xrCreateSwapchain( xrBackend->Session(), &createInfo, &swapchain );
	XR_CheckResult( result, "creating swapchain", xrBackend->Instance() );

	// enumerate number of images in the swapchain
	uint32_t imageCount = 0;
	result = xrEnumerateSwapchainImages( swapchain, 0, &imageCount, nullptr );
	XR_CheckResult( result, "enumerating swapchain images", xrBackend->Instance() );
	idList<XrSwapchainImageD3D11KHR> swapchainImages;
	swapchainImages.SetNum( imageCount );
	for ( int i = 0; i < swapchainImages.Num(); ++i ) {
		swapchainImages[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
		swapchainImages[i].next = nullptr;
	}
	result = xrEnumerateSwapchainImages( swapchain, swapchainImages.Num(), &imageCount,
		reinterpret_cast<XrSwapchainImageBaseHeader *>( swapchainImages.Ptr() ) );
	XR_CheckResult( result, "enumerating swapchain images", xrBackend->Instance() );

	swapchainTextures.SetNum( imageCount );
	renderTargetViews.SetNum( imageCount );
	for ( int i = 0; i < imageCount; ++i ) {
		swapchainTextures[i] = swapchainImages[i].texture;
		D3D11_RENDER_TARGET_VIEW_DESC rtv;
		rtv.Format = (DXGI_FORMAT)format;
		rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtv.Texture2D.MipSlice = 0;
		if ( FAILED ( d3d11Helper->device->CreateRenderTargetView( swapchainTextures[i], &rtv, renderTargetViews[i].GetAddressOf() ) ) ) {
			common->Error( "Failed to create render target view for texture" );
		}
	}
}

void OpenXRSwapchainDX::Destroy() {
	if ( swapchain == nullptr ) {
		return;
	}

	frameBuffer->Destroy();
	frameBuffer = nullptr;
	image->PurgeImage();
	image->texnum = idImage::TEXTURE_NOT_LOADED;
	image = nullptr;
	qwglDXUnregisterObjectNV( d3d11Helper->glDeviceHandle, handle );
	handle = nullptr;
	shaderResourceView.Reset();
	texture.Reset();
	renderTargetViews.Clear();
	swapchainTextures.Clear();

	xrDestroySwapchain( swapchain );
	swapchain = nullptr;
	curIndex = INVALID_INDEX;
}

void OpenXRSwapchainDX::PrepareNextImage() {
	if ( curIndex != INVALID_INDEX ) {
		common->Printf( "Xr: Acquiring swapchain that was not released\n" );
		ReleaseImage();
	}

	XrResult result = xrAcquireSwapchainImage( swapchain, nullptr, &curIndex );
	XR_CheckResult( result, "acquiring swapchain image", xrBackend->Instance() );

	XrSwapchainImageWaitInfo waitInfo = {
		XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
		nullptr,
		1000000000,
	};
	result = xrWaitSwapchainImage( swapchain, &waitInfo );
	XR_CheckResult( result, "awaiting swapchain image", xrBackend->Instance() );

	currentImage.imageArrayIndex = 0;
	currentImage.swapchain = swapchain;
	currentImage.imageRect = { {0, 0}, {width, height} };

	if ( !qwglDXLockObjectsNV( d3d11Helper->glDeviceHandle, 1, &handle ) ) {
		common->Error( "Could not lock DX texture" );
	}
}

void OpenXRSwapchainDX::ReleaseImage() {
	if ( curIndex == INVALID_INDEX ) {
		common->Printf( "Xr: Releasing swapchain that was not acquired\n" );
		return;
	}

	if ( !qwglDXUnlockObjectsNV( d3d11Helper->glDeviceHandle, 1, &handle ) ) {
		common->Error( "Could not unlock DX texture" );
	}

	d3d11Helper->RenderTextureFlipped( shaderResourceView.Get(), renderTargetViews[curIndex].Get(), width, height );

	XrResult result = xrReleaseSwapchainImage( swapchain, nullptr );
	XR_CheckResult( result, "releasing swapchain image", xrBackend->Instance() );
	curIndex = INVALID_INDEX;
}

FrameBuffer * OpenXRSwapchainDX::CurrentFrameBuffer() const {
	if ( curIndex == INVALID_INDEX ) {
		common->Error( "OpenXRSwapchain: no image currently acquired when attempting to access framebuffer" );
	}

	return frameBuffer;
}

idImage * OpenXRSwapchainDX::CurrentImage() const {
	if ( curIndex == INVALID_INDEX ) {
		common->Error( "OpenXRSwapchain: no image currently acquired when attempting to access image" );
	}

	return image;
}

void OpenXRSwapchainDX::InitFrameBuffer( FrameBuffer *fbo, idImage *image, GLuint texnum, int width, int height ) {
	image->texnum = texnum;
	image->uploadWidth = width;
	image->uploadHeight = height;
	GL_SetDebugLabel( GL_TEXTURE, texnum, image->imgName );
	fbo->Init( width, height );
	fbo->AddColorRenderTexture( 0, image );
}

#endif
