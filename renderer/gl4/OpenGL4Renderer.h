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
	SHADER_DEPTH_GENERIC,
	TOTAL_SHADER_COUNT
};

class OpenGL4Renderer {
public:
	OpenGL4Renderer();
	~OpenGL4Renderer();

	void Init();
	void Shutdown();

	DrawElementsIndirectCommand * ReserveCommandBuffer( uint count );
	byte * ReserveSSBO( uint size );
	void LockSSBO( uint size );

	GL4Program GetShader( ProgramType shaderType ) const;

	void BindDrawId( GLuint index );

	void BindSSBO( GLuint index, GLuint size );

	void BindUBO( GLuint index );
	void UpdateUBO( const void *data, GLsizeiptr size );


private:
	void BindBuffer( GLenum target, GLuint buffer );

	void LoadShaders();
	void DestroyShaders();

	bool initialized;
	GLuint drawIdBuffer;
	PersistentBufferObject ssbo;
	UniformBufferObject ubo;
	DrawElementsIndirectCommand *commandBuffer;
	
	GL4Program shaders[TOTAL_SHADER_COUNT];
	std::unordered_map<GLenum, GLuint> boundBuffers;

	friend void GL4_BindBuffer( GLenum target, GLuint buffer );
};

extern OpenGL4Renderer openGL4Renderer;

#endif