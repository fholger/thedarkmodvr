#ifndef _QGL_H_
#define _QGL_H_

// All GL/WGL/GLX features available at compile time must be enumerated here.
// Versions:
#define GL_VERSION_1_0 1
#define GL_VERSION_1_1 1
#define GL_VERSION_1_2 1
#define GL_VERSION_1_3 1
#define GL_VERSION_1_4 1
#define GL_VERSION_1_5 1
#define GL_VERSION_2_0 1
#define GL_VERSION_2_1 1
#define GL_VERSION_3_0 1
#define GL_VERSION_3_1 1
#define GL_VERSION_3_2 1
#define GL_VERSION_3_3 1
// Mandatory extensions:
#define GL_EXT_texture_compression_s3tc			1
// Optional extensions:
#define GL_ARB_vertex_program					1	//ARB2 assembly shader language
#define GL_ARB_fragment_program					1	//ARB2 assembly shader language
#define GL_EXT_texture_filter_anisotropic		1	//core since 4.6
#define GL_EXT_depth_bounds_test				1
#define GL_ARB_compatibility					1	//check context profile, ??3.1 only??
#define GL_KHR_debug							1	//core since 4.3
#define GL_ARB_stencil_texturing				1	//core since 4.3
#define GL_ARB_buffer_storage					1	//single VBO, core since 4.4
#define GL_ARB_multi_draw_indirect				1	//core since 4.3
#define GL_ARB_vertex_attrib_binding			1	//core since 4.3
#define GL_ARB_bindless_texture					1
#define GL_NV_shading_rate_image				1
#define GL_ARB_texture_storage					1
#include "glad.h"

#ifdef _WIN32
// Versions:
#define WGL_VERSION_1_0 1
// Mandatory extensions:
#define WGL_ARB_create_context					1
#define WGL_ARB_create_context_profile			1
#define WGL_ARB_pixel_format					1
// Optional extensions:
#define WGL_EXT_swap_control					1
#include "glad_wgl.h"
#endif

#ifdef __linux__
// Versions:
#define GLX_VERSION_1_0 1
#define GLX_VERSION_1_1 1
#define GLX_VERSION_1_2 1
#define GLX_VERSION_1_3 1
#define GLX_VERSION_1_4 1
// Mandatory extensions:
#define GLX_ARB_create_context					1
#define GLX_ARB_create_context_profile			1
#include "glad_glx.h"
#endif

#define QGL_REQUIRED_VERSION_MAJOR 3
#define QGL_REQUIRED_VERSION_MINOR 3

// Loads all GL/WGL/GLX functions from OpenGL library (using glad-generated loader).
// Note: no requirements are checked here, run GLimp_CheckRequiredFeatures afterwards.
void GLimp_LoadFunctions(bool inContext = true);

// Check and load all optional extensions.
// Fills extensions flags in glConfig and loads optional function pointers.
void GLimp_CheckRequiredFeatures();

// Unloads OpenGL library (does NOTHING in glad loader).
inline void GLimp_UnloadFunctions() {}

#endif
