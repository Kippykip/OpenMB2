/*
 * gl.h - the OpenGL ES 2 functions the renderer uses.
 *
 * We load every function through SDL at runtime instead of linking a GL
 * library. That way the same code runs on a desktop GL driver, a GLES2
 * driver (handhelds, phones) and WebGL (Emscripten) without any build
 * changes. Call them as gl.Clear(...), gl.Viewport(...) and so on.
 *
 * To use a new GL function, add it to GL_FUNCTIONS below.
 */
#ifndef OPENMB2_GL_H
#define OPENMB2_GL_H

/* Use SDL's bundled copy of the GLES2 headers, so none need installing. */
#define SDL_USE_BUILTIN_OPENGL_DEFINITIONS 1
#include <SDL_opengles2.h>
#include <stdbool.h>

#define GL_FUNCTIONS(X)                    \
    X(PFNGLGETSTRINGPROC, GetString)       \
    X(PFNGLVIEWPORTPROC, Viewport)         \
    X(PFNGLCLEARCOLORPROC, ClearColor)     \
    X(PFNGLCLEARPROC, Clear)

typedef struct GlFunctions {
#define X(type, name) type name;
    GL_FUNCTIONS(X)
#undef X
} GlFunctions;

extern GlFunctions gl;

/* Load all functions from the current context. False if any is missing. */
bool gl_load_functions(void);

#endif
