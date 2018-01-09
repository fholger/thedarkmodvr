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
#include "GL4Program.h"
#include "../../tools/radiant/NewTexWnd.h"

GLuint CompileShader( GLint type, const char *fileName ) {
	// get shader source
	char *source = nullptr;
	fileSystem->ReadFile( fileName, ( void ** )&source, nullptr );
	if( !source ) {
		common->Warning( "CompileShader: %s not found", fileName );
		return 0;
	}

	// create shader object, set the source, and compile
	GLuint shader = qglCreateShader( type );
	GLint length = ( GLint )strlen( source );
	qglShaderSource( shader, 1, ( const char ** )&source, &length );
	qglCompileShader( shader );
	fileSystem->FreeFile( source );

	// check if the compilation was successful
	GLint result;
	qglGetShaderiv( shader, GL_COMPILE_STATUS, &result );
	if( result == GL_FALSE ) {
		// get the shader info log
		qglGetShaderiv( shader, GL_INFO_LOG_LENGTH, &length );
		char *log = new char[length];
		qglGetShaderInfoLog( shader, length, &result, log );

		common->Warning( "Compiling shader '%s' failed:\n%s\n", fileName, log );
		delete[] log;

		qglDeleteShader( shader );
		return 0;
	}

	return shader;
}

void LoadAndAttachShader( GLuint program, GLint type, const char *fileName ) {
	GLuint shader = CompileShader( type, fileName );
	if( shader != 0 ) {
		qglAttachShader( program, shader );
		// delete the shader - it won't actually be destroyed until the program
		// that it's attached to has been destroyed
		qglDeleteShader( shader );
	}
}

GL4Program GL4Program::Load( const char *vertex, const char *fragment ) {
	GLuint program = qglCreateProgram();
	idStr baseDir( "gl4progs/" );
	LoadAndAttachShader( program, GL_VERTEX_SHADER, baseDir + vertex );
	LoadAndAttachShader( program, GL_FRAGMENT_SHADER, baseDir + fragment );
	common->Printf( "Compiled GLSL program: vertex %s - fragment %s\n", vertex, fragment );

	qglLinkProgram( program );
	GLint result;
	qglGetProgramiv( program, GL_LINK_STATUS, &result );
	if( result != GL_TRUE ) {
		// get the program info log
		GLint length;
		qglGetProgramiv( program, GL_INFO_LOG_LENGTH, &length );
		char *log = new char[length];
		qglGetProgramInfoLog( program, length, &result, log );
		common->Warning( "Program linking failed:\n%s\n", log );
		delete[] log;

		qglDeleteProgram( program );
		return GL4Program( 0 );
	}

	qglValidateProgram( program );
	GLint validProgram;
	qglGetProgramiv( program, GL_VALIDATE_STATUS, &validProgram );
	if( !validProgram ) {
		// get the program info log
		GLint length;
		qglGetProgramiv( program, GL_INFO_LOG_LENGTH, &length );
		char *log = new char[length];
		qglGetProgramInfoLog( program, length, &result, log );
		common->Warning( "Program validation failed:\n%s\n", log );
		delete[] log;

		qglDeleteProgram( program );
		return GL4Program( 0 );
	}

	return GL4Program( program );
}

void GL4Program::Destroy() {
	if( program != 0 && qglIsProgram(program) ) {
		qglDeleteProgram( program );
	}
	program = 0;
}

void GL4Program::Activate() const {
	qglUseProgram( program );
}

void GL4Program::Unset() {
	qglUseProgram( 0 );
}

void GL4Program::SetUniform1( GLint location, GLfloat value ) {
	qglProgramUniform1f( program, location, value );
}

void GL4Program::SetUniform4( GLint location, const GLfloat *values ) {
	qglProgramUniform4fv( program, location, 1, values );
}

void GL4Program::SetUniformMatrix4( GLint location, const GLfloat *matrix ) {
	qglProgramUniformMatrix4fv( program, location, 1, GL_FALSE, matrix );
}

void GL4Program::SetProjectionMatrix( GLint location ) {
	SetUniformMatrix4( location, backEnd.viewDef->projectionMatrix );
}

void GL4Program::SetViewMatrix( GLint location ) {
	SetUniformMatrix4( location, backEnd.viewDef->worldSpace.modelViewMatrix );
}

void GL4Program::SetModelViewProjectionMatrix( GLint location, const viewEntity_t *entity ) {
	float mvpMatrix[16];
	myGlMultMatrix( entity->modelViewMatrix, backEnd.viewDef->projectionMatrix, mvpMatrix );
	SetUniformMatrix4( location, mvpMatrix );
}

GL4Program::GL4Program() : program(0) {}

GL4Program::GL4Program( GLuint program ) : program(program) {}
