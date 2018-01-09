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
#include "GLDebugGroup.h"
#include <stack>
#include "../../tools/radiant/NewTexWnd.h"

const bool enableProfiling = true;

idCVar r_profileGL( "r_profileGL", "0", CVAR_RENDERER | CVAR_BOOL, "Profile CPU/GPU timings in backend rendering" );

struct profileInfo_t {
	std::string name;
	int indent;
	GLuint query;
	double cpuStart;
	double cpuEnd;
	double gpuTime;
};

struct GLProfiling {
	std::vector<profileInfo_t> sections;
	std::stack<profileInfo_t> profileStack;
	idFile* profileFile;

	void Push(const char *message) {
		profileInfo_t info;
		info.name = message;
		info.indent = profileStack.size();
		qglGenQueries( 1, &info.query );
		qglBeginQuery( GL_TIME_ELAPSED, info.query );
		info.cpuStart = Sys_GetClockTicks();
		profileStack.push( info );
	}

	void Pop() {
		sections.push_back( profileStack.top() );
		profileStack.pop();
		profileInfo_t& info = *sections.rbegin();
		info.cpuEnd = Sys_GetClockTicks();
		qglEndQuery( info.query );
		qglEndQuery( GL_TIME_ELAPSED );
	}
} glProfiling;

GL_DebugGroup::GL_DebugGroup( const char *message, GLuint id ) {
	qglPushDebugGroup( GL_DEBUG_SOURCE_APPLICATION, id, -1, message );
	if( r_profileGL.GetBool() )
		glProfiling.Push( message );
}

GL_DebugGroup::~GL_DebugGroup() {
	if( r_profileGL.GetBool() )
		glProfiling.Pop();
	qglPopDebugGroup();
}

void SaveProfilingData() {
	if( !glProfiling.profileFile ) {
		idStr fileName;
		uint64_t currentTime = Sys_GetTimeMicroseconds();
		sprintf( fileName, "gl_profile_%.20llu.txt", currentTime );
		glProfiling.profileFile = fileSystem->OpenFileWrite( fileName, "fs_savepath", "" );
	}

	glProfiling.profileFile->Printf( "======== Frame start ========\n" );
	for( auto it = glProfiling.sections.rbegin(); it != glProfiling.sections.rend(); ++it ) {
		profileInfo_t& info = *it;
		std::string indent;
		for( int i = 0; i < info.indent; ++i )
			indent += "  ";
		double cpuTime = ( info.cpuEnd - info.cpuStart ) / Sys_ClockTicksPerSecond() * 1000000;
		GLuint gpuNanos;
		qglGetQueryObjectuiv( info.query, GL_QUERY_RESULT, &gpuNanos );
		qglDeleteQueries( 1, &info.query );
		double gpuTime = gpuNanos / 1000.0;
		glProfiling.profileFile->Printf( "%s%s: %.2f us GPU / %.2f us CPU\n", indent.c_str(), info.name.c_str(), gpuTime, cpuTime );
	}
	glProfiling.sections.clear();
}
