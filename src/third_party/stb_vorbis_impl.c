/* Builds stb_vorbis (public domain / MIT, see stb_vorbis/stb_vorbis.c) for
 * the game's .ogg sound effects and the Vorbis samples in its MO3 music,
 * decoding from memory only. */
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis/stb_vorbis.c"
