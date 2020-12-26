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

#ifdef WIN32
#include "D3D11Helper.h"

/*
 * Unfortunately, SteamVR's OpenXR implementation's OpenGL support is not ideal.
 * In particular, it causes serious performance issues when bindless textures are
 * used due to multiple GL context switches during frame submission.
 * Therefore, we use a D3D11 swapchain as a workaround and use OGL -> DX interop.
 */
class OpenXRSwapchainDX : public OpenXRSwapchain {
public:
	OpenXRSwapchainDX( D3D11Helper *d3d11Helper ) : d3d11Helper( d3d11Helper ) {}

	void Init( const idStr &name, int64_t format, int width, int height ) override;
	void Destroy() override;

	void PrepareNextImage() override;

	void ReleaseImage() override;

	FrameBuffer *CurrentFrameBuffer() const override;
	idImage *CurrentImage() const override;

	const XrSwapchainSubImage &CurrentSwapchainSubImage() const override { return currentImage; }

private:
	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
	
	static const uint32_t INVALID_INDEX = 0xffffffff;

	D3D11Helper *d3d11Helper;

	int width;
	int height;
	ComPtr<ID3D11Texture2D> texture;
	ComPtr<ID3D11ShaderResourceView> shaderResourceView;
	idImage * image;
	FrameBuffer * frameBuffer;
	HANDLE handle;
	XrSwapchain swapchain = nullptr;
	idList<ID3D11Texture2D*> swapchainTextures;
	idList<ComPtr<ID3D11RenderTargetView>> renderTargetViews;
	uint32_t curIndex = INVALID_INDEX;
	XrSwapchainSubImage currentImage;

	void InitFrameBuffer( FrameBuffer *fbo, idImage *image, GLuint texnum, int width, int height );
	
};

#endif
