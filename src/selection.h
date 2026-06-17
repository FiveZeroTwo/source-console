// X11 clipboard + mouse text selection. The user drags over the output pane to
// select; we then own PRIMARY (and CLIPBOARD on Ctrl+Shift+C) and serve the text
// to other clients. Middle-click pastes PRIMARY, Ctrl+Shift+V pastes CLIPBOARD —
// into the command box, or to the running program when it owns the tty.
#pragma once
#include "app.h"

void selection_init(App *a);  // intern the selection atoms (needs an open display)

// Mouse drag lifecycle (pixel coords within the window).
void sel_begin(App *a, int x, int y);
void sel_update(App *a, int x, int y);
bool sel_end(App *a, int x, int y);  // finalize; true if a non-empty selection
void sel_clear(App *a);              // drop the highlight (keeps owned text)
void sel_word(App *a, int x, int y);    // double-click: select the word
void sel_line(App *a, int x, int y);    // triple-click: select the whole line
void sel_extend(App *a, int x, int y);  // Shift-click: move the head, keep anchor
// While a drag is in progress, scroll the buffer if the pointer is past the top
// or bottom edge of the output pane; called once per main-loop tick.
void selection_autoscroll_tick(App *a);

// Ctrl+click: if (x,y) is on a URL token, xdg-open it. Returns true if opened.
bool open_url_at(App *a, int x, int y);

// Is the cell at (line, col) inside the current selection? (for rendering)
bool cell_in_selection(App *a, int line, int col);

// Clipboard ops.
void copy_clipboard(App *a);              // own CLIPBOARD with the current selection
void paste_request(App *a, bool clipboard);  // ask X for PRIMARY/CLIPBOARD text

// X event handlers for selection ownership + transfer.
void on_selection_request(App *a, XSelectionRequestEvent *e);
void on_selection_notify(App *a, XSelectionEvent *e);
void on_selection_clear(App *a, XSelectionClearEvent *e);
