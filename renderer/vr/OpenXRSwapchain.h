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
#include <openxr/openxr.h>

class FrameBuffer;
class idImage;

class OpenXRSwapchain {
public:
	void Init( const idStr &name, GLuint format, int width, int height );
	void Destroy();

	/* Must call this before rendering to a framebuffer of this swapchain.
	 * Will acquire and wait for the next image in the swapchain to be ready
	 * for rendering.
	 */
	void PrepareNextImage();

	// call when rendering is done to release the image
	void ReleaseImage();

	// returns the currently acquired FrameBuffer for rendering
	FrameBuffer *CurrentFrameBuffer() const;
	// returns the currently acquired Image for rendering
	idImage *CurrentImage() const;

	const XrSwapchainSubImage &CurrentSwapchainSubImage() const { return currentImage; }

private:
	static const uint32_t INVALID_INDEX = 0xffffffff;
	
	int width;
	int height;
	XrSwapchain swapchain = nullptr;
	idList<idImage *> images;
	idList<FrameBuffer *> frameBuffers;
	uint32_t curIndex = INVALID_INDEX;
	XrSwapchainSubImage currentImage;

	void InitFrameBuffer( FrameBuffer *fbo, idImage *image, GLuint texnum, int width, int height );
};
