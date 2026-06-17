#include "search.h"

#include "util.h"

#include <algorithm>

// ASCII-ish text of a visual line (scrollback or live), lowercased for matching.
static std::string line_lower(App *a, int line) {
  int N = (int)a->sb.size();
  std::string s;
  auto push = [&](uint32_t cp) { s += (cp >= 32 && cp < 127) ? (char)cp : ' '; };
  if (line < 0 || line >= N + a->rows) return s;
  if (line < N) {
    for (const Cell &cell : a->sb[line]) push(cell.cp);
  } else {
    for (int c = 0; c < a->cols; c++) {
      VTermPos pos = {line - N, c};
      VTermScreenCell cell;
      if (vterm_screen_get_cell(a->vts, pos, &cell)) push(cell.chars[0]);
    }
  }
  return lower(s);
}

// Scroll so visual line `line` sits about a third of the way down the pane.
static void jump_to(App *a, int line) {
  int N = (int)a->sb.size();
  int top = std::max(0, line - a->rows / 3);
  a->scroll = std::max(0, std::min(N - top, N));
  a->dirty = true;
}

// Rebuild the hit list for the current query and jump to the newest match.
static void recompute(App *a) {
  a->search_hits.clear();
  a->search_idx = -1;
  if (a->search.empty()) {
    a->dirty = true;
    return;
  }
  std::string q = lower(a->search);
  int N = (int)a->sb.size();
  for (int line = 0; line < N + a->rows; line++)
    if (line_lower(a, line).find(q) != std::string::npos)
      a->search_hits.push_back(line);
  if (!a->search_hits.empty()) {
    a->search_idx = (int)a->search_hits.size() - 1;  // start at the newest match
    jump_to(a, a->search_hits[a->search_idx]);
  }
  a->dirty = true;
}

// Move to another match. dir<0 = older (up the buffer), dir>0 = newer.
static void step(App *a, int dir) {
  if (a->search_hits.empty()) return;
  a->search_idx = std::max(
      0, std::min((int)a->search_hits.size() - 1, a->search_idx + dir));
  jump_to(a, a->search_hits[a->search_idx]);
}

void search_open(App *a) {
  a->searching = true;
  a->search.clear();
  a->search_hits.clear();
  a->search_idx = -1;
  a->popup = false;  // hide any completion popup
  a->dirty = true;
}
void search_close(App *a) {
  a->searching = false;
  a->dirty = true;
}
void search_key(App *a, KeySym ks, const char *text, int len) {
  switch (ks) {
    case XK_Escape:
      search_close(a);
      return;
    case XK_Return:
    case XK_KP_Enter:
    case XK_Up:
      step(a, -1);  // older match
      return;
    case XK_Down:
      step(a, +1);  // newer match
      return;
    case XK_BackSpace:
      if (!a->search.empty()) {
        a->search.pop_back();
        recompute(a);
      }
      return;
  }
  if (len > 0 && (unsigned char)text[0] >= 0x20) {  // printable → extend query
    a->search.append(text, len);
    recompute(a);
  }
}
