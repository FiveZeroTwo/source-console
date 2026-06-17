#include "selection.h"

#include "completion.h"
#include "util.h"

#include <X11/Xatom.h>

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

void selection_init(App *a) {
  a->a_primary = XA_PRIMARY;
  a->a_clipboard = XInternAtom(a->dpy, "CLIPBOARD", False);
  a->a_utf8 = XInternAtom(a->dpy, "UTF8_STRING", False);
  a->a_targets = XInternAtom(a->dpy, "TARGETS", False);
  a->a_seldata = XInternAtom(a->dpy, "SRCTERM_SEL", False);
}

// ---- coordinate mapping + range helpers ----
static void px_to_cell(App *a, int x, int y, int &line, int &col) {
  const Rect &o = a->r_out;
  int N = (int)a->sb.size();
  int row = (y - (o.y + 1)) / a->ch;
  row = std::max(0, std::min(row, a->rows - 1));
  int c = (x - (o.x + 3)) / a->cw;
  c = std::max(0, std::min(c, a->cols - 1));
  line = N - a->scroll + row;
  col = c;
}
// Normalize the stored anchor/head into (l0,c0) <= (l1,c1).
static void sel_range(App *a, int &l0, int &c0, int &l1, int &c1) {
  l0 = a->sel_l0; c0 = a->sel_c0; l1 = a->sel_l1; c1 = a->sel_c1;
  if (l0 > l1 || (l0 == l1 && c0 > c1)) {
    std::swap(l0, l1);
    std::swap(c0, c1);
  }
}

bool cell_in_selection(App *a, int line, int col) {
  if (!a->have_sel) return false;
  int l0, c0, l1, c1;
  sel_range(a, l0, c0, l1, c1);
  if (line < l0 || line > l1) return false;
  if (line == l0 && col < c0) return false;
  if (line == l1 && col > c1) return false;
  return true;
}

// ---- text extraction ----
// Codepoints of one visual line (scrollback or live), padded to a->cols.
static std::vector<uint32_t> line_cps(App *a, int line) {
  int N = (int)a->sb.size();
  std::vector<uint32_t> v(a->cols, ' ');
  if (line < 0 || line >= N + a->rows) return v;
  if (line < N) {
    const std::vector<Cell> &ln = a->sb[line];
    for (int c = 0; c < a->cols && c < (int)ln.size(); c++)
      v[c] = ln[c].cp ? ln[c].cp : ' ';
  } else {
    for (int c = 0; c < a->cols; c++) {
      VTermPos pos = {line - N, c};
      VTermScreenCell cell;
      if (vterm_screen_get_cell(a->vts, pos, &cell) && cell.chars[0])
        v[c] = cell.chars[0];
    }
  }
  return v;
}
// Build the selected text: per line, the covered column span, trailing spaces
// trimmed, lines joined with '\n' (standard terminal copy behavior).
static std::string sel_extract(App *a) {
  int l0, c0, l1, c1;
  sel_range(a, l0, c0, l1, c1);
  std::string out;
  for (int line = l0; line <= l1; line++) {
    int from = (line == l0) ? c0 : 0;
    int to = (line == l1) ? c1 : a->cols - 1;  // inclusive
    std::vector<uint32_t> cps = line_cps(a, line);
    std::string row;
    for (int c = from; c <= to && c < a->cols; c++) {
      char u8[4];
      int n = enc_utf8(cps[c], u8);
      row.append(u8, n);
    }
    // trim trailing spaces on each line
    size_t end = row.find_last_not_of(' ');
    row = (end == std::string::npos) ? "" : row.substr(0, end + 1);
    if (line != l0) out += '\n';
    out += row;
  }
  return out;
}

// ---- drag lifecycle ----
void sel_begin(App *a, int x, int y) {
  px_to_cell(a, x, y, a->sel_l0, a->sel_c0);
  a->sel_l1 = a->sel_l0;
  a->sel_c1 = a->sel_c0;
  a->selecting = true;
  a->have_sel = false;  // hidden until the drag actually moves
  a->dirty = true;
}
void sel_update(App *a, int x, int y) {
  if (!a->selecting) return;
  px_to_cell(a, x, y, a->sel_l1, a->sel_c1);
  if (a->sel_l1 != a->sel_l0 || a->sel_c1 != a->sel_c0) a->have_sel = true;
  a->dirty = true;
}
// Extract the current range, and (if non-empty) own PRIMARY so middle-click
// paste works here and in other apps. Returns whether a selection now exists.
static bool sel_own(App *a) {
  a->seltext = sel_extract(a);
  a->have_sel = !a->seltext.empty();
  a->dirty = true;
  if (!a->have_sel) return false;
  XSetSelectionOwner(a->dpy, a->a_primary, a->win, CurrentTime);
  return true;
}
bool sel_end(App *a, int x, int y) {
  px_to_cell(a, x, y, a->sel_l1, a->sel_c1);
  a->selecting = false;
  if (a->sel_l1 == a->sel_l0 && a->sel_c1 == a->sel_c0) {  // a click, not a drag
    a->have_sel = false;
    a->dirty = true;
    return false;
  }
  return sel_own(a);
}
void sel_clear(App *a) {
  a->have_sel = false;
  a->dirty = true;
}

// ---- word / line / extend selection ----
static bool is_wordchar(uint32_t cp) {
  if (cp == ' ' || cp == 0) return false;
  if (cp < 128 && strchr("\t()[]{}<>'\"`|&;,", (int)cp)) return false;
  return true;  // letters, digits, and path/URL-ish punctuation count as a word
}
void sel_word(App *a, int x, int y) {
  int line, col;
  px_to_cell(a, x, y, line, col);
  std::vector<uint32_t> cps = line_cps(a, line);
  int s = col, e = col;
  if (is_wordchar(cps[col])) {
    while (s > 0 && is_wordchar(cps[s - 1])) s--;
    while (e < a->cols - 1 && is_wordchar(cps[e + 1])) e++;
  }
  a->sel_l0 = a->sel_l1 = line;
  a->sel_c0 = s;
  a->sel_c1 = e;
  a->selecting = false;
  sel_own(a);
}
void sel_line(App *a, int x, int y) {
  int line, col;
  px_to_cell(a, x, y, line, col);
  std::vector<uint32_t> cps = line_cps(a, line);
  int e = a->cols - 1;
  while (e > 0 && (cps[e] == ' ' || cps[e] == 0)) e--;  // to last non-blank
  a->sel_l0 = a->sel_l1 = line;
  a->sel_c0 = 0;
  a->sel_c1 = e;
  a->selecting = false;
  sel_own(a);
}
void sel_extend(App *a, int x, int y) {
  px_to_cell(a, x, y, a->sel_l1, a->sel_c1);  // keep the anchor, move the head
  a->selecting = false;
  sel_own(a);
}
void selection_autoscroll_tick(App *a) {
  if (!a->selecting) return;
  const Rect &o = a->r_out;
  int N = (int)a->sb.size();
  int before = a->scroll;
  if (a->drag_y < o.y + 1) a->scroll = std::min(a->scroll + 1, N);        // above → older
  else if (a->drag_y > o.y + o.h - 1) a->scroll = std::max(a->scroll - 1, 0);  // below → newer
  if (a->scroll != before) {
    sel_update(a, a->drag_x, a->drag_y);  // re-extend the head into the new row
    a->dirty = true;
  }
}

// ---- clickable URLs ----
static bool is_urlchar(uint32_t cp) {
  if (cp <= ' ' || cp >= 127) return false;
  return !strchr("()<>\"'`{}|\\^", (int)cp);
}
bool open_url_at(App *a, int x, int y) {
  int line, col;
  px_to_cell(a, x, y, line, col);
  std::vector<uint32_t> cps = line_cps(a, line);
  if (!is_urlchar(cps[col])) return false;
  int s = col, e = col;
  while (s > 0 && is_urlchar(cps[s - 1])) s--;
  while (e < a->cols - 1 && is_urlchar(cps[e + 1])) e++;
  std::string tok;
  for (int c = s; c <= e; c++) {
    char u8[4];
    tok.append(u8, enc_utf8(cps[c], u8));
  }
  std::string url;
  if (tok.compare(0, 7, "http://") == 0 || tok.compare(0, 8, "https://") == 0)
    url = tok;
  else if (tok.compare(0, 4, "www.") == 0)
    url = "https://" + tok;
  else
    return false;
  while (!url.empty() && strchr(".,;:!?", url.back())) url.pop_back();  // trailing punct
  pid_t pid = fork();
  if (pid == 0) {
    execlp("xdg-open", "xdg-open", url.c_str(), (char *)nullptr);
    _exit(127);
  }
  return pid > 0;
}

// ---- clipboard ops ----
void copy_clipboard(App *a) {
  if (a->seltext.empty()) return;
  a->clip = a->seltext;
  XSetSelectionOwner(a->dpy, a->a_clipboard, a->win, CurrentTime);
}
void paste_request(App *a, bool clipboard) {
  a->paste_to_pty = a->passthrough;  // where the bytes go when they arrive
  Atom sel = clipboard ? a->a_clipboard : a->a_primary;
  XConvertSelection(a->dpy, sel, a->a_utf8, a->a_seldata, a->win, CurrentTime);
}

// ---- serving our selection to other clients ----
void on_selection_request(App *a, XSelectionRequestEvent *e) {
  const std::string &text = (e->selection == a->a_clipboard) ? a->clip : a->seltext;
  XSelectionEvent ev = {};
  ev.type = SelectionNotify;
  ev.display = e->display;
  ev.requestor = e->requestor;
  ev.selection = e->selection;
  ev.target = e->target;
  ev.property = e->property;
  ev.time = e->time;
  if (e->target == a->a_targets) {  // advertise what we can hand over
    Atom targets[] = {a->a_targets, a->a_utf8, XA_STRING};
    XChangeProperty(a->dpy, e->requestor, e->property, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)targets, 3);
  } else if (e->target == a->a_utf8 || e->target == XA_STRING) {
    XChangeProperty(a->dpy, e->requestor, e->property, e->target, 8,
                    PropModeReplace, (const unsigned char *)text.c_str(),
                    text.size());
  } else {
    ev.property = None;  // unsupported target
  }
  XSendEvent(a->dpy, e->requestor, False, 0, (XEvent *)&ev);
}

// ---- receiving a paste ----
void on_selection_notify(App *a, XSelectionEvent *e) {
  if (e->property == None) return;  // no owner / unsupported
  Atom type;
  int fmt;
  unsigned long nitems, after;
  unsigned char *data = nullptr;
  if (XGetWindowProperty(a->dpy, a->win, e->property, 0, 1 << 20, True,
                         AnyPropertyType, &type, &fmt, &nitems, &after,
                         &data) != Success)
    return;
  if (!data) return;
  std::string text((const char *)data, nitems);
  XFree(data);
  if (text.empty()) return;
  if (a->paste_to_pty) {
    // Bracketed paste (if the app enabled it) lets it tell typed input from
    // pasted text, so a multi-line paste doesn't auto-run line by line.
    if (a->bracket_paste) {
      std::string w = "\x1b[200~" + text + "\x1b[201~";
      (void)!write(a->master, w.c_str(), w.size());
    } else {
      (void)!write(a->master, text.c_str(), text.size());
    }
  } else {
    // Into the command box: a single line, so flatten newlines and drop a
    // trailing one (don't auto-submit on paste).
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
      text.pop_back();
    for (char &c : text)
      if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    a->input.insert(a->caret, text);
    a->caret += text.size();
    update_completion(a);
  }
  a->dirty = true;
}
// ---- OSC 52: a program sets the system clipboard ----
// libvterm parses OSC 52 and base64-decodes it for us, delivering the decoded
// text (possibly in fragments) to this `set` callback. We then own the matching
// X selection and serve the text like any other selection.
static int sel_set_cb(VTermSelectionMask mask, VTermStringFragment frag,
                      void *user) {
  App *a = (App *)user;
  if (frag.initial) a->osc_buf.clear();
  a->osc_buf.append(frag.str, frag.len);
  if (!frag.final) return 1;
  std::string text = a->osc_buf;
  a->osc_buf.clear();
  if (text.empty()) return 1;
  if (mask & VTERM_SELECTION_PRIMARY) {
    a->seltext = text;
    XSetSelectionOwner(a->dpy, a->a_primary, a->win, CurrentTime);
  }
  // CLIPBOARD (or anything that isn't strictly PRIMARY) → the clipboard.
  if (mask != VTERM_SELECTION_PRIMARY) {
    a->clip = text;
    XSetSelectionOwner(a->dpy, a->a_clipboard, a->win, CurrentTime);
  }
  return 1;
}
// Refuse clipboard reads (don't let programs exfiltrate the clipboard).
static int sel_query_cb(VTermSelectionMask, void *) { return 0; }
static const VTermSelectionCallbacks SEL_CB = {sel_set_cb, sel_query_cb};
void selection_register_osc(App *a) {
  static char buf[16384];  // scratch libvterm uses to assemble the payload
  vterm_state_set_selection_callbacks(a->vst, &SEL_CB, a, buf, sizeof buf);
}

void on_selection_clear(App *a, XSelectionClearEvent *e) {
  if (e->selection == a->a_clipboard) {
    a->clip.clear();
  } else {  // lost PRIMARY → another app owns the selection; drop our highlight
    a->have_sel = false;
    a->dirty = true;
  }
}
