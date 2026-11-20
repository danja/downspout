#pragma once

// Declaration-only include for the vendored single-file decoder. The
// implementation is compiled separately as third_party/stb_vorbis/stb_vorbis.c.
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY
