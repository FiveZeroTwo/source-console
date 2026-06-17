// srcterm — a terminal emulator skinned like the Source Engine developer
// console. Suckless-style: raw Xlib + Xft for the window/text and libvterm for
// the terminal emulation. No GTK, no VTE.
//
//   window/input/render : Xlib + Xft (FreeType)        (render.cpp, input.cpp)
//   terminal engine     : libvterm (feed pty bytes -> screen grid)  (terminal.cpp)
//   shell               : zsh via forkpty(), pure-output mode (ZDOTDIR)
//   command box         : $PATH + alias autocomplete   (completion.cpp)
//
// This file is just startup + the event loop; see the per-module headers.
#include "app.h"
#include "completion.h"
#include "input.h"
#include "render.h"
#include "selection.h"
#include "terminal.h"
#include "theme.h"
#include "util.h"

#include <X11/Xatom.h>

#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main() {
  signal(SIGCHLD, SIG_IGN);
  App a;
  load_theme(a.th);
  {  // shell flavor + per-instance temp file for live alias dumps
    const char *sh = getenv("SHELL");
    std::string s = (sh && *sh) ? sh : "/usr/bin/zsh";
    a.is_zsh = s.size() >= 3 && s.compare(s.size() - 3, 3, "zsh") == 0;
    a.alias_tmp = "/tmp/.srcterm-aliases-" + std::to_string((long)getpid());
  }
  scan_commands(&a);

  a.dpy = XOpenDisplay(nullptr);
  if (!a.dpy) {
    fprintf(stderr, "srcterm: cannot open X display\n");
    return 1;
  }
  a.scr = DefaultScreen(a.dpy);
  a.root = RootWindow(a.dpy, a.scr);
  a.vis = DefaultVisual(a.dpy, a.scr);
  a.cmap = DefaultColormap(a.dpy, a.scr);
  selection_init(&a);  // intern clipboard/selection atoms

  // font: split "Family Name 11" into family + point size, then load.
  a.fontfam = a.th.font;
  a.fontsize = 11;
  size_t sp = a.fontfam.find_last_of(' ');
  if (sp != std::string::npos &&
      a.fontfam.find_first_not_of("0123456789", sp + 1) == std::string::npos) {
    a.fontsize = atoi(a.fontfam.c_str() + sp + 1);
    a.fontfam = a.fontfam.substr(0, sp);
  }
  // An explicit `font_size` in the config wins over the size in the `font` line.
  if (a.th.font_size > 0) a.fontsize = a.th.font_size;
  a.fontsize = a.fontsize < 5 ? 5 : (a.fontsize > 60 ? 60 : a.fontsize);
  a.fontsize_base = a.fontsize;
  load_fonts(&a);
  if (!a.font) {
    fprintf(stderr, "srcterm: cannot load font '%s'\n", a.fontfam.c_str());
    return 1;
  }
  // Persistent chrome/UI scale from the config (clamped like Ctrl+Shift +/-).
  a.uiscale = a.th.ui_scale < 0.6 ? 0.6 : (a.th.ui_scale > 3.0 ? 3.0 : a.th.ui_scale);
  // Chrome font + scaled layout dimensions for the configured UI scale.
  apply_uiscale(&a);

  a.win = XCreateSimpleWindow(a.dpy, a.root, 0, 0, a.W, a.H, 0, 0,
                              colh(&a, a.th.face)->pixel);
  XSelectInput(a.dpy, a.win,
               ExposureMask | KeyPressMask | ButtonPressMask |
                   ButtonReleaseMask | Button1MotionMask | StructureNotifyMask);
  // borderless (Motif hint), but still WM-managed (movable/resizable)
  struct {
    unsigned long flags, functions, decorations;
    long input_mode;
    unsigned long status;
  } mwm = {2, 0, 0, 0, 0};
  Atom mwmA = XInternAtom(a.dpy, "_MOTIF_WM_HINTS", False);
  XChangeProperty(a.dpy, a.win, mwmA, mwmA, 32, PropModeReplace,
                  (unsigned char *)&mwm, 5);
  XStoreName(a.dpy, a.win, "Console");
  Atom wmdel = XInternAtom(a.dpy, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(a.dpy, a.win, &wmdel, 1);
  // Mark resizable with a sane minimum so the WM always allows growing it.
  XSizeHints *sh = XAllocSizeHints();
  sh->flags = PMinSize;
  sh->min_width = 320;
  sh->min_height = 200;
  XSetWMNormalHints(a.dpy, a.win, sh);
  XFree(sh);

  a.gc = XCreateGC(a.dpy, a.win, 0, nullptr);
  a.buf = XCreatePixmap(a.dpy, a.win, a.W, a.H, DefaultDepth(a.dpy, a.scr));
  a.xd = XftDrawCreate(a.dpy, a.buf, a.vis, a.cmap);

  compute_grid(&a);

  // vterm engine
  a.vt = vterm_new(a.rows, a.cols);
  vterm_set_utf8(a.vt, 1);
  a.vts = vterm_obtain_screen(a.vt);
  vterm_screen_set_callbacks(a.vts, &SCB, &a);
  vterm_screen_reset(a.vts, 1);
  VTermState *st = vterm_obtain_state(a.vt);
  a.vst = st;
  int r, g, b;
  VTermColor fg, bgc;
  parse_hex(a.th.fg, r, g, b);
  vterm_color_rgb(&fg, r, g, b);
  parse_hex(a.th.bg, r, g, b);
  vterm_color_rgb(&bgc, r, g, b);
  vterm_state_set_default_colors(st, &fg, &bgc);
  for (int i = 0; i < 16; i++) {
    VTermColor c;
    parse_hex(a.th.palette[i], r, g, b);
    vterm_color_rgb(&c, r, g, b);
    vterm_state_set_palette_color(st, i, &c);
  }
  vterm_output_set_callback(a.vt, out_cb, &a);  // terminal -> child responses

  spawn(&a);
  apply_size(&a);
  a.cwd = read_cwd(&a);
  feed(&a, "\x1b[38;2;150;150;150mSource Console  ::  developer terminal\x1b[0m\r\n\r\n");

  XMapWindow(a.dpy, a.win);

  bool selftest = getenv("SRCTERM_SELFTEST");
  const char *shoot = getenv("SRCTERM_SHOOT");
  struct timespec t0;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  bool seeded = false, seeded2 = false;
  bool was_app = false;

  int xfd = ConnectionNumber(a.dpy);
  while (a.running) {
    // A foreground app can grab/release the tty without emitting output (e.g.
    // it just blocks on input); refresh the chrome when that ownership flips.
    bool now_app = app_owns_tty(&a);
    if (now_app != was_app) {
      was_app = now_app;
      a.dirty = true;
    }
    // Track the shell's cwd (changes after `cd`) for the titlebar + prompt echo.
    std::string cwd_now = read_cwd(&a);
    if (cwd_now != a.cwd) {
      a.cwd = cwd_now;
      a.dirty = true;
    }
    // Live alias rescan: send the dump only while the shell is idle (so we don't
    // type into a foreground app), then pick up the file once it's complete.
    if (a.alias_want && !a.alias_pending && !now_app) {
      send_alias_dump(&a);
      a.alias_want = false;
    }
    if (a.alias_pending) try_load_alias_dump(&a);
    selection_autoscroll_tick(&a);  // continuous edge-scroll during a drag
    if (a.dirty) {
      render(&a);
      a.dirty = false;
    }
    XFlush(a.dpy);

    if (!XPending(a.dpy)) {
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(xfd, &fds);
      FD_SET(a.master, &fds);
      struct timeval tv = {0, 100000};
      select(std::max(xfd, a.master) + 1, &fds, nullptr, nullptr, &tv);
    }

    while (XPending(a.dpy)) {
      XEvent e;
      XNextEvent(a.dpy, &e);
      switch (e.type) {
        case Expose:
          XCopyArea(a.dpy, a.buf, a.win, a.gc, 0, 0, a.W, a.H, 0, 0);
          break;
        case KeyPress:
          on_key(&a, &e.xkey);
          break;
        case ButtonPress:
          on_button(&a, &e.xbutton);
          break;
        case ButtonRelease:
          on_release(&a, &e.xbutton);
          break;
        case MotionNotify:
          on_motion(&a, &e.xmotion);
          break;
        case SelectionRequest:
          on_selection_request(&a, &e.xselectionrequest);
          break;
        case SelectionNotify:
          on_selection_notify(&a, &e.xselection);
          break;
        case SelectionClear:
          on_selection_clear(&a, &e.xselectionclear);
          break;
        case ConfigureNotify:
          if (e.xconfigure.width != a.W || e.xconfigure.height != a.H) {
            a.W = e.xconfigure.width;
            a.H = e.xconfigure.height;
            XFreePixmap(a.dpy, a.buf);
            a.buf = XCreatePixmap(a.dpy, a.win, a.W, a.H,
                                  DefaultDepth(a.dpy, a.scr));
            XftDrawChange(a.xd, a.buf);
            compute_grid(&a);
            apply_size(&a);
            a.dirty = true;
          }
          break;
        case ClientMessage:
          if ((Atom)e.xclient.data.l[0] == wmdel) a.running = false;
          break;
      }
    }

    // drain pty. read()==0 is EOF; on Linux a pty master returns -1/EIO when
    // the child (zsh) exits — both mean "shell died, so we die".
    char rb[8192];
    for (;;) {
      ssize_t n = read(a.master, rb, sizeof(rb));
      if (n > 0) {
        feed(&a, rb, n);
        a.dirty = true;
      } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        break;  // no more data for now
      } else {
        a.running = false;  // EOF (0) or EIO/other error → shell gone
        break;
      }
    }

    if (selftest || shoot) {
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      double dt = (now.tv_sec - t0.tv_sec) + (now.tv_nsec - t0.tv_nsec) / 1e9;
      if (!seeded && dt > 0.8) {
        seeded = true;
        // Clipboard round-trip: select the banner row (owns PRIMARY), then paste
        // PRIMARY into the box — exercises serve + receive end to end.
        if (const char *ct = getenv("SRCTERM_CLIPTEST")) {
          Rect o = a.r_out;
          int y = o.y + 1 + a.ch / 2;
          std::string mode = ct;
          if (mode == "word") {  // double-click on "Console" (banner cols 7..13)
            sel_word(&a, o.x + 3 + 9 * a.cw, y);
          } else if (mode == "line") {  // triple-click selects the whole line
            sel_line(&a, o.x + 3 + 2 * a.cw, y);
          } else {  // default: drag-select "Source Console" + paste round-trip
            int ex = o.x + 3 + 13 * a.cw + a.cw / 2;  // through col 13
            sel_begin(&a, o.x + 3 + a.cw / 2, y);
            sel_update(&a, ex, y);
            sel_end(&a, ex, y);
            a.passthrough = false;     // box mode → paste lands in the field
            paste_request(&a, false);  // PRIMARY (we own it) → round-trip → a.input
          }
          a.dirty = true;
          continue;
        }
        // Exercise the passthrough path: type each char + Enter via libvterm.
        if (const char *pt = getenv("SRCTERM_PTTEST")) {
          for (const char *p = pt; *p; p++)
            vterm_keyboard_unichar(a.vt, (unsigned char)*p, VTERM_MOD_NONE);
          vterm_keyboard_key(a.vt, VTERM_KEY_ENTER, VTERM_MOD_NONE);
          a.dirty = true;
          continue;
        }
        const char *sub = getenv("SRCTERM_SUBMIT");
        a.input = sub ? sub : "echo SELFTEST_OK";
        a.caret = a.input.size();
        update_completion(&a);
        if (!getenv("SRCTERM_KEEPINPUT")) submit(&a);
        if (getenv("SRCTERM_ZOOM")) set_font_size(&a, a.fontsize + atoi(getenv("SRCTERM_ZOOM")));
        if (getenv("SRCTERM_UISCALE")) set_ui_scale(&a, atof(getenv("SRCTERM_UISCALE")));
        if (getenv("SRCTERM_APPSCALE")) scale_app(&a, atoi(getenv("SRCTERM_APPSCALE")));
        a.dirty = true;
      }
      // second submit (e.g. empty Enter) to test "continue operations"
      if (seeded && !seeded2 && dt > 1.2 && getenv("SRCTERM_SUBMIT2")) {
        seeded2 = true;
        a.input = getenv("SRCTERM_SUBMIT2");
        a.caret = a.input.size();
        if (getenv("SRCTERM_KEEPINPUT2"))
          update_completion(&a);  // show completion instead of running it
        else
          submit(&a);
      }
      if (dt > 2.0) {
        render(&a);
        if (shoot) screenshot(&a, shoot);
        if (getenv("SRCTERM_CLIPTEST"))
          fprintf(stderr, "CLIPTEST seltext=[%s] pasted=[%s]\n",
                  a.seltext.c_str(), a.input.c_str());
        fprintf(stderr, "SELFTEST OK: %dx%d grid %dx%d, %zu commands\n", a.W,
                a.H, a.cols, a.rows, a.commands.size());
        a.running = false;
      }
    }
  }

  if (a.child > 0) kill(a.child, SIGHUP);  // don't leave the shell orphaned
  if (!a.alias_tmp.empty()) unlink(a.alias_tmp.c_str());
  return 0;
}
