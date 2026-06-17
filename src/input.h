// Keyboard + mouse handling and command submission. Routes keys either to the
// running program (passthrough, via libvterm) or to the command box, and drives
// window move/resize through the WM.
#pragma once
#include "app.h"

void submit(App *a);  // run the command box's contents in the shell

// Passthrough is "effective" only when the user wants it AND a foreground app
// actually owns the tty; otherwise the command box owns the keyboard.
bool app_owns_tty(App *a);
bool key_to_app(App *a);

void on_key(App *a, XKeyEvent *ev);
void on_button(App *a, XButtonEvent *ev);

// Ask the WM to start an interactive move/resize (_NET_WM_MOVERESIZE dir).
void wm_moveresize(App *a, int xr, int yr, int dir);
