#pragma once
#include "Str.h"
#include <cstdint>
#include <gl/GL.h>

class idImage;

class Framebuffer
{
public:
	Framebuffer(const char* name, int width, int height);
	~Framebuffer();

	void Bind();
	static void BindPrimary();

	void AddColorBuffer( GLuint format, int index );
	void AddColorImage( idImage* colorImage, int index, int mipmapLod = 0 );
	void AddColorImageLayer(GLuint texture, int layer, int index, int mipmapLod = 0);
	void AddDepthStencilBuffer( GLuint format );

	void AddStereoColorArray(int numSamples);
	void AddStereoDepthStencilArray(int numSamples);

	uint32_t GetStereoColorArray() const { return stereoColorArray; }
	uint32_t GetFbo() const { return frameBuffer; }

	int GetWidth() const { return width; }
	int GetHeight() const { return height; }
	class idStr GetName() const { return fboName; }

	// check for OpenGL errors
	void Check();

private:
	idStr fboName;

	uint32_t frameBuffer;

	uint32_t colorBuffers[16];
	int colorFormat;

	uint32_t depthBuffer;
	int depthFormat;

	uint32_t stencilBuffer;
	int stencilFormat;

	uint32_t stereoColorArray;
	uint32_t stereoDepthStencilArray;

	int width;
	int height;

	GLuint colorTexnum;
	GLuint depthTexnum;
};

extern Framebuffer* primaryFramebuffer;

