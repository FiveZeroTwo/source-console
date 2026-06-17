// Minimal zlib-free PNG writer used only for debug screenshots (SRCTERM_SHOOT).
// Emits a 24-bit truecolor PNG with stored (uncompressed) deflate blocks.
#pragma once
#include <cstdint>

// Write `w`x`h` RGB (3 bytes/pixel, row-major) to `path`. Returns success.
bool write_png(const char *path, int w, int h, const uint8_t *rgb);
