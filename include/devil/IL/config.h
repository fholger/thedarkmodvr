/* include/IL/config.h.  Generated from config.h.in by configure.  */
/* include/IL/config.h.in.  Generated from configure.ac by autoheader.  */

/* Altivec extension found */
/* #undef ALTIVEC_GCC */

/* "Enable debug code features" */
/* #undef DEBUG */

/* PPC_ASM assembly found */
/* #undef GCC_PCC_ASM */

/* X86_64_ASM assembly found */
/* #undef GCC_X86_64_ASM */

/* X86_ASM assembly found */
#if !defined(__ppc__)
#define GCC_X86_ASM 
#endif

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Support Allegro API */
/* #undef ILUT_USE_ALLEGRO */

/* Support DirectX8 API */
/* #undef ILUT_USE_DIRECTX8 */

/* Support DirectX9 API */
/* #undef ILUT_USE_DIRECTX9 */

/* Support OpenGL API */
#define ILUT_USE_OPENGL 

/* Support SDL API */
/* #undef ILUT_USE_SDL */

/* Support X11 API */
#define ILUT_USE_X11 

/* Support X11 XShm extension */
#define ILUT_USE_XSHM 

/* #undef IL_NO_BMP */
#define IL_NO_CUT
#define IL_NO_CHEAD
#define IL_NO_DCX
/* #undef IL_NO_DDS */
/* #undef IL_NO_DOOM */
/* #undef IL_NO_GIF */
#define IL_NO_HDR
#define IL_NO_ICO
#define IL_NO_ICNS
#define IL_NO_JP2
/* #undef IL_NO_JPG */
#define IL_NO_LCMS
#define IL_NO_LIF
#define IL_NO_MDL
#define IL_NO_MNG
#define IL_NO_PCD
#define IL_NO_PCX
#define IL_NO_PIC
#define IL_NO_PIX
/* #undef IL_NO_PNG */
#define IL_NO_PNM
#define IL_NO_PSD
#define IL_NO_PSP
#define IL_NO_PXR
#define IL_NO_RAW
#define IL_NO_SGI
/* #undef IL_NO_TGA */
#define IL_NO_TIF
#define IL_NO_WAL
#define IL_NO_XPM
#define IL_NO_EXR

/* Use libjpeg without modification. always enabled. */
#define IL_USE_JPEGLIB_UNMODIFIED 

/* LCMS include without lcms/ support */
#define LCMS_NODIRINCLUDE 

/* Building on Mac OS X */
/* #undef MAX_OS_X */

/* memalign memory allocation */
#define MEMALIGN 

/* mm_malloc memory allocation */
#if !defined(__ppc__)
#define MM_MALLOC 
#endif

/* Name of package */
#define PACKAGE "DevIL"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT ""

/* Define to the full name of this package. */
#define PACKAGE_NAME ""

/* Define to the full name and version of this package. */
#define PACKAGE_STRING ""

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME ""

/* Define to the version of this package. */
#define PACKAGE_VERSION ""

/* posix_memalign memory allocation */
#define POSIX_MEMALIGN 

/* restric keyword available */
//#define RESTRICT_KEYWORD 

/* SSE extension found */
#define SSE 

/* SSE2 extension found */
/* #undef SSE2 */

/* SSE3 extension found */
/* #undef SSE3 */

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* valloc memory allocation */
#define VALLOC 

/* Memory must be vector aligned */
#define VECTORMEM 

/* Version number of package */
#define VERSION "1.7.2"

/* Define to 1 if your processor stores words with the most significant byte
   first (like Motorola and SPARC, unlike Intel and VAX). */
/* #undef WORDS_BIGENDIAN */

/* Define to 1 if the X Window System is missing or not being used. */
/* #undef X_DISPLAY_MISSING */
