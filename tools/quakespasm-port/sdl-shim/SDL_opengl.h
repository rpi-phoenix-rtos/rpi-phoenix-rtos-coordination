/*
 * SDL_opengl.h shim for the Quakespasm → Phoenix port: OpenGL declarations come
 * from the ported Mesa headers (external/mesa/include/GL), not from SDL. Quake
 * loads GL *extension* entry points at runtime via GL_GetProcAddress; the base
 * GL 1.x API is provided by libGL-phoenix at link time.
 */
#ifndef PHOENIX_QS_SDL_OPENGL_SHIM_H
#define PHOENIX_QS_SDL_OPENGL_SHIM_H

#include <GL/gl.h>
#include <GL/glext.h>

#endif /* PHOENIX_QS_SDL_OPENGL_SHIM_H */
