#include "render.h"

#include "input.h"
#include "png.h"
#include "util.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

// ---- colors ----
XftColor *col(App *a, int r, int g, int b) {
  unsigned key = (r << 16) | (g << 8) | b;
  auto it = a->ccache.find(key);
  if (it != a->ccache.end()) return &it->second;
  XRenderColor rc = {(unsigned short)(r * 257), (unsigned short)(g * 257),
                     (unsigned short)(b * 257), 0xffff};
  XftColor c;
  XftColorAllocValue(a->dpy, a->vis, a->cmap, &rc, &c);
  a->ccache[key] = c;
  return &a->ccache[key];
}
XftColor *colh(App *a, const std::string &hex) {
  int r, g, b;
  parse_hex(hex, r, g, b);
  return col(a, r, g, b);
}

// ---- drawing primitives on the back buffer ----
static void fillr(App *a, int x, int y, int w, int h, XftColor *c) {
  XSetForeground(a->dpy, a->gc, c->pixel);
  XFillRectangle(a->dpy, a->buf, a->gc, x, y, w, h);
}
static void line(App *a, int x1, int y1, int x2, int y2, XftColor *c) {
  XSetForeground(a->dpy, a->gc, c->pixel);
  XDrawLine(a->dpy, a->buf, a->gc, x1, y1, x2, y2);
}
static void bevel(App *a, int x, int y, int w, int h, bool raised) {
  XftColor *lt = colh(a, raised ? a->th.light : a->th.dark);
  XftColor *dk = colh(a, raised ? a->th.dark : a->th.light);
  line(a, x, y, x + w - 1, y, lt);
  line(a, x, y, x, y + h - 1, lt);
  line(a, x, y + h - 1, x + w - 1, y + h - 1, dk);
  line(a, x + w - 1, y, x + w - 1, y + h - 1, dk);
}
// chrome/UI text — always the fixed-size UI font (unaffected by Ctrl+/- zoom).
static void text(App *a, int x, int y, const std::string &s, XftColor *c,
                 bool bold = false) {
  XftDrawStringUtf8(a->xd, c, bold ? a->uifontb : a->uifont, x, y,
                    (const FcChar8 *)s.c_str(), s.size());
}

// ===================== rendering =========================================
void render(App *a) {
  XftColor *face = colh(a, a->th.face), *bg = colh(a, a->th.bg);
  bool pt = key_to_app(a);
  // frame
  fillr(a, 0, 0, a->W, a->H, face);
  XSetForeground(a->dpy, a->gc, colh(a, a->th.outer)->pixel);
  XDrawRectangle(a->dpy, a->buf, a->gc, 0, 0, a->W - 1, a->H - 1);
  bevel(a, 1, 1, a->W - 2, a->H - 2, true);

  // title bar
  a->r_title = {1, 1, a->W - 2, a->title_h - 1};
  bevel(a, a->r_title.x, a->r_title.y, a->r_title.w, a->r_title.h, true);
  text(a, a->S(10), 1 + a->uiasc + a->S(3), "Console", colh(a, a->th.title_text), true);
  a->r_close = {a->W - 2 - a->btn, a->S(3), a->btn, a->title_h - a->S(6)};
  a->r_min = {a->r_close.x - a->btn - 2, a->S(3), a->btn, a->title_h - a->S(6)};
  for (auto *br : {&a->r_min, &a->r_close}) {
    bevel(a, br->x, br->y, br->w, br->h, true);
    fillr(a, br->x + 1, br->y + 1, br->w - 2, br->h - 2, face);
  }
  text(a, a->r_min.x + a->S(7), 1 + a->uiasc + a->S(2), "_", colh(a, a->th.title_text));
  text(a, a->r_close.x + a->S(8), 1 + a->uiasc + a->S(2), "x", colh(a, a->th.title_text));
  // cwd after the title, dimmed, left-truncated to fit before the buttons.
  if (a->th.show_cwd && !a->cwd.empty()) {
    XGlyphInfo gi;
    XftTextExtentsUtf8(a->dpy, a->uifontb, (const FcChar8 *)"Console", 7, &gi);
    int cx = a->S(10) + gi.xOff + a->S(14), maxx = a->r_min.x - a->S(8);
    std::string s = a->cwd;
    auto width = [&](const std::string &t) {
      XGlyphInfo g;
      XftTextExtentsUtf8(a->dpy, a->uifont, (const FcChar8 *)t.c_str(), t.size(), &g);
      return g.xOff;
    };
    while (s.size() > 1 && cx + width("…" + s) > maxx) s.erase(0, 1);
    if (s != a->cwd) s = "…" + s;
    if (cx < maxx) text(a, cx, 1 + a->uiasc + a->S(3), s, colh(a, a->th.dark));
  }

  // output panel (sunken)
  Rect o = a->r_out;
  bevel(a, o.x, o.y, o.w + a->sbw, o.h, false);
  fillr(a, o.x + 1, o.y + 1, o.w - 1, o.h - 2, bg);

  // grid (scrollback + live screen, honoring scroll offset)
  int N = a->sb.size();
  int defr, defg, defb;
  parse_hex(a->th.bg, defr, defg, defb);
  for (int row = 0; row < a->rows; row++) {
    int vidx = N - a->scroll + row;
    int py = o.y + 1 + row * a->ch;
    for (int c = 0; c < a->cols; c++) {
      int px = o.x + 3 + c * a->cw;
      uint32_t cp = 0;
      int fr = 0, fgc = 0, fb = 0, br = defr, bgc = defg, bb = defb;
      bool bold = false;
      if (vidx < N) {  // scrollback
        if (c >= (int)a->sb[vidx].size()) continue;
        const Cell &cell = a->sb[vidx][c];
        cp = cell.cp;
        fr = cell.fr; fgc = cell.fg; fb = cell.fb;
        br = cell.br; bgc = cell.bg; bb = cell.bb;
        bold = cell.bold;
      } else {  // live screen
        VTermPos pos = {vidx - N, c};
        VTermScreenCell cell;
        if (!vterm_screen_get_cell(a->vts, pos, &cell)) continue;
        if (cell.width == 0) continue;
        VTermColor f = cell.fg, b = cell.bg;
        vterm_screen_convert_color_to_rgb(a->vts, &f);
        vterm_screen_convert_color_to_rgb(a->vts, &b);
        if (cell.attrs.reverse) std::swap(f, b);
        cp = cell.chars[0];
        fr = f.rgb.red; fgc = f.rgb.green; fb = f.rgb.blue;
        br = b.rgb.red; bgc = b.rgb.green; bb = b.rgb.blue;
        bold = cell.attrs.bold;
      }
      if (br != defr || bgc != defg || bb != defb)
        fillr(a, px, py, a->cw, a->ch, col(a, br, bgc, bb));
      if (cp && cp != ' ') {
        char u8[8];
        int n = 0;
        if (cp < 0x80)
          u8[n++] = cp;
        else if (cp < 0x800) {
          u8[n++] = 0xc0 | (cp >> 6);
          u8[n++] = 0x80 | (cp & 0x3f);
        } else if (cp < 0x10000) {
          u8[n++] = 0xe0 | (cp >> 12);
          u8[n++] = 0x80 | ((cp >> 6) & 0x3f);
          u8[n++] = 0x80 | (cp & 0x3f);
        } else {
          u8[n++] = 0xf0 | (cp >> 18);
          u8[n++] = 0x80 | ((cp >> 12) & 0x3f);
          u8[n++] = 0x80 | ((cp >> 6) & 0x3f);
          u8[n++] = 0x80 | (cp & 0x3f);
        }
        XftDrawStringUtf8(a->xd, col(a, fr, fgc, fb), bold ? a->fontb : a->font,
                          px, py + a->asc, (const FcChar8 *)u8, n);
      }
    }
  }

  // terminal cursor — only when a foreground app owns the tty. At the idle
  // shell there's no prompt and the box owns the keyboard, so a cursor in the
  // output would just be a stray block on an empty line.
  if (pt && a->scroll == 0 && a->currow >= 0 && a->currow < a->rows &&
      a->curcol >= 0 && a->curcol < a->cols) {
    int px = o.x + 3 + a->curcol * a->cw, py = o.y + 1 + a->currow * a->ch;
    fillr(a, px, py, a->cw, a->ch, colh(a, a->th.cursor));  // solid, char inverted
    VTermPos cpos = {a->currow, a->curcol};
    VTermScreenCell cell;
    if (vterm_screen_get_cell(a->vts, cpos, &cell) && cell.chars[0] &&
        cell.chars[0] != ' ') {
      char u8[8];
      int ln = enc_utf8(cell.chars[0], u8);
      XftDrawStringUtf8(a->xd, bg, a->font, px, py + a->asc,
                        (const FcChar8 *)u8, ln);
    }
  }

  // scrollbar
  int sbx = o.x + o.w, sby = o.y + 1, sbh = o.h - 2;
  fillr(a, sbx, sby, a->sbw, sbh, colh(a, a->th.dark));
  int total = N + a->rows;
  int thumb = total > 0 ? std::max(20, sbh * a->rows / total) : sbh;
  int tpos = total > a->rows
                 ? (sbh - thumb) * (N - a->scroll) / std::max(1, N)
                 : 0;
  bevel(a, sbx, sby + tpos, a->sbw, thumb, true);
  fillr(a, sbx + 1, sby + tpos + 1, a->sbw - 2, thumb - 2, face);

  // input bar
  int iy = a->H - a->input_h;
  fillr(a, 1, iy, a->W - 2, a->input_h - 1, face);
  text(a, a->S(8), iy + a->uiasc + a->S(6), "]", colh(a, a->th.accent), true);
  a->r_submit = {a->W - 2 - a->S(92), iy + a->S(3), a->S(90), a->input_h - a->S(7)};
  Rect fld = {a->S(22), iy + a->S(3), a->r_submit.x - a->S(22) - a->S(6), a->input_h - a->S(7)};
  bevel(a, fld.x, fld.y, fld.w, fld.h, false);
  fillr(a, fld.x + 1, fld.y + 1, fld.w - 2, fld.h - 2, colh(a, a->th.entry_bg));
  XftColor *fldfg = colh(a, a->th.fg);
  int tx = fld.x + a->S(6), ty = fld.y + a->uiasc + a->S(3);
  if (!a->input.empty())
    text(a, tx, ty, a->input, fldfg);
  else if (pt)
    text(a, tx, ty, "passthrough — keys go to the terminal  (Ctrl+` for box)",
         colh(a, a->th.dark));
  else
    text(a, tx, ty, "enter command…", colh(a, a->th.dark));
  if (!pt) {  // box caret whenever the box owns the keyboard
    XGlyphInfo gi;
    XftTextExtentsUtf8(a->dpy, a->uifont, (const FcChar8 *)a->input.c_str(),
                       a->caret, &gi);
    int cx = tx + gi.xOff;
    line(a, cx, fld.y + a->S(4), cx, fld.y + fld.h - a->S(4),
         colh(a, a->th.cursor));
  }
  // submit button (text centered)
  bevel(a, a->r_submit.x, a->r_submit.y, a->r_submit.w, a->r_submit.h, true);
  fillr(a, a->r_submit.x + 1, a->r_submit.y + 1, a->r_submit.w - 2,
        a->r_submit.h - 2, face);
  XGlyphInfo sg;
  XftTextExtentsUtf8(a->dpy, a->uifontb, (const FcChar8 *)"Submit", 6, &sg);
  text(a, a->r_submit.x + (a->r_submit.w - sg.xOff) / 2,
       a->r_submit.y + a->uiasc + (a->r_submit.h - a->uich) / 2, "Submit",
       colh(a, a->th.title_text), true);

  // resize grip — big, accent-colored diagonal hatch in the bottom-right corner
  int gs = a->S(20);
  a->r_grip = {a->W - gs - 1, a->H - gs - 1, gs, gs};
  for (int i = 1; i <= 5; i++) {
    int off = i * a->S(4);
    line(a, a->W - 2 - off, a->H - 3, a->W - 3, a->H - 2 - off,
         colh(a, a->th.accent));
  }

  // completion popup (above the input bar)
  if (a->popup && !a->matches.empty()) {
    int rowh = a->uich + 2;
    int ph = (int)a->matches.size() * rowh + 2;
    int pw = a->S(320), pxx = a->S(22), pyy = iy - ph - 2;
    XSetForeground(a->dpy, a->gc, colh(a, a->th.outer)->pixel);
    XDrawRectangle(a->dpy, a->buf, a->gc, pxx, pyy, pw - 1, ph - 1);
    fillr(a, pxx + 1, pyy + 1, pw - 2, ph - 2, colh(a, a->th.entry_bg));
    for (int i = 0; i < (int)a->matches.size(); i++) {
      int ry = pyy + 1 + i * rowh;
      XftColor *fgc = colh(a, a->th.fg);
      if (i == a->sel) {
        fillr(a, pxx + 1, ry, pw - 2, rowh, colh(a, a->th.accent));
        fgc = colh(a, "#201808");
      }
      text(a, pxx + a->S(10), ry + a->uiasc, a->matchdisp[i], fgc, i == a->sel);
    }
  }

  XCopyArea(a->dpy, a->buf, a->win, a->gc, 0, 0, a->W, a->H, 0, 0);
}

// ===================== screenshot (XGetImage -> PNG) =====================
void screenshot(App *a, const char *path) {
  XImage *img = XGetImage(a->dpy, a->buf, 0, 0, a->W, a->H, AllPlanes, ZPixmap);
  if (!img) return;
  // XGetImage on a Pixmap leaves the masks zeroed; for our 24-bit TrueColor the
  // pixel is packed 0x00RRGGBB, so extract with fixed shifts.
  std::vector<uint8_t> rgb((size_t)a->W * a->H * 3);
  for (int y = 0; y < a->H; y++)
    for (int x = 0; x < a->W; x++) {
      unsigned long p = XGetPixel(img, x, y);
      size_t o = ((size_t)y * a->W + x) * 3;
      rgb[o] = (p >> 16) & 0xff;
      rgb[o + 1] = (p >> 8) & 0xff;
      rgb[o + 2] = p & 0xff;
    }
  XDestroyImage(img);
  fprintf(stderr, "SHOT %s %dx%d ok=%d\n", path, a->W, a->H,
          write_png(path, a->W, a->H, rgb.data()));
}
