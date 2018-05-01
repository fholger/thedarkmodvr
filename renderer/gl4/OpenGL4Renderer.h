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
#ifndef __OPENGL4_RENDERER_H__
#define __OPENGL4_RENDERER_H__

#include <unordered_map>
#include "PersistentBufferObject.h"
#include "GL4Program.h"
#include "UniformBufferObject.h"

struct DrawElementsIndirectCommand;

enum ProgramType {
	SHADER_DEPTH_FAST_MD = 0,
	SHADER_DEPTH_FAST_MD_STEREO,
	SHADER_DEPTH_GENERIC,
	SHADER_DEPTH_GENERIC_STEREO,
	SHADER_OCCLUSION,
	SHADER_OCCLUSION_BITPACK,
	SHADER_OCCLUSION_DEBUG,
	SHADER_STENCIL_MD,
	SHADER_INTERACTION_SIMPLE,
	SHADER_OLDSTAGE,
	TOTAL_SHADER_COUNT
};

enum VertexAttribs {
	VA_POSITION		= 0,
	VA_TEXCOORD		= 1,
	VA_NORMAL		= 2,
	VA_TANGENT		= 3,
	VA_BITANGENT	= 4,
	VA_COLOR		= 5,
	VA_SHADOWPOS	= 6,
	VA_DRAWID		= 15
};

class OpenGL4Renderer {
public:
	OpenGL4Renderer();
	~OpenGL4Renderer();

	void Init();
	void Shutdown();
	bool IsInitialized() const { return initialized; }

	DrawElementsIndirectCommand * ReserveCommandBuffer( uint count );
	byte * ReserveSSBO( uint size );
	void MarkUsedSSBO( uint size );

	GL4Program GetShader( ProgramType shaderType ) const;

	void BindSSBO( GLuint index, GLuint size );

	void BindUBO( GLuint index );
	void UpdateUBO( const void *data, GLsizeiptr size );

	// prepares vertex attrib formats and disables them all initially. static buffers are bound
	void PrepareVertexAttribs();

	void EnableVertexAttribs( std::initializer_list<VertexAttribs> attribs );
	void BindVertexBuffer( bool staticBuffer );

	void BeginFrame();
	void EndFrame();

private:

	void LoadShaders();
	void DestroyShaders();

	bool initialized;
	GLuint drawIdBuffer;
	PersistentBufferObject ssbo;
	PersistentBufferObject ubo;
	GLuint uboBindIndex;
	DrawElementsIndirectCommand *commandBuffer;

	uint32_t enabledVertexAttribs;
	bool boundStaticVertexBuffer;
	
	GL4Program shaders[TOTAL_SHADER_COUNT];
	std::unordered_map<GLenum, GLuint> boundBuffers;
};

extern OpenGL4Renderer openGL4Renderer;

#endif