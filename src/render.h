// Everything that draws to the back buffer: the XftColor cache, the VGUI chrome
// + terminal grid compositor (render), and the debug screenshot path.
#pragma once
#include "app.h"

// Allocate-or-cache an XftColor for an RGB triple / a "#rrggbb" string.
XftColor *col(App *a, int r, int g, int b);
XftColor *colh(App *a, const std::string &hex);

// Composite the whole window (frame, grid, scrollbar, input bar, popup) and
// blit the back buffer to the window.
void render(App *a);

// Capture the back buffer to a PNG (debug; SRCTERM_SHOOT).
void screenshot(App *a, const char *path);
