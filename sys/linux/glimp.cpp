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
#include "../../idlib/precompiled.h"
#include "../../renderer/tr_local.h"
#include "local.h"

#include <GLFW/glfw3.h>
GLXFBConfig bestFbc = 0;
XVisualInfo *visinfo = NULL;

GLFWwindow *window = nullptr;

idCVar r_customMonitor( "r_customMonitor", "0", CVAR_RENDERER|CVAR_INTEGER|CVAR_ARCHIVE|CVAR_NOCHEAT, "Select monitor to use for fullscreen mode (0 = primary)" );

void GLimp_ActivateContext() {
	assert( window );
	glfwMakeContextCurrent( window );
}

void GLimp_DeactivateContext() {
	glfwMakeContextCurrent( nullptr );
}

/*
=================
GLimp_RestoreGamma

save and restore the original gamma of the system
=================
*/
void GLimp_RestoreGamma() {}

/*
=================
GLimp_SetGamma

gamma ramp is generated by the renderer from r_gamma and r_brightness for 256 elements
the size of the gamma ramp can not be changed on X (I need to confirm this)
=================
*/
void GLimp_SetGamma(unsigned short red[256], unsigned short green[256], unsigned short blue[256]) {
	//stgatilov: brightness and gamma adjustments are done in final shader pass
	return;
}

void GLimp_Shutdown() {
	if ( window ) {
		common->Printf( "...shutting down QGL\n" );
		GLimp_UnloadFunctions();
		
		glfwDestroyWindow( window );
		glfwTerminate();

		window = nullptr;
	}
}

void GLimp_SwapBuffers() {
	assert( window );
	glfwSwapBuffers( window );
}

static GLFWmonitor *GLimp_GetMonitor() {
	int count;
	GLFWmonitor **monitors = glfwGetMonitors( &count );
	int requestedMonitor = r_customMonitor.GetInteger();
	if ( requestedMonitor < 0 || requestedMonitor >= count ) {
		common->Printf( "Requested monitor %d not found, using primary monitor\n", requestedMonitor );
		r_customMonitor.SetInteger( 0 );
		return glfwGetPrimaryMonitor();
	}
	return monitors[requestedMonitor];
}

void close_callback( GLFWwindow * ) {
	common->Quit();
}

void resize_callback( GLFWwindow *, int width, int height ) {
	glConfig.vidWidth = width;
	glConfig.vidHeight = height;
	cvarSystem->Find( "r_fboResolution" )->SetModified();
}

/*
===============
GLX_Init
===============
*/
int GLX_Init(glimpParms_t a) {
	if (!glfwInit()) {
		return false;
	}

	common->Printf( "Initializing OpenGL display\n" );

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, QGL_REQUIRED_VERSION_MAJOR);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, QGL_REQUIRED_VERSION_MINOR);
	glfwWindowHint(GLFW_OPENGL_PROFILE, r_glCoreProfile.GetInteger() == 0 ? GLFW_OPENGL_COMPAT_PROFILE : GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, r_glCoreProfile.GetInteger() == 2 ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, r_glDebugContext.GetBool() ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHint(GLFW_CENTER_CURSOR, GLFW_TRUE);
	glfwWindowHint(GLFW_SRGB_CAPABLE, r_fboSRGB ? GLFW_TRUE : GLFW_FALSE );

	GLFWmonitor *monitor = nullptr;
	if ( a.fullScreen ) {
		monitor = GLimp_GetMonitor();
		if ( monitor == nullptr ) {
			return false;
		}

		if ( r_fullscreen.GetInteger() == 2 ) {
			const GLFWvidmode *mode = glfwGetVideoMode( monitor );
			common->Printf( "Borderless fullscreen - using current video mode for monitor %d: %d x %d\n", r_customMonitor.GetInteger(), mode->width, mode->height );
			// "borderless" fullscreen mode, need to set parameters exactly as original video mode of the active monitor
			glfwWindowHint(GLFW_RED_BITS, mode->redBits);
			glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
			glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
			glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
			glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
			glConfig.vidWidth = mode->width;
			glConfig.vidHeight = mode->height;
		} else {
			glfwWindowHint(GLFW_RED_BITS, 8);
			glfwWindowHint(GLFW_GREEN_BITS, 8);
			glfwWindowHint(GLFW_BLUE_BITS, 8);
			glfwWindowHint(GLFW_REFRESH_RATE, a.displayHz <= 0 ? GLFW_DONT_CARE : a.displayHz);
			glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_TRUE);
		}
	}

	window = glfwCreateWindow(glConfig.vidWidth, glConfig.vidHeight, "The Dark Mod", monitor, nullptr);

	if (window == nullptr) {
		common->Printf( "Failed creating window\n" );
		return false;
	}

	GLFWimage icons[2];
	R_LoadImage( "darkmod_icon_small.tga", &icons[0].pixels, &icons[0].width, &icons[0].height, nullptr );
	R_LoadImage( "darkmod_icon.tga", &icons[1].pixels, &icons[1].width, &icons[1].height, nullptr );
	glfwSetWindowIcon( window, 2, icons );
	R_StaticFree( icons[0].pixels );
	R_StaticFree( icons[1].pixels );

	glfwSetFramebufferSizeCallback( window, resize_callback );
	glfwSetWindowCloseCallback( window, close_callback );
	glfwSetWindowSizeLimits( window, 200, 200, GLFW_DONT_CARE, GLFW_DONT_CARE );
	glfwMakeContextCurrent(window);

	glConfig.isFullscreen = a.fullScreen;
	
	if ( glConfig.isFullscreen ) {
		Sys_GrabMouseCursor( true );
	}
	
	return true;
}

/*
===================
GLimp_Init

This is the platform specific OpenGL initialization function.  It
is responsible for loading OpenGL, initializing it,
creating a window of the appropriate size, doing
fullscreen manipulations, etc.  Its overall responsibility is
to make sure that a functional OpenGL subsystem is operating
when it returns to the ref.

If there is any failure, the renderer will revert back to safe
parameters and try again.
===================
*/
bool GLimp_Init( glimpParms_t a ) {

	if (!GLX_Init(a)) {
		return false;
	}

	common->Printf( "...initializing QGL\n" );
	//load all function pointers available in the final context
	GLimp_LoadFunctions();
	
	return true;
}

/*
===================
GLimp_SetScreenParms
===================
*/
bool GLimp_SetScreenParms( glimpParms_t parms ) {
	return true;
}

bool Sys_GetCurrentMonitorResolution( int &width, int &height ) {
	// make sure glfw is initialized, as this function may be called before we get to that
	glfwInit();

	GLFWmonitor *monitor = GLimp_GetMonitor();
	if ( monitor != nullptr ) {
		const GLFWvidmode *mode = glfwGetVideoMode(monitor);
		width = mode->width;
		height = mode->height;
		return true;
	}
	return false;
}
