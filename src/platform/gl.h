/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

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

#define GL_FUNCTIONS(X)                                        \
    X(PFNGLGETSTRINGPROC, GetString)                           \
    X(PFNGLGETERRORPROC, GetError)                             \
    X(PFNGLVIEWPORTPROC, Viewport)                             \
    X(PFNGLCLEARCOLORPROC, ClearColor)                         \
    X(PFNGLCLEARPROC, Clear)                                   \
    X(PFNGLENABLEPROC, Enable)                                 \
    X(PFNGLDISABLEPROC, Disable)                               \
    X(PFNGLBLENDFUNCPROC, BlendFunc)                           \
    X(PFNGLDEPTHMASKPROC, DepthMask)                           \
    X(PFNGLDEPTHFUNCPROC, DepthFunc)                           \
    X(PFNGLCULLFACEPROC, CullFace)                             \
    X(PFNGLFRONTFACEPROC, FrontFace)                           \
    X(PFNGLCREATESHADERPROC, CreateShader)                     \
    X(PFNGLSHADERSOURCEPROC, ShaderSource)                     \
    X(PFNGLCOMPILESHADERPROC, CompileShader)                   \
    X(PFNGLGETSHADERIVPROC, GetShaderiv)                       \
    X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog)             \
    X(PFNGLDELETESHADERPROC, DeleteShader)                     \
    X(PFNGLCREATEPROGRAMPROC, CreateProgram)                   \
    X(PFNGLATTACHSHADERPROC, AttachShader)                     \
    X(PFNGLBINDATTRIBLOCATIONPROC, BindAttribLocation)         \
    X(PFNGLLINKPROGRAMPROC, LinkProgram)                       \
    X(PFNGLGETPROGRAMIVPROC, GetProgramiv)                     \
    X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog)           \
    X(PFNGLDELETEPROGRAMPROC, DeleteProgram)                   \
    X(PFNGLUSEPROGRAMPROC, UseProgram)                         \
    X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation)         \
    X(PFNGLUNIFORM1IPROC, Uniform1i)                           \
    X(PFNGLUNIFORM1FPROC, Uniform1f)                           \
    X(PFNGLUNIFORM2FPROC, Uniform2f)                           \
    X(PFNGLUNIFORM3FPROC, Uniform3f)                           \
    X(PFNGLUNIFORM4FVPROC, Uniform4fv)                         \
    X(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv)             \
    X(PFNGLGENBUFFERSPROC, GenBuffers)                         \
    X(PFNGLBINDBUFFERPROC, BindBuffer)                         \
    X(PFNGLBUFFERDATAPROC, BufferData)                         \
    X(PFNGLDELETEBUFFERSPROC, DeleteBuffers)                   \
    X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer)       \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray) \
    X(PFNGLDRAWARRAYSPROC, DrawArrays)                         \
    X(PFNGLGENTEXTURESPROC, GenTextures)                       \
    X(PFNGLBINDTEXTUREPROC, BindTexture)                       \
    X(PFNGLACTIVETEXTUREPROC, ActiveTexture)                   \
    X(PFNGLTEXIMAGE2DPROC, TexImage2D)                         \
    X(PFNGLTEXPARAMETERIPROC, TexParameteri)                   \
    X(PFNGLDELETETEXTURESPROC, DeleteTextures)                 \
    X(PFNGLPIXELSTOREIPROC, PixelStorei)                       \
    X(PFNGLREADPIXELSPROC, ReadPixels)

typedef struct GlFunctions {
#define X(type, name) type name;
    GL_FUNCTIONS(X)
#undef X
} GlFunctions;

extern GlFunctions gl;

/* Load all functions from the current context. False if any is missing. */
bool gl_load_functions(void);

#endif
