// Scrollback search (Ctrl+Shift+F): a live query over the buffer that jumps the
// view between matches. Rendering of the query bar + match highlighting lives in
// render.cpp; this module owns the state, the hit list, and navigation.
#pragma once
#include <X11/keysym.h>

#include "app.h"

void search_open(App *a);   // enter search mode (clears the previous query)
void search_close(App *a);  // leave search mode (keeps the current scroll pos)
// Handle a key while searching: edit the query, navigate matches, or Esc out.
void search_key(App *a, KeySym ks, const char *text, int len);
