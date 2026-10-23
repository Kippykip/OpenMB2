/* Builds stb_vorbis (public domain / MIT, see stb_vorbis.c) for the game's
 * .ogg sound effects, decoding from memory only. */
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
