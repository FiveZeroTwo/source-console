#include "input.h"

#include "completion.h"
#include "terminal.h"
#include "util.h"

#include <X11/keysym.h>

#include <unistd.h>

#include <algorithm>
#include <cstring>

// ===================== submit ============================================
void submit(App *a) {
  std::string cmd = a->input;
  if (!strip(cmd).empty()) a->history.push_back(cmd);
  a->hist = a->history.size();
  // The shell echoes nothing (output-only mode), so echo the command ourselves
  // as a prompt line — optionally prefixed with the cwd — above its output.
  if (a->th.echo_command && !strip(cmd).empty()) {
    std::string line;
    if (a->th.show_cwd && !a->cwd.empty())
      line += sgr_fg(a->th.highlight) + a->cwd + " ";
    line += sgr_fg(a->th.accent) + "] " + sgr_fg(a->th.fg) + cmd + "\x1b[0m\r\n";
    feed(a, line);
  }
  // Feed the line to the output-only shell (no zle, no echo) + Enter; only the
  // command's output comes back — see runtime/.zshrc.
  std::string out = cmd + "\r";
  (void)!write(a->master, out.c_str(), out.size());
  // Hook: commands that (re)define aliases → refresh the completion list after.
  std::string c0 = strip(cmd);
  std::string w0 = c0.substr(0, c0.find_first_of(" \t"));
  if (w0 == "alias" || w0 == "unalias" || w0 == "source" || w0 == ".")
    request_alias_rescan(a);
  a->input.clear();
  a->caret = 0;
  a->popup = false;
  a->matches.clear();
  a->scroll = 0;
  a->dirty = true;
}

// Keys should reach the running program only when a foreground application
// (not the idle shell) actually owns the terminal. At an idle prompt the tty's
// foreground process group is the shell itself; a launched job gets its own pgrp
// via job control, so a mismatch means "an app is running and expects input".
bool app_owns_tty(App *a) {
  if (a->master < 0 || a->child <= 0) return false;
  pid_t fg = tcgetpgrp(a->master);
  return fg > 0 && fg != a->child;
}
// Effective passthrough: the user wants keys in the terminal AND an app is there
// to receive them. Otherwise the command box owns the keyboard.
bool key_to_app(App *a) { return a->passthrough && app_owns_tty(a); }

// ===================== window move/resize via the WM =====================
void wm_moveresize(App *a, int xr, int yr, int dir) {
  XUngrabPointer(a->dpy, CurrentTime);
  XEvent e = {};
  e.xclient.type = ClientMessage;
  e.xclient.window = a->win;
  e.xclient.message_type = XInternAtom(a->dpy, "_NET_WM_MOVERESIZE", False);
  e.xclient.format = 32;
  e.xclient.data.l[0] = xr;
  e.xclient.data.l[1] = yr;
  e.xclient.data.l[2] = dir;  // 8 = move, 4 = resize bottom-right
  e.xclient.data.l[3] = 1;
  XSendEvent(a->dpy, a->root, False,
             SubstructureNotifyMask | SubstructureRedirectMask, &e);
}

// ===================== input handling ====================================
static size_t prev_char(const std::string &s, size_t i) {
  if (i == 0) return 0;
  i--;
  while (i > 0 && (s[i] & 0xc0) == 0x80) i--;
  return i;
}
static size_t next_char(const std::string &s, size_t i) {
  if (i >= s.size()) return s.size();
  i++;
  while (i < s.size() && (s[i] & 0xc0) == 0x80) i++;
  return i;
}
// Direct/passthrough: translate an X key event into libvterm keyboard input,
// which emits the correct escape sequences (honoring application cursor-key /
// keypad modes + modifiers) to the running program via the pty. Alacritty-grade.
static void passthrough_key(App *a, XKeyEvent *ev) {
  KeySym ks;
  char buf[64];
  XLookupString(ev, buf, sizeof buf, &ks, nullptr);
  VTermModifier mod = VTERM_MOD_NONE;
  if (ev->state & ShiftMask) mod = (VTermModifier)(mod | VTERM_MOD_SHIFT);
  if (ev->state & ControlMask) mod = (VTermModifier)(mod | VTERM_MOD_CTRL);
  if (ev->state & Mod1Mask) mod = (VTermModifier)(mod | VTERM_MOD_ALT);
  a->scroll = 0;
  a->dirty = true;

  struct { KeySym ks; VTermKey k; } sk[] = {
      {XK_Return, VTERM_KEY_ENTER},      {XK_KP_Enter, VTERM_KEY_ENTER},
      {XK_BackSpace, VTERM_KEY_BACKSPACE}, {XK_Tab, VTERM_KEY_TAB},
      {XK_ISO_Left_Tab, VTERM_KEY_TAB},  {XK_Escape, VTERM_KEY_ESCAPE},
      {XK_Up, VTERM_KEY_UP},             {XK_Down, VTERM_KEY_DOWN},
      {XK_Left, VTERM_KEY_LEFT},         {XK_Right, VTERM_KEY_RIGHT},
      {XK_Insert, VTERM_KEY_INS},        {XK_Delete, VTERM_KEY_DEL},
      {XK_Home, VTERM_KEY_HOME},         {XK_End, VTERM_KEY_END},
      {XK_Prior, VTERM_KEY_PAGEUP},      {XK_Next, VTERM_KEY_PAGEDOWN}};
  for (auto &m : sk)
    if (ks == m.ks) {
      vterm_keyboard_key(a->vt, m.k, mod);
      return;
    }
  if (ks >= XK_F1 && ks <= XK_F35) {
    vterm_keyboard_key(a->vt, (VTermKey)VTERM_KEY_FUNCTION(ks - XK_F1 + 1), mod);
    return;
  }
  // Printable: re-decode without Ctrl/Alt so libvterm applies them itself.
  XKeyEvent e2 = *ev;
  e2.state &= ~(unsigned)(ControlMask | Mod1Mask);
  char cb[64];
  KeySym k2;
  int cn = XLookupString(&e2, cb, sizeof cb, &k2, nullptr);
  VTermModifier cmod = VTERM_MOD_NONE;
  if (ev->state & ControlMask) cmod = (VTermModifier)(cmod | VTERM_MOD_CTRL);
  if (ev->state & Mod1Mask) cmod = (VTermModifier)(cmod | VTERM_MOD_ALT);
  for (int i = 0; i < cn;) {
    unsigned char c = cb[i];
    uint32_t cp;
    int len;
    if (c < 0x80) { cp = c; len = 1; }
    else if ((c >> 5) == 0x6) { cp = c & 0x1f; len = 2; }
    else if ((c >> 4) == 0xe) { cp = c & 0x0f; len = 3; }
    else if ((c >> 3) == 0x1e) { cp = c & 0x07; len = 4; }
    else { i++; continue; }
    for (int j = 1; j < len && i + j < cn; j++) cp = (cp << 6) | (cb[i + j] & 0x3f);
    vterm_keyboard_unichar(a->vt, cp, cmod);
    i += len;
  }
}

void on_key(App *a, XKeyEvent *ev) {
  char b[32];
  KeySym ks;
  int n = XLookupString(ev, b, sizeof(b), &ks, nullptr);

  bool ctrl = ev->state & ControlMask, shift = ev->state & ShiftMask;

  // Ctrl+` toggles between passthrough (keys -> running app) and the command box.
  if (ctrl && (ks == XK_grave || ks == XK_asciitilde)) {
    a->passthrough = !a->passthrough;
    a->dirty = true;
    return;
  }
  if (ctrl && shift) {  // Ctrl+Shift +/-/0 : scale the whole UI (chrome)
    if (ks == XK_plus || ks == XK_equal || ks == XK_KP_Add) {
      set_ui_scale(a, a->uiscale + 0.1);
      return;
    }
    if (ks == XK_underscore || ks == XK_minus || ks == XK_KP_Subtract) {
      set_ui_scale(a, a->uiscale - 0.1);
      return;
    }
    if (ks == XK_parenright || ks == XK_0 || ks == XK_KP_0) {
      set_ui_scale(a, 1.0);
      return;
    }
  } else if (ctrl) {  // Ctrl +/-/0 : scale the WHOLE app (font + chrome)
    if (ks == XK_plus || ks == XK_equal || ks == XK_KP_Add) {
      scale_app(a, +1);
      return;
    }
    if (ks == XK_minus || ks == XK_KP_Subtract) {
      scale_app(a, -1);
      return;
    }
    if (ks == XK_0 || ks == XK_KP_0) {
      scale_app(a, 0);
      return;
    }
  }

  // Keys go to the running application only when one actually owns the tty;
  // at the idle shell (no app expecting input) the command box takes over.
  if (key_to_app(a)) {
    passthrough_key(a, ev);
    return;
  }

  // Ctrl+R (box mode): manually refresh the command/alias completion list.
  if (ctrl && (ks == XK_r || ks == XK_R)) {
    request_alias_rescan(a);
    return;
  }

  if (a->popup) {
    if (ks == XK_Tab) {
      accept_completion(a);
      return;
    }
    if (ks == XK_Down) {
      a->sel = std::min(a->sel + 1, (int)a->matches.size() - 1);
      a->dirty = true;
      return;
    }
    if (ks == XK_Up) {
      a->sel = std::max(a->sel - 1, 0);
      a->dirty = true;
      return;
    }
    if (ks == XK_Escape) {
      a->popup = false;
      a->matches.clear();
      a->dirty = true;
      return;
    }
  } else {
    if (ks == XK_Up && !a->history.empty()) {
      a->hist = std::max(0, a->hist - 1);
      a->input = a->history[a->hist];
      a->caret = a->input.size();
      a->dirty = true;
      return;
    }
    if (ks == XK_Down && !a->history.empty()) {
      a->hist = std::min((int)a->history.size(), a->hist + 1);
      a->input = a->hist < (int)a->history.size() ? a->history[a->hist] : "";
      a->caret = a->input.size();
      a->dirty = true;
      return;
    }
  }

  switch (ks) {
    case XK_Return:
    case XK_KP_Enter:
      submit(a);
      return;
    case XK_BackSpace:
      if (a->caret > 0) {
        size_t p = prev_char(a->input, a->caret);
        a->input.erase(p, a->caret - p);
        a->caret = p;
        update_completion(a);
        a->dirty = true;
      }
      return;
    case XK_Delete:
      if (a->caret < a->input.size()) {
        size_t nx = next_char(a->input, a->caret);
        a->input.erase(a->caret, nx - a->caret);
        update_completion(a);
        a->dirty = true;
      }
      return;
    case XK_Left:
      a->caret = prev_char(a->input, a->caret);
      a->dirty = true;
      return;
    case XK_Right:
      a->caret = next_char(a->input, a->caret);
      a->dirty = true;
      return;
    case XK_Home:
      a->caret = 0;
      a->dirty = true;
      return;
    case XK_End:
      a->caret = a->input.size();
      a->dirty = true;
      return;
    case XK_Escape:
      return;
  }
  if (n > 0 && (unsigned char)b[0] >= 0x20) {  // printable
    a->input.insert(a->caret, b, n);
    a->caret += n;
    update_completion(a);
    a->dirty = true;
  }
}
void on_button(App *a, XButtonEvent *ev) {
  if (ev->button == 4 || ev->button == 5) {  // wheel scroll over output
    if (a->r_out.hit(ev->x, ev->y)) {
      int N = a->sb.size();
      a->scroll += (ev->button == 4 ? 3 : -3);
      a->scroll = std::max(0, std::min(a->scroll, N));
      a->dirty = true;
    }
    return;
  }
  if (ev->button != 1) return;
  // Widgets first so the resize zones never swallow a button click.
  if (a->r_close.hit(ev->x, ev->y)) {
    a->running = false;
    return;
  }
  if (a->r_min.hit(ev->x, ev->y)) {
    XIconifyWindow(a->dpy, a->win, a->scr);
    return;
  }
  if (a->r_submit.hit(ev->x, ev->y)) {
    submit(a);
    return;
  }
  if (a->popup) {  // click a completion row
    int rowh = a->uich + 2, iy = a->H - a->input_h;
    int ph = (int)a->matches.size() * rowh + 2, pyy = iy - ph - 2;
    if (ev->x >= a->S(22) && ev->x < a->S(22) + a->S(320) && ev->y >= pyy && ev->y < pyy + ph) {
      a->sel = (ev->y - pyy - 1) / rowh;
      accept_completion(a);
      return;
    }
  }
  if (a->r_grip.hit(ev->x, ev->y)) {  // corner grip → resize
    wm_moveresize(a, ev->x_root, ev->y_root, 4);
    return;
  }
  // Edge/corner resize — borderless window has no WM resize border, so a press
  // within EDGE px of any edge starts an interactive WM resize.
  // _NET_WM_MOVERESIZE dirs: 0=TL 1=T 2=TR 3=R 4=BR 5=B 6=BL 7=L.
  const int EDGE = 12;
  bool L = ev->x < EDGE, R = ev->x >= a->W - EDGE;
  bool T = ev->y < EDGE, D = ev->y >= a->H - EDGE;
  int dir = -1;
  if (T && L) dir = 0; else if (T && R) dir = 2;
  else if (D && L) dir = 6; else if (D && R) dir = 4;
  else if (L) dir = 7; else if (R) dir = 3;
  else if (T) dir = 1; else if (D) dir = 5;
  if (dir >= 0) {
    wm_moveresize(a, ev->x_root, ev->y_root, dir);
    return;
  }
  // Click the terminal to type into the app; click the input bar for the box.
  if (a->r_out.hit(ev->x, ev->y)) {
    a->passthrough = true;
    a->dirty = true;
    return;
  }
  if (ev->y >= a->H - a->input_h) {
    a->passthrough = false;
    a->dirty = true;
    return;
  }
  if (a->r_title.hit(ev->x, ev->y)) {
    wm_moveresize(a, ev->x_root, ev->y_root, 8);
    return;
  }
}
