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
#include "OpenXRSwapchain.h"
#include <d3d11.h>
#include <wrl/client.h>

#ifdef WIN32

/*
 * Unfortunately, SteamVR's OpenXR implementation's OpenGL support is not ideal.
 * In particular, it causes serious performance issues when bindless textures are
 * used due to multiple GL context switches during frame submission.
 * Therefore, we use a D3D11 swapchain as a workaround and use OGL -> DX interop.
 */
class OpenXRSwapchainDX : public OpenXRSwapchain {
public:
	OpenXRSwapchainDX( ID3D11Device *device, ID3D11DeviceContext *context, HANDLE glDeviceHandle ) : device( device ), context( context ), glDeviceHandle( glDeviceHandle ) {}

	void Init( const idStr &name, int64_t format, int width, int height ) override;
	void Destroy() override;

	void PrepareNextImage() override;

	void ReleaseImage() override;

	FrameBuffer *CurrentFrameBuffer() const override;
	idImage *CurrentImage() const override;

	const XrSwapchainSubImage &CurrentSwapchainSubImage() const override { return currentImage; }

private:
	static const uint32_t INVALID_INDEX = 0xffffffff;

	ID3D11Device *device;
	ID3D11DeviceContext *context;
	HANDLE glDeviceHandle;

	int width;
	int height;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
	idImage * image;
	FrameBuffer * frameBuffer;
	HANDLE handle;
	XrSwapchain swapchain = nullptr;
	idList<ID3D11Texture2D*> swapchainTextures;
	uint32_t curIndex = INVALID_INDEX;
	XrSwapchainSubImage currentImage;

	void InitFrameBuffer( FrameBuffer *fbo, idImage *image, GLuint texnum, int width, int height );
	
};

#endif
