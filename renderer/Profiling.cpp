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
#pragma hdrstop

#include "Profiling.h"

idCVar r_useDebugGroups( "r_useDebugGroups", "0", CVAR_RENDERER | CVAR_BOOL, "Emit GL debug groups during rendering. Useful for frame debugging and analysis with e.g. nSight, which will group render calls accordingly." );
idCVar r_glProfiling( "r_glProfiling", "0", CVAR_RENDERER | CVAR_BOOL, "Collect profiling information about GPU and CPU time spent in the rendering backend." );
idCVar r_frontendProfiling( "r_frontendProfiling", "0", CVAR_RENDERER| CVAR_BOOL, "Collect profiling information about CPU time spent in the rendering frontend." );

/**
 * Tracks CPU and GPU time spent in profiled sections.
 * Sections are structured in hierarchical fashion, you can push and pop sections onto a stack.
 * Each section has a name; sections with the same name at the same place in the hierarchy will
 * be accumulated to a single total time in the end.
 */
class Profiler {
public:
	Profiler(bool gpuTimes) : recordGpuTimes(gpuTimes), profilingActive(false), lastTimingCopy(0), frameMarker(0), frameNumber(0) {}

	void BeginFrame() {
		++frameNumber;
		sectionStack.clear();
		frame[ frameMarker ] = section();
		frame[ frameMarker ].name = idStr("Frame ") + frameNumber;
		AddProfilingQuery( &frame[ frameMarker ] );
		sectionStack.push_back( &frame[ frameMarker ] );
		profilingActive = true;
	}

	void EndFrame() {
		LeaveSection();
		assert( sectionStack.empty() );
		frameMarker = ( frameMarker + 1 ) % NUM_FRAMES;
		profilingActive = false;

		// collect timing info from the *oldest* frame and save it for retrieval
		AccumulateTotalTimes( frame[ frameMarker ] );
		// little hack: only copy the timings for retrieval every half second
		// otherwise, displaying the values will fluctuate rapidly, making actually reading anything hard...
		uint64_t curTime = Sys_GetClockTicks();
		if( ( curTime - lastTimingCopy ) / Sys_ClockTicksPerSecond() >= 0.5 ) {
			lastTimingCopy = curTime;
			currentTimingInfos = frame[ frameMarker ];
		}
	}

	void EnterSection(const idStr& name) {
		if( !profilingActive )
			return;
		assert( !sectionStack.empty() );
		section *s = FindOrInsertSection( name );
		AddProfilingQuery( s );
		sectionStack.push_back( s );
	}

	void LeaveSection() {
		if( !profilingActive )
			return;
		assert( !sectionStack.empty() );
		CompleteProfilingQuery( sectionStack.back() );
		sectionStack.pop_back();
	}

	typedef struct query {
		uint64_t cpuStartTime;
		uint64_t cpuStopTime;
		GLuint glQueries[ 2 ];
	} query;

	typedef struct section {
		idStr name;
		int count;
		double totalCpuTimeMillis;
		double totalGpuTimeMillis;
		std::vector<section> children;
		std::vector<query> queries;
	} section;

	section * GetCurrentTimingInfo() {
		return &currentTimingInfos;
	}
	
private:
	static const int NUM_FRAMES = 3;

	bool recordGpuTimes;
	bool profilingActive;
	std::vector<section*> sectionStack;
	section currentTimingInfos;
	uint64_t lastTimingCopy;

	// double-buffering, since GL query information is only available after the frame has rendered
	section frame[ NUM_FRAMES ];
	int frameMarker;
	int frameNumber;

	section *FindOrInsertSection(const idStr& name) {
		for ( auto& s : sectionStack.back()->children ) {
			if( s.name == name ) {
				return &s;
			}
		}
		sectionStack.back()->children.push_back( section() );
		section *s = &sectionStack.back()->children.back();
		s->name = name;
		return s;
	}

	void AddProfilingQuery( section *s ) {
		s->queries.push_back( query() );
		query &q = s->queries.back();
		q.cpuStartTime = Sys_GetClockTicks();
		if (recordGpuTimes) {
			qglGenQueries( 2, q.glQueries );
			qglQueryCounter( q.glQueries[ 0 ], GL_TIMESTAMP );
		}
	}

	void CompleteProfilingQuery( section *s ) {
		query &q = s->queries.back();
		q.cpuStopTime = Sys_GetClockTicks();
		if (recordGpuTimes) {
			qglQueryCounter( q.glQueries[ 1 ], GL_TIMESTAMP );
		}
	}

	void AccumulateTotalTimes( section &s) {
		s.totalCpuTimeMillis = s.totalGpuTimeMillis = 0;
		s.count = (int)s.queries.size();
		for ( auto& q : s.queries ) {
			s.totalCpuTimeMillis += ( q.cpuStopTime - q.cpuStartTime ) * 1000 / Sys_ClockTicksPerSecond();
			if (recordGpuTimes) {
			uint64_t gpuStartNanos, gpuStopNanos;
				qglGetQueryObjectui64v( q.glQueries[ 0 ], GL_QUERY_RESULT, &gpuStartNanos );
				qglGetQueryObjectui64v( q.glQueries[ 1 ], GL_QUERY_RESULT, &gpuStopNanos );
				s.totalGpuTimeMillis += ( gpuStopNanos - gpuStartNanos ) / 1000000.0;
				qglDeleteQueries( 2, q.glQueries );
			}
		}
		s.queries.clear();

		for ( auto& c : s.children ) {
			AccumulateTotalTimes( c );
		}
	}
};

Profiler glProfilerImpl(true);
Profiler frontendProfilerImpl(false);
Profiler *glProfiler = &glProfilerImpl;
Profiler *frontendProfiler = &frontendProfilerImpl;
static const int NAME_COLUMN_WIDTH = -40;

void ProfilingEnterSection( Profiler *profiler, const char *section ) {
	profiler->EnterSection( section );
}

void ProfilingLeaveSection( Profiler *profiler ) {
	profiler->LeaveSection();
}

void ProfilingBeginFrame() {
	if( r_glProfiling.GetBool() ) {
		glProfiler->BeginFrame();
	}
	if( r_frontendProfiling.GetBool() ) {
		frontendProfiler->BeginFrame();
	}
}

void ProfilingEndFrame() {
	if( r_glProfiling.GetBool() ) {
		glProfiler->EndFrame();
	}
	if( r_frontendProfiling.GetBool() ) {
		frontendProfiler->EndFrame();
	}
}

void ProfilingDrawSingleLine( int &y, const char *text, ... ) {
	char string[MAX_STRING_CHARS];
	va_list argptr;

	va_start( argptr, text );
	idStr::vsnPrintf( string, sizeof( string ), text, argptr );
	va_end( argptr );

	const idMaterial *textShader = declManager->FindMaterial( "textures/consolefont" );
	renderSystem->DrawSmallStringExt( 0, y + 2, string, colorWhite, true, textShader );
	y += SMALLCHAR_HEIGHT + 4;
}

void ProfilingDrawSectionTimings( int &y, Profiler::section &s, idStr indent ) {
	idStr level = indent + s.name;
	ProfilingDrawSingleLine( y, "%*s %6d  %6.3f ms  %6.3f ms", NAME_COLUMN_WIDTH, level.c_str(), s.count, s.totalCpuTimeMillis, s.totalGpuTimeMillis );
	for( auto& c : s.children ) {
		ProfilingDrawSectionTimings( y, c, indent + "  " );
	}
}

void ProfilingDrawCurrentTimings() {
	int y = 4;
	ProfilingDrawSingleLine( y, "%*s %6s  %9s  %9s", NAME_COLUMN_WIDTH, "# Section", "Count", "CPU", "GPU" );
	if (r_glProfiling.GetBool()) {
		Profiler::section *s = glProfiler->GetCurrentTimingInfo();
		ProfilingDrawSectionTimings( y, *s, "" );
	}
	if (r_frontendProfiling.GetBool()) {
		Profiler::section *s = frontendProfiler->GetCurrentTimingInfo();
		ProfilingDrawSectionTimings( y, *s, "" );
	}
}

void ProfilingPrintSectionTimings( Profiler::section &s, idStr indent ) {
	idStr level = indent + s.name;
	common->Printf( "%*s %6d  %6.3f ms  %6.3f ms\n", NAME_COLUMN_WIDTH, level.c_str(), s.count, s.totalCpuTimeMillis, s.totalGpuTimeMillis );
	for( auto& c : s.children ) {
		ProfilingPrintSectionTimings( c, indent + "  " );
	}
}

void GL_SetDebugLabel(GLenum identifier, GLuint name, const idStr &label ) {
	if( r_useDebugGroups.GetBool() ) {
		qglObjectLabel( identifier, name, std::min(label.Length(), 256), label.c_str() );
	}
}

void GL_SetDebugLabel(void *ptr, const idStr &label ) {
	if( r_useDebugGroups.GetBool() ) {
		qglObjectPtrLabel( ptr, std::min(label.Length(), 256), label.c_str() );
	}
}
