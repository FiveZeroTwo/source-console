#include "terminal.h"

#include "util.h"

#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

// ---- vterm callbacks ----
static int cb_damage(VTermRect, void *u) {
  ((App *)u)->dirty = true;
  return 1;
}
static int cb_movecursor(VTermPos pos, VTermPos, int, void *u) {
  App *a = (App *)u;
  a->currow = pos.row;
  a->curcol = pos.col;
  a->dirty = true;
  return 1;
}
static int cb_pushline(int cols, const VTermScreenCell *cells, void *u) {
  App *a = (App *)u;
  std::vector<Cell> ln(cols);
  for (int c = 0; c < cols; c++) {
    VTermColor f = cells[c].fg, b = cells[c].bg;
    vterm_screen_convert_color_to_rgb(a->vts, &f);
    vterm_screen_convert_color_to_rgb(a->vts, &b);
    const VTermScreenCellAttrs &at = cells[c].attrs;
    uint8_t attrs = (at.bold ? ATTR_BOLD : 0) | (at.underline ? ATTR_UNDERLINE : 0) |
                    (at.italic ? ATTR_ITALIC : 0) | (at.strike ? ATTR_STRIKE : 0) |
                    (at.reverse ? ATTR_REVERSE : 0);
    ln[c] = {cells[c].chars[0], f.rgb.red,  f.rgb.green, f.rgb.blue,
             b.rgb.red,         b.rgb.green, b.rgb.blue,  attrs,
             (uint8_t)cells[c].width};
  }
  a->sb.push_back(std::move(ln));
  if (a->sb.size() > 5000) a->sb.pop_front();
  a->have_sel = false;  // selection coords are now stale; copied text is kept
  return 1;
}
// libvterm reclaims scrollback into the screen when it grows / rewraps on
// resize: hand back our most recent line (reconstructed into VTermScreenCells).
static int cb_popline(int cols, VTermScreenCell *cells, void *u) {
  App *a = (App *)u;
  if (a->sb.empty()) return 0;
  const std::vector<Cell> &ln = a->sb.back();
  for (int c = 0; c < cols; c++) {
    VTermScreenCell sc;
    memset(&sc, 0, sizeof sc);
    sc.width = 1;
    const Cell *cell = c < (int)ln.size() ? &ln[c] : nullptr;
    sc.chars[0] = (cell && cell->cp) ? cell->cp : ' ';
    if (cell) {
      vterm_color_rgb(&sc.fg, cell->fr, cell->fg, cell->fb);
      vterm_color_rgb(&sc.bg, cell->br, cell->bg, cell->bb);
      sc.attrs.bold = (cell->attrs & ATTR_BOLD) != 0;
      sc.attrs.underline = (cell->attrs & ATTR_UNDERLINE) ? 1 : 0;
      sc.attrs.italic = (cell->attrs & ATTR_ITALIC) != 0;
      sc.attrs.strike = (cell->attrs & ATTR_STRIKE) != 0;
      sc.attrs.reverse = (cell->attrs & ATTR_REVERSE) != 0;
      if (cell->width) sc.width = cell->width;
    }
    cells[c] = sc;
  }
  a->sb.pop_back();
  a->dirty = true;
  return 1;
}
// Track terminal properties we care about: which mouse-reporting mode the
// foreground app has enabled (so input.cpp knows whether to forward the mouse).
static int cb_settermprop(VTermProp prop, VTermValue *val, void *u) {
  App *a = (App *)u;
  if (prop == VTERM_PROP_MOUSE) a->mouse_mode = val->number;
  return 1;
}
// `clear`'s \e[3J asks to drop the scrollback; flush our own buffer so the
// content is actually gone, not just scrolled out of view.
static int cb_sbclear(void *u) {
  App *a = (App *)u;
  a->sb.clear();
  a->scroll = 0;
  a->dirty = true;
  return 1;
}
const VTermScreenCallbacks SCB = {cb_damage,     nullptr,      cb_movecursor,
                                  cb_settermprop, nullptr,     nullptr,
                                  cb_pushline,   cb_popline,   cb_sbclear};
void out_cb(const char *s, size_t len, void *u) {
  App *a = (App *)u;
  if (a->master >= 0) (void)!write(a->master, s, len);
}

// Watch the child's output for bracketed-paste mode toggles (DECSET 2004
// h/l). A short tail is carried between calls so a sequence split across two
// reads is still caught.
static void scan_bracket(App *a, const char *s, size_t n) {
  std::string buf = a->vt_tail;
  buf.append(s, n);
  const std::string key = "\x1b[?2004";  // ESC [ ? 2 0 0 4, then 'h' or 'l'
  size_t pos = 0;
  while ((pos = buf.find(key, pos)) != std::string::npos) {
    size_t e = pos + key.size();
    if (e >= buf.size()) break;  // letter not arrived yet — leave in the tail
    if (buf[e] == 'h') a->bracket_paste = true;
    else if (buf[e] == 'l') a->bracket_paste = false;
    pos = e + 1;
  }
  size_t keep = std::min<size_t>(buf.size(), key.size() + 1);
  a->vt_tail = buf.substr(buf.size() - keep);
}

// ---- pty feed (input to terminal = output from child) ----
void feed(App *a, const char *s, size_t n) {
  scan_bracket(a, s, n);
  vterm_input_write(a->vt, s, n);
  vterm_screen_flush_damage(a->vts);
}
void feed(App *a, const std::string &s) { feed(a, s.c_str(), s.size()); }

// Push the current theme's colors into the vterm engine and drop the cached
// XftColors so the next render re-allocates them. Used at startup and on
// hot-reload.
void apply_theme_colors(App *a) {
  int r, g, b;
  VTermColor fg, bg;
  parse_hex(a->th.fg, r, g, b);
  vterm_color_rgb(&fg, r, g, b);
  parse_hex(a->th.bg, r, g, b);
  vterm_color_rgb(&bg, r, g, b);
  vterm_state_set_default_colors(a->vst, &fg, &bg);
  for (int i = 0; i < 16; i++) {
    VTermColor c;
    parse_hex(a->th.palette[i], r, g, b);
    vterm_color_rgb(&c, r, g, b);
    vterm_state_set_palette_color(a->vst, i, &c);
  }
  for (auto &kv : a->ccache) XftColorFree(a->dpy, a->vis, a->cmap, &kv.second);
  a->ccache.clear();
}
// Re-read the theme files and re-apply their colors live (hot-reload). Font
// changes need a restart; colors/palette update in place.
void reload_theme(App *a) {
  load_theme(a->th);
  apply_theme_colors(a);
  a->theme_mtime = theme_files_mtime();
  a->dirty = true;
}

// ===================== layout / sizing ===================================
void compute_grid(App *a) {
  a->r_out = {a->pad, a->title_h + a->pad, a->W - 2 * a->pad - a->sbw,
              a->H - a->title_h - a->input_h - 2 * a->pad};
  a->cols = std::max(8, a->r_out.w / a->cw);
  a->rows = std::max(2, a->r_out.h / a->ch);
}
void apply_size(App *a) {
  if (a->master >= 0) {
    struct winsize ws = {(unsigned short)a->rows, (unsigned short)a->cols,
                         (unsigned short)a->r_out.w, (unsigned short)a->r_out.h};
    ioctl(a->master, TIOCSWINSZ, &ws);
  }
  vterm_set_size(a->vt, a->rows, a->cols);
  vterm_screen_flush_damage(a->vts);
}

// (re)load the regular + bold fonts at the current size; update cell metrics.
void load_fonts(App *a) {
  for (XftFont **f : {&a->fontb, &a->fonti, &a->fontbi})
    if (*f && *f != a->font) XftFontClose(a->dpy, *f);
  if (a->font) XftFontClose(a->dpy, a->font);
  a->font = a->fontb = a->fonti = a->fontbi = nullptr;
  std::string pat =
      a->fontfam + ":size=" + std::to_string(a->fontsize) + ":dpi=96";
  a->font = XftFontOpenName(a->dpy, a->scr, pat.c_str());
  if (!a->font) return;
  a->fontb = XftFontOpenName(a->dpy, a->scr, (pat + ":weight=bold").c_str());
  a->fonti = XftFontOpenName(a->dpy, a->scr, (pat + ":slant=italic").c_str());
  a->fontbi = XftFontOpenName(a->dpy, a->scr,
                              (pat + ":weight=bold:slant=italic").c_str());
  if (!a->fontb) a->fontb = a->font;   // fall back to regular if a variant is
  if (!a->fonti) a->fonti = a->font;   // missing for this family
  if (!a->fontbi) a->fontbi = a->fontb;
  a->cw = a->font->max_advance_width;
  a->ch = a->font->ascent + a->font->descent;
  a->asc = a->font->ascent;
}

// Ctrl +/- font zoom: reload font, reflow the grid + pty.
void set_font_size(App *a, int sz) {
  a->fontsize = sz < 5 ? 5 : (sz > 48 ? 48 : sz);
  load_fonts(a);
  compute_grid(a);
  apply_size(a);
  a->dirty = true;
}

// (re)load the fixed chrome font at base_size * uiscale.
void load_uifont(App *a) {
  if (a->uifontb && a->uifontb != a->uifont) XftFontClose(a->dpy, a->uifontb);
  if (a->uifont && a->uifont != a->font) XftFontClose(a->dpy, a->uifont);
  a->uifont = a->uifontb = nullptr;
  int sz = (int)(a->fontsize_base * a->uiscale + 0.5);
  if (sz < 5) sz = 5;
  std::string pat = a->fontfam + ":size=" + std::to_string(sz) + ":dpi=96";
  a->uifont = XftFontOpenName(a->dpy, a->scr, pat.c_str());
  a->uifontb = XftFontOpenName(a->dpy, a->scr, (pat + ":weight=bold").c_str());
  if (!a->uifont) a->uifont = a->font;
  if (!a->uifontb) a->uifontb = a->uifont;
  a->uiasc = a->uifont->ascent;
  a->uich = a->uifont->ascent + a->uifont->descent;
}

// Recompute chrome font + all layout dimensions for the current uiscale.
void apply_uiscale(App *a) {
  a->title_h = a->S(TITLE_H);
  a->input_h = a->S(INPUT_H);
  a->pad = a->S(PAD);
  a->sbw = a->S(SBW);
  a->btn = a->S(BTN);
  load_uifont(a);
}

// Ctrl+Shift +/- : scale the whole UI (chrome font + dimensions).
void set_ui_scale(App *a, double s) {
  a->uiscale = s < 0.6 ? 0.6 : (s > 3.0 ? 3.0 : s);
  apply_uiscale(a);
  compute_grid(a);
  apply_size(a);
  a->dirty = true;
}

// Ctrl +/-/0 : scale the WHOLE app — terminal font AND chrome together, so the
// application gets uniformly bigger/smaller (uiscale tracks the font ratio).
void scale_app(App *a, int delta) {
  int sz = (delta == 0) ? a->fontsize_base : a->fontsize + delta;
  a->fontsize = sz < 5 ? 5 : (sz > 60 ? 60 : sz);
  a->uiscale = (double)a->fontsize / a->fontsize_base;
  load_fonts(a);
  apply_uiscale(a);
  compute_grid(a);
  apply_size(a);
  a->dirty = true;
}

// ---- current directory (read straight from the shell's /proc entry) ----
// `cd` is a shell builtin, so the shell process's cwd always reflects where we
// are; no shell cooperation (OSC 7, prompt hooks) needed.
std::string read_cwd(App *a) {
  if (a->child <= 0) return "";
  char path[4096];
  std::string link = "/proc/" + std::to_string((long)a->child) + "/cwd";
  ssize_t n = readlink(link.c_str(), path, sizeof(path) - 1);
  if (n <= 0) return a->cwd;  // keep last good value on transient failure
  path[n] = 0;
  std::string p(path);
  const char *home = getenv("HOME");
  if (home && *home) {
    size_t hl = strlen(home);
    if (p.compare(0, hl, home) == 0 && (p.size() == hl || p[hl] == '/'))
      p = "~" + p.substr(hl);
  }
  return p;
}
// "\x1b[38;2;R;G;Bm" for a #rrggbb color (24-bit truecolor SGR).
std::string sgr_fg(const std::string &hex) {
  int r, g, b;
  if (!parse_hex(hex, r, g, b)) return "";
  return "\x1b[38;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" +
         std::to_string(b) + "m";
}

// ===================== pty spawn =========================================
void spawn(App *a) {
  struct winsize ws = {(unsigned short)a->rows, (unsigned short)a->cols, 0, 0};
  a->child = forkpty(&a->master, nullptr, nullptr, &ws);
  if (a->child == 0) {
    const char *sh = getenv("SHELL");
    if (!sh) sh = "/usr/bin/zsh";
    setenv("TERM", "xterm-256color", 1);
    setenv("SRCTERM", "1", 1);
    std::string s(sh);
    if (s.size() >= 3 && s.compare(s.size() - 3, 3, "zsh") == 0)
      setenv("ZDOTDIR", (exe_dir() + "/runtime").c_str(), 1);
    execlp(sh, sh, (char *)nullptr);
    _exit(127);
  }
  int fl = fcntl(a->master, F_GETFL);
  fcntl(a->master, F_SETFL, fl | O_NONBLOCK);
}
