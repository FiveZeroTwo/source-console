// The libvterm engine + pty side of the app: screen callbacks, feeding pty bytes
// into vterm, spawning the shell, grid/font sizing and the zoom/scale operations.
#pragma once
#include "app.h"

// vterm screen callbacks + the child-response output callback (terminal -> pty).
extern const VTermScreenCallbacks SCB;
void out_cb(const char *s, size_t len, void *u);

// Feed child output bytes into the terminal engine and flush damage.
void feed(App *a, const char *s, size_t n);
void feed(App *a, const std::string &s);

// Layout / sizing.
void compute_grid(App *a);   // recompute rows/cols + the output rect for W/H
void apply_size(App *a);     // push the current rows/cols to the pty + vterm
void load_fonts(App *a);     // (re)load grid regular+bold fonts; update metrics
void load_uifont(App *a);    // (re)load the fixed chrome font at base*uiscale
void apply_uiscale(App *a);  // recompute chrome font + scaled dimensions

// Zoom / scale operations (bound to Ctrl +/- and Ctrl+Shift +/-).
void set_font_size(App *a, int sz);   // grid font only
void set_ui_scale(App *a, double s);  // chrome only
void scale_app(App *a, int delta);    // whole app (grid font + chrome together)

// Shell process.
void spawn(App *a);             // forkpty() the shell, set TERM/ZDOTDIR
std::string read_cwd(App *a);   // the shell's cwd from /proc/<child>/cwd
std::string sgr_fg(const std::string &hex);  // 24-bit truecolor SGR for #rrggbb
