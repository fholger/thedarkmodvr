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
#include "tr_local.h"

class FrameBuffer {
public:
	using Generator = std::function<void( FrameBuffer* )>;

	// do not use directly, use FrameBufferManager::CreateFromGenerator
	FrameBuffer(const idStr &name, const Generator &generator);
	~FrameBuffer();

	void Init(int width, int height, int msaa = 1);
	void Destroy();

	void AddColorRenderBuffer(int attachment, GLenum format);
	void AddColorRenderTexture(int attachment, idImage *texture, int mipLevel = 0);
	void AddDepthStencilRenderBuffer(GLenum format);
	void AddDepthStencilRenderTexture(idImage *texture);
	void AddDepthRenderTexture(idImage *texture);
	void AddStencilRenderTexture(idImage *texture);

	void Validate();

	void Bind();
	void BindDraw();

	void BlitTo(FrameBuffer *target, GLbitfield mask, GLenum filter = GL_LINEAR);
	void BlitTo(FrameBuffer *target, uint srcX1, uint srcY1, uint srcX2, uint srcY2, uint dstX1, uint dstY1, uint dstX2, uint dstY2, GLbitfield mask, GLenum filter = GL_LINEAR);

	int Width() const { return width; }
	int Height() const { return height; }
	int MultiSamples() const { return msaa; }

	const char *Name() const { return name.c_str(); }

	bool IsDefault() const { return fbo == 0; }

	static void CreateDefaultFrameBuffer(FrameBuffer *fbo);

	static const int MAX_COLOR_ATTACHMENTS = 8;
private:
	idStr name;
	Generator generator;
	bool initialized = false;

	GLuint fbo = 0;
	int width = 0;
	int height = 0;
	int msaa = 0;

	GLuint colorRenderBuffers[MAX_COLOR_ATTACHMENTS] = { 0 };
	GLuint depthRenderBuffer = 0;

	void Generate();

	void AddRenderBuffer(GLuint &buffer, GLenum attachment, GLenum format, const idStr &name);
	void AddRenderTexture(idImage *texture, GLenum attachment, int mipLevel);
};

extern idCVar r_showFBO;
extern idCVar r_fboColorBits;
extern idCVarBool r_fboSRGB;
extern idCVar r_fboDepthBits;
extern idCVarInt r_shadowMapSize;
extern idCVar r_fboResolution;
extern idCVarBool r_tonemap;

extern renderCrop_t ShadowAtlasPages[42];

void FB_RenderTexture(idImage *texture);
void FB_DebugShowContents();
void FB_ApplyScissor();
