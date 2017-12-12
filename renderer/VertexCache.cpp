/*****************************************************************************
                    The Dark Mod GPL Source Code
 
 This file is part of the The Dark Mod Source Code, originally based 
 on the Doom 3 GPL Source Code as published in 2011.
 
 The Dark Mod Source Code is free software: you can redistribute it 
 and/or modify it under the terms of the GNU General Public License as 
 published by the Free Software Foundation, either version 3 of the License, 
 or (at your option) any later version. For details, see LICENSE.TXT.
 
 Project: The Dark Mod (http://www.thedarkmod.com/)
 
 $Revision: 5383 $ (Revision of last commit) 
 $Date: 2012-04-11 09:46:32 +0000 (Wed, 11 Apr 2012) $ (Date of last commit)
 $Author: serpentine $ (Author of last commit)
 
******************************************************************************/

#include "precompiled_engine.h"
#include "../game/DarkModGlobals.h"
#include <mutex>
#pragma hdrstop

static bool versioned = RegisterVersionedFile("$Id: VertexCache.cpp 5383 2012-04-11 09:46:32Z serpentine $");

#include "tr_local.h"


idCVar idVertexCache::r_showVertexCache( "r_showVertexCache", "0", CVAR_INTEGER|CVAR_RENDERER, "" );
idCVar idVertexCache::r_vertexBufferMegs( "r_vertexBufferMegs", "32", CVAR_INTEGER|CVAR_RENDERER, "" );

idVertexCache		vertexCache;


idFile* cacheLogFile = nullptr;
std::mutex logMutex;

void CacheLog(const char* fmt, ...) {
	/*std::unique_lock<std::mutex> lock( logMutex );
	if (cacheLogFile == nullptr) {
		cacheLogFile = fileSystem->OpenFileWrite( "vertexcache.txt", "fs_savepath", "" );
	}
	va_list args;
	va_start( args, fmt );
	cacheLogFile->VPrintf( fmt, args );
	va_end( args );
	cacheLogFile->Flush();*/
}

/*
==============
R_ListVertexCache_f
==============
*/
static void R_ListVertexCache_f( const idCmdArgs &args ) {
	vertexCache.List();
}

/*
==============
idVertexCache::ActuallyFree
==============
*/
void idVertexCache::ActuallyFree( vertCache_t *block ) {
	CacheLog( "Actually freeing vertCache block %d in list %d\r", block, listNum );
	if (!block) {
		common->Error( "idVertexCache Free: NULL pointer" );
		return;
	}

	else if ( block->user ) {
		// let the owner know we have purged it
		*block->user = NULL;
		block->user = NULL;
	}

	// temp blocks are in a shared space that won't be freed
	if ( block->tag != TAG_TEMP ) {
		staticAllocTotal -= block->size;
		staticCountTotal--;

		if ( block->vbo ) {
#if 0		// this isn't really necessary, it will be reused soon enough
			// filling with zero length data is the equivalent of freeing
			glBindBufferARB(GL_ARRAY_BUFFER_ARB, block->vbo);
			glBufferDataARB(GL_ARRAY_BUFFER_ARB, 0, 0, GL_DYNAMIC_DRAW_ARB);
#endif
		} else if ( block->virtMem ) {
			Mem_Free( block->virtMem );
			block->virtMem = NULL;
		}
	}
	block->tag = TAG_FREE;		// mark as free

	// unlink stick it back on the free list
	block->next->prev = block->prev;
	block->prev->next = block->next;

#if 1
	// stick it on the front of the free list so it will be reused immediately
	block->next = freeStaticHeaders[listNum].next;
	block->prev = &freeStaticHeaders[listNum];
#else
	// stick it on the back of the free list so it won't be reused soon (just for debugging)
	block->next = &freeStaticHeaders[listNum];
	block->prev = freeStaticHeaders[listNum].prev;
#endif

	block->next->prev = block;
	block->prev->next = block;
}

/*
==============
idVertexCache::Position

this will be a real pointer with virtual memory,
but it will be an int offset cast to a pointer with
ARB_vertex_buffer_object

The ARB_vertex_buffer_object will be bound
==============
*/
void *idVertexCache::Position( vertCache_t *buffer ) {
	CacheLog( "Getting position for vertCache block %d, current list %d\r", buffer, listNum );
	if (!buffer || buffer->tag == TAG_FREE) {
		common->FatalError( "idVertexCache::Position: bad vertCache_t %d", buffer );
	}

	// the ARB vertex object just uses an offset
	else if ( buffer->vbo ) {
		if ( r_showVertexCache.GetInteger() == 2 ) {
			if ( buffer->tag == TAG_TEMP ) {
				common->Printf( "GL_ARRAY_BUFFER_ARB = %i + %i (%i bytes)\n", buffer->vbo, buffer->offset, buffer->size ); 
			} else {
				common->Printf( "GL_ARRAY_BUFFER_ARB = %i (%i bytes)\n", buffer->vbo, buffer->size ); 
			}
		}
		if ( buffer->indexBuffer ) {
			glBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, buffer->vbo );
		} else {
			glBindBufferARB( GL_ARRAY_BUFFER_ARB, buffer->vbo );
		}
		return (void *)buffer->offset;
	}

	// virtual memory is a real pointer
	return (void *)((byte *)buffer->virtMem + buffer->offset);
}

void idVertexCache::UnbindIndex() {
	glBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
}


//================================================================================

/*
===========
idVertexCache::Init
===========
*/
void idVertexCache::Init() {
	cmdSystem->AddCommand( "listVertexCache", R_ListVertexCache_f, CMD_FL_RENDERER, "lists vertex cache" );

	if ( r_vertexBufferMegs.GetInteger() < 8 ) {
		r_vertexBufferMegs.SetInteger( 8 );
	}

	virtualMemory = false;

	// use ARB_vertex_buffer_object unless explicitly disabled
	if( r_useVertexBuffers.GetInteger() && glConfig.ARBVertexBufferObjectAvailable ) {
		common->Printf( "using ARB_vertex_buffer_object memory\n" );
	} else {
		virtualMemory = true;
		r_useIndexBuffers.SetBool( false );
		common->Printf( "WARNING: vertex array range in virtual memory (SLOW)\n" );
	}

	// initialize the cache memory blocks
	for (int i = 0; i < NUM_VERTEX_FRAMES; i++) {
		freeStaticHeaders[i].next = freeStaticHeaders[i].prev = &freeStaticHeaders[i];
		staticHeaders[i].next = staticHeaders[i].prev = &staticHeaders[i];
		freeDynamicHeaders[i].next = freeDynamicHeaders[i].prev = &freeDynamicHeaders[i];
		dynamicHeaders[i].next = dynamicHeaders[i].prev = &dynamicHeaders[i];
		deferredFreeList[i].next = deferredFreeList[i].prev = &deferredFreeList[i];
	}
	listNum = 0;

	// set up the dynamic frame memory
	staticAllocTotal = 0;
	byte	*junk = (byte *)Mem_Alloc( FRAME_MEMORY_BYTES );
	for ( int i = 0 ; i < NUM_VERTEX_FRAMES ; i++ ) {
		allocatingTempBuffer = true;	// force the alloc to use GL_STREAM_DRAW_ARB
		Alloc( junk, FRAME_MEMORY_BYTES, &tempBuffers[i] );
		allocatingTempBuffer = false;
		tempBuffers[i]->tag = TAG_FIXED;
		// unlink these from the static list, so they won't ever get purged
		tempBuffers[i]->next->prev = tempBuffers[i]->prev;
		tempBuffers[i]->prev->next = tempBuffers[i]->next;
	}
	Mem_Free( junk );

	EndFrame();
}

/*
===========
idVertexCache::PurgeAll

Used when toggling vertex programs on or off, because
the cached data isn't valid
===========
*/
void idVertexCache::PurgeAll() {
	for (int i = 0; i < NUM_VERTEX_FRAMES; ++i) {
		while (staticHeaders[i].next != &staticHeaders[i]) {
			ActuallyFree( staticHeaders[i].next );
		}
	}
}

/*
===========
idVertexCache::Shutdown
===========
*/
void idVertexCache::Shutdown() {
//	PurgeAll();	// !@#: also purge the temp buffers

	headerAllocator.Shutdown();
}

/*
===========
idVertexCache::Alloc
===========
*/
void idVertexCache::Alloc( void *data, int size, vertCache_t **buffer, bool indexBuffer ) {
	vertCache_t	*block;

	if ( size <= 0 ) {
		common->Error( "idVertexCache::Alloc: size = %i\n", size );
	}

	// if we can't find anything, it will be NULL
	*buffer = NULL;

	// if we don't have any remaining unused headers, allocate some more
	if (freeStaticHeaders[listNum].next == &freeStaticHeaders[listNum]) {
		for ( int i = 0; i < EXPAND_HEADERS; i++ ) {
			block = headerAllocator.Alloc();
			block->next = freeStaticHeaders[listNum].next;
			block->prev = &freeStaticHeaders[listNum];
			block->next->prev = block;
			block->prev->next = block;

			if( !virtualMemory ) {
				glGenBuffersARB( 1, & block->vbo );
			}
		}
	}

	// move it from the freeStaticHeaders list to the staticHeaders list
	block = freeStaticHeaders[listNum].next;
	block->next->prev = block->prev;
	block->prev->next = block->next;
	block->next = staticHeaders[listNum].next;
	block->prev = &staticHeaders[listNum];
	block->next->prev = block;
	block->prev->next = block;

	block->size = size;
	block->offset = 0;
	block->tag = TAG_USED;

	// save data for debugging
	staticAllocThisFrame += block->size;
	staticCountThisFrame++;
	staticCountTotal++;
	staticAllocTotal += block->size;

	// allocation doesn't imply used-for-drawing, because at level
	// load time lots of things may be created, but they aren't
	// referenced by the GPU yet, and can be purged if needed.
	block->frameUsed = currentFrame - NUM_VERTEX_FRAMES;

	block->indexBuffer = indexBuffer;

	// copy the data
	if ( block->vbo ) {
		if ( indexBuffer ) {
			glBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, block->vbo );
			glBufferDataARB( GL_ELEMENT_ARRAY_BUFFER_ARB, (GLsizeiptrARB)size, data, GL_STATIC_DRAW_ARB );
		} else {
			glBindBufferARB( GL_ARRAY_BUFFER_ARB, block->vbo );
			if ( allocatingTempBuffer ) {
				glBufferDataARB( GL_ARRAY_BUFFER_ARB, (GLsizeiptrARB)size, data, GL_STREAM_DRAW_ARB );
			} else {
				glBufferDataARB( GL_ARRAY_BUFFER_ARB, (GLsizeiptrARB)size, data, GL_STATIC_DRAW_ARB );
			}
		}
	} else {
		block->virtMem = Mem_Alloc( size );
		SIMDProcessor->Memcpy( block->virtMem, data, size );
	}

	// this will be set to zero when it is purged
	block->user = buffer;
	*buffer = block;
	CacheLog( "Allocated vertCache block %d, current list %d\r", block, listNum );
}

/*
===========
idVertexCache::Touch
===========
*/
void idVertexCache::Touch( vertCache_t *block ) {
	CacheLog( "Touching vertCache block %d, current list %d\r", block, listNum );
	if (!block) {
		common->Error( "idVertexCache Touch: NULL pointer" );
		return;
	}
	else if ( block->tag == TAG_FREE ) {
		common->FatalError( "idVertexCache Touch: freed pointer" );
	}
	else if ( block->tag == TAG_TEMP ) {
		common->FatalError( "idVertexCache Touch: temporary pointer" );
	}
	else {
		block->frameUsed = currentFrame;

		// move to the head of the LRU list
		block->next->prev = block->prev;
		block->prev->next = block->next;

		block->next = staticHeaders[listNum].next;
		block->prev = &staticHeaders[listNum];
		staticHeaders[listNum].next->prev = block;
		staticHeaders[listNum].next = block;
	}
}

/*
===========
idVertexCache::Free
===========
*/
void idVertexCache::Free( vertCache_t *block ) {
	CacheLog( "Freeing vertCache block %d, current list %d\r", block, listNum );
	if (!block) {
		return;
	}

	else if ( block->tag == TAG_FREE ) {
		common->FatalError( "idVertexCache Free: freed pointer" );
	}
	else if ( block->tag == TAG_TEMP ) {
		common->FatalError( "idVertexCache Free: temporary pointer" );
	}

	// this block still can't be purged until the frame count has expired,
	// but it won't need to clear a user pointer when it is
	block->user = NULL;

	block->next->prev = block->prev;
	block->prev->next = block->next;

	block->next = deferredFreeList[listNum].next;
	block->prev = &deferredFreeList[listNum];
	deferredFreeList[listNum].next->prev = block;
	deferredFreeList[listNum].next = block;
}

/*
===========
idVertexCache::AllocFrameTemp

A frame temp allocation must never be allowed to fail due to overflow.
We can't simply sync with the GPU and overwrite what we have, because
there may still be future references to dynamically created surfaces.
===========
*/
vertCache_t	*idVertexCache::AllocFrameTemp( void *data, int size ) {
	vertCache_t	*block;

	if ( size <= 0 ) {
		common->Error( "idVertexCache::AllocFrameTemp: size = %i\n", size );
	}

	if ( dynamicAllocThisFrame + size > FRAME_MEMORY_BYTES ) {
		// if we don't have enough room in the temp block, allocate a static block,
		// but immediately free it so it will get freed at the next frame
		tempOverflow = true;
		Alloc( data, size, &block );
		Free( block);
		return block;
	}

	// this data is just going on the shared dynamic list

	// if we don't have any remaining unused headers, allocate some more
	if (freeDynamicHeaders[listNum].next == &freeDynamicHeaders[listNum]) {

		for ( int i = 0; i < EXPAND_HEADERS; i++ ) {
			block = headerAllocator.Alloc();
			block->next = freeDynamicHeaders[listNum].next;
			block->prev = &freeDynamicHeaders[listNum];
			block->next->prev = block;
			block->prev->next = block;
		}
	}

	// move it from the freeDynamicHeaders list to the dynamicHeaders list
	block = freeDynamicHeaders[listNum].next;
	block->next->prev = block->prev;
	block->prev->next = block->next;
	block->next = dynamicHeaders[listNum].next;
	block->prev = &dynamicHeaders[listNum];
	block->next->prev = block;
	block->prev->next = block;

	block->size = size;
	block->tag = TAG_TEMP;
	block->indexBuffer = false;
	block->offset = dynamicAllocThisFrame;
	dynamicAllocThisFrame += block->size;
	dynamicCountThisFrame++;
	block->user = NULL;
	block->frameUsed = 0;

	// copy the data
	block->virtMem = tempBuffers[listNum]->virtMem;
	block->vbo = tempBuffers[listNum]->vbo;

	if ( block->vbo ) {
		glBindBufferARB( GL_ARRAY_BUFFER_ARB, block->vbo );
		glBufferSubDataARB( GL_ARRAY_BUFFER_ARB, block->offset, (GLsizeiptrARB)size, data );
	} else {
		SIMDProcessor->Memcpy( (byte *)block->virtMem + block->offset, data, size );
	}

	CacheLog( "Allocated frame temp vertCache block %d, current list %d\r", block, listNum );

	return block;
}

/*
===========
idVertexCache::EndFrame
===========
*/
void idVertexCache::EndFrame() {
	// display debug information
	if ( r_showVertexCache.GetBool() ) {
		int	staticUseCount = 0;
		int staticUseSize = 0;

		for (vertCache_t *block = staticHeaders[listNum].next; block != &staticHeaders[listNum]; block = block->next) {
			if ( block->frameUsed == currentFrame) {
				staticUseCount++;
				staticUseSize += block->size;
			}
		}

		const char *frameOverflow = tempOverflow ? "(OVERFLOW)" : "";

		common->Printf( "vertex dynamic:%i=%ik%s, static alloc:%i=%ik used:%i=%ik total:%i=%ik\n",
			dynamicCountThisFrame, dynamicAllocThisFrame/1024, frameOverflow,
			staticCountThisFrame, staticAllocThisFrame/1024,
			staticUseCount, staticUseSize/1024,
			staticCountTotal, staticAllocTotal/1024 );
	}

#if 0
	// if our total static count is above our working memory limit, start purging things
	while ( staticAllocTotal > r_vertexBufferMegs.GetInteger() * 1024 * 1024 ) {
		// free the least recently used

	}
#endif

	if( !virtualMemory ) {
		// unbind vertex buffers so normal virtual memory will be used in case
		// r_useVertexBuffers / r_useIndexBuffers
		glBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
		glBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
	}


	currentFrame = tr.frameCount;
	listNum = (listNum + 1) % NUM_VERTEX_FRAMES;
	staticAllocThisFrame = 0;
	staticCountThisFrame = 0;
	dynamicAllocThisFrame = 0;
	dynamicCountThisFrame = 0;
	tempOverflow = false;

	// free all the deferred free headers
	while( deferredFreeList[listNum].next != &deferredFreeList[listNum] ) {
		ActuallyFree( deferredFreeList[listNum].next );
	}

	// free all the frame temp headers from last frame
	vertCache_t	*block = dynamicHeaders[listNum].next;
	if (block != &dynamicHeaders[listNum]) {
		block->prev = &freeDynamicHeaders[listNum];
		dynamicHeaders[listNum].prev->next = freeDynamicHeaders[listNum].next;
		freeDynamicHeaders[listNum].next->prev = dynamicHeaders[listNum].prev;
		freeDynamicHeaders[listNum].next = block;

		dynamicHeaders[listNum].next = dynamicHeaders[listNum].prev = &dynamicHeaders[listNum];
	}
}

/*
=============
idVertexCache::List
=============
*/
void idVertexCache::List( void ) {
	int	numActive = 0;
	int frameStatic = 0;
	int	totalStatic = 0;
	int	numFreeStaticHeaders = 0;
	int	numFreeDynamicHeaders = 0;

	vertCache_t *block;
	for (block = staticHeaders[listNum].next; block != &staticHeaders[listNum]; block = block->next) {
		numActive++;

		totalStatic += block->size;
		if ( block->frameUsed == currentFrame ) {
			frameStatic += block->size;
		}
	}
	
	for (block = freeStaticHeaders[listNum].next; block != &freeStaticHeaders[listNum]; block = block->next) {
		numFreeStaticHeaders++;
	}

	for (block = freeDynamicHeaders[listNum].next; block != &freeDynamicHeaders[listNum]; block = block->next) {
		numFreeDynamicHeaders++;
	}

	common->Printf( "%i megs working set\n", r_vertexBufferMegs.GetInteger() );
	common->Printf( "%i dynamic temp buffers of %ik\n", NUM_VERTEX_FRAMES, FRAME_MEMORY_BYTES / 1024 );
	common->Printf( "%5i active static headers\n", numActive );
	common->Printf( "%5i free static headers\n", numFreeStaticHeaders );
	common->Printf( "%5i free dynamic headers\n", numFreeDynamicHeaders );

	if ( !virtualMemory  ) {
		common->Printf( "Vertex cache is in ARB_vertex_buffer_object memory (FAST).\n");
	} else {
		common->Printf( "Vertex cache is in virtual memory (SLOW)\n" );
	}

	if ( r_useIndexBuffers.GetBool() ) {
		common->Printf( "Index buffers are accelerated.\n" );
	} else {
		common->Printf( "Index buffers are not used.\n" );
	}
}
