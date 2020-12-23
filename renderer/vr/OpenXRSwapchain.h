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

class OpenXRSwapchain {
public:
	virtual void Init( const idStr &name, int64_t format, int width, int height ) = 0;
	virtual void Destroy() = 0;

	virtual ~OpenXRSwapchain() {}

	/* Must call this before rendering to a framebuffer of this swapchain.
	 * Will acquire and wait for the next image in the swapchain to be ready
	 * for rendering.
	 */
	virtual void PrepareNextImage() = 0;

	// call when rendering is done to release the image
	virtual void ReleaseImage() = 0;

	// returns the currently acquired FrameBuffer for rendering
	virtual FrameBuffer *CurrentFrameBuffer() const = 0;
	// returns the currently acquired Image for rendering
	virtual idImage *CurrentImage() const = 0;

	virtual const XrSwapchainSubImage &CurrentSwapchainSubImage() const = 0;
};
