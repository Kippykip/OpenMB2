#include "gl.h"
#include "../core/log.h"

#include <SDL.h>

GlFunctions gl;

bool gl_load_functions(void)
{
    bool ok = true;

#define X(type, name)                                              \
    gl.name = (type)SDL_GL_GetProcAddress("gl" #name);             \
    if (gl.name == NULL) {                                         \
        log_error("gl: missing function gl%s", #name);             \
        ok = false;                                                \
    }
    GL_FUNCTIONS(X)
#undef X

    return ok;
}
