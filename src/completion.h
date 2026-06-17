// The command box's autocomplete: a list of completable tokens (PATH executables
// ∪ shell aliases) plus filesystem-path completion for arguments. The alias list
// is refreshed live by asking the running shell to dump its aliases to a file.
#pragma once
#include "app.h"

void rebuild_commands(App *a);  // commands = execset ∪ aliases, sorted & deduped
void scan_commands(App *a);     // scan $PATH + a fresh shell's aliases (startup)

// Live alias rescan ("the hook"): request -> dump -> load, driven by the loop.
void request_alias_rescan(App *a);
void send_alias_dump(App *a);
void try_load_alias_dump(App *a);

// Popup state for the current input.
void update_completion(App *a);  // recompute matches for a->input
void accept_completion(App *a);  // insert the selected match into a->input
