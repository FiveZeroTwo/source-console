// App = the entire runtime state of srcterm: the X11 connection and back buffer,
// the libvterm engine + pty, the scrollback grid, and the command-box/completion
// state. Almost every function in the app takes an `App *` and mutates it.
#pragma once
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <vterm.h>

#include <sys/types.h>

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "theme.h"

// Cell attribute bit flags (stored in Cell::attrs and read back in render).
enum CellAttr {
  ATTR_BOLD = 1, ATTR_UNDERLINE = 2, ATTR_ITALIC = 4,
  ATTR_STRIKE = 8, ATTR_DIM = 16, ATTR_REVERSE = 32,
};
struct Cell {  // a stored scrollback cell
  uint32_t cp;
  uint8_t fr, fg, fb, br, bg, bb;
  uint8_t attrs;  // CellAttr flags
  uint8_t width;  // 1 = normal, 2 = double-width (CJK/emoji), 0 = trailing half
};
struct Rect {
  int x, y, w, h;
  bool hit(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

// Base (unscaled) chrome dimensions; runtime values live in App and are derived
// from these via App::S() at the current uiscale.
inline constexpr int TITLE_H = 26, INPUT_H = 32, PAD = 4, SBW = 14, BTN = 26;
inline constexpr int MAXCOMP = 14;  // max completion rows shown in the popup

struct App {
  Theme th;
  // X
  Display *dpy = nullptr;
  int scr = 0;
  Window win = 0, root = 0;
  Visual *vis = nullptr;
  Colormap cmap = 0;
  Pixmap buf = 0;
  GC gc = 0;
  XftDraw *xd = nullptr;
  XftFont *font = nullptr, *fontb = nullptr;      // terminal grid (zoomable)
  XftFont *fonti = nullptr, *fontbi = nullptr;    // italic + bold-italic
  XftFont *uifont = nullptr, *uifontb = nullptr;  // chrome (fixed size)
  std::string fontfam = "monospace";
  int fontsize = 11;              // current point size (for zoom)
  int fontsize_base = 11;        // configured size (Ctrl+0 reset target)
  int cw = 8, ch = 16, asc = 12;  // cell metrics
  int uiasc = 12, uich = 16;      // chrome font ascent / line height
  bool passthrough = true;        // true: keys -> running app (modern terminal)
  double uiscale = 1.0;           // chrome scale (Ctrl+Shift +/-/0)
  int title_h = TITLE_H, input_h = INPUT_H, pad = PAD, sbw = SBW, btn = BTN;
  int S(int v) const { return (int)(v * uiscale + 0.5); }  // scale a dimension
  int W = 1100, H = 700;
  std::map<unsigned, XftColor> ccache;
  // vterm
  VTerm *vt = nullptr;
  VTermScreen *vts = nullptr;
  VTermState *vst = nullptr;   // for line-info (resize reflow)
  int mouse_mode = 0;          // VTERM_PROP_MOUSE_* the app enabled (0 = none)
  bool bracket_paste = false;  // app enabled bracketed paste (DECSET 2004)
  std::string vt_tail;         // last bytes of pty output (split-sequence scan)
  int master = -1;
  pid_t child = -1;
  int rows = 24, cols = 80;
  int currow = 0, curcol = 0;
  std::deque<std::vector<Cell>> sb;  // scrollback
  int scroll = 0;                    // lines scrolled up from bottom
  // input + completion
  std::string input;
  size_t caret = 0;
  std::vector<std::string> history;
  int hist = 0;
  std::vector<std::string> commands;   // execset ∪ aliases, sorted (completion)
  std::set<std::string> execset;       // PATH executables, scanned once
  std::vector<std::string> aliases;    // shell aliases, refreshed by rescan
  bool is_zsh = true;                  // live alias dump uses zsh syntax
  bool alias_want = false;             // a rescan was requested
  bool alias_pending = false;          // dump command sent; awaiting its file
  std::string alias_tmp;               // temp file the running shell dumps to
  std::vector<std::string> matches;    // text inserted when accepted
  std::vector<std::string> matchdisp;  // short label shown in the popup
  int sel = -1;
  bool popup = false;
  bool fuzzy = false;          // current matches came from fuzzy (not prefix) search
  bool match_is_path = false;  // matches are filesystem paths (vs command names)
  std::string cwd;             // shell's current dir (from /proc/<shell>/cwd)
  // hit rects
  Rect r_title, r_min, r_close, r_submit, r_grip, r_out;
  bool running = true, dirty = true;
  // X selection / clipboard. Selection coords are (line, col) in the visual
  // buffer: line < |sb| indexes scrollback, else the live row (line - |sb|).
  bool selecting = false;  // a mouse drag is in progress
  bool have_sel = false;   // a finalized selection (highlight + seltext) exists
  bool mouse_to_app = false;   // current press was forwarded to the app's mouse
  int sel_l0 = 0, sel_c0 = 0;  // drag anchor
  int sel_l1 = 0, sel_c1 = 0;  // drag head (current)
  std::string seltext;     // PRIMARY selection text we serve
  std::string clip;        // CLIPBOARD text we serve (Ctrl+Shift+C)
  bool paste_to_pty = false;   // pending paste: deliver to the app vs the box
  Atom a_primary = 0, a_clipboard = 0, a_utf8 = 0, a_targets = 0, a_seldata = 0;
  // multi-click + drag tracking
  Time last_click = 0;     // time of the previous button-1 press
  int click_count = 0;     // 1=single 2=double(word) 3=triple(line)
  int click_x = 0, click_y = 0;  // where the previous click landed
  int drag_x = 0, drag_y = 0;    // last drag position (for edge auto-scroll)
};
