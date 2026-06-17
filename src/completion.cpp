#include "completion.h"

#include "util.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>

// commands = the PATH executables ∪ the shell aliases, sorted & deduped.
void rebuild_commands(App *a) {
  std::set<std::string> all = a->execset;
  for (const auto &al : a->aliases) all.insert(al);
  a->commands.assign(all.begin(), all.end());
}
// Aliases live in the interactive shell, not on PATH. At startup we ask a fresh
// login shell (same ZDOTDIR srcterm launches it with) for its alias names; live
// rescans (below) instead query the *running* shell so session-defined aliases
// are picked up too.
static std::vector<std::string> query_aliases_fresh(bool zsh) {
  const char *sh = getenv("SHELL");
  std::string shell = (sh && *sh) ? sh : "/usr/bin/zsh";
  std::string cmd;
  if (zsh)
    cmd = "ZDOTDIR='" + exe_dir() + "/runtime' '" + shell +
          "' -ic 'print -rl -- ${(k)aliases}' 2>/dev/null";
  else
    cmd = "'" + shell + "' -ic 'compgen -a' 2>/dev/null";  // bash & friends
  std::vector<std::string> out;
  FILE *f = popen(cmd.c_str(), "r");
  if (!f) return out;
  char line[512];
  while (fgets(line, sizeof line, f)) {
    std::string s = strip(line);
    if (!s.empty()) out.push_back(s);
  }
  pclose(f);
  return out;
}
void scan_commands(App *a) {
  a->execset.clear();
  const char *path = getenv("PATH");
  std::string p(path ? path : "/usr/bin:/bin");
  size_t i = 0;
  while (i <= p.size()) {
    size_t j = p.find(':', i);
    std::string dir = p.substr(i, j == std::string::npos ? -1 : j - i);
    i = (j == std::string::npos) ? p.size() + 1 : j + 1;
    if (dir.empty()) continue;
    DIR *d = opendir(dir.c_str());
    if (!d) continue;
    for (struct dirent *e; (e = readdir(d));) {
      if (e->d_name[0] == '.') continue;
      std::string f = dir + "/" + e->d_name;
      if (access(f.c_str(), X_OK) == 0) a->execset.insert(e->d_name);
    }
    closedir(d);
  }
  a->aliases = query_aliases_fresh(a->is_zsh);
  rebuild_commands(a);
}

// ---- live alias rescan (the "hook") -------------------------------------
// Ask the running shell to dump its current alias names to a temp file, capped
// with a sentinel line so we never read a half-written file. Triggered manually
// (Ctrl+R) or after alias-changing commands; see submit() and the main loop.
void request_alias_rescan(App *a) {
  if (a->master < 0) return;
  if (!a->is_zsh) {  // no live-dump syntax; just re-query a fresh shell
    scan_commands(a);
    a->dirty = true;
    return;
  }
  a->alias_want = true;
}
void send_alias_dump(App *a) {
  unlink(a->alias_tmp.c_str());
  std::string c = "{ print -rl -- ${(k)aliases}; print -r -- __SRCTERM_END__; } >| '" +
                  a->alias_tmp + "' 2>/dev/null\r";
  (void)!write(a->master, c.c_str(), c.size());
  a->alias_pending = true;
}
void try_load_alias_dump(App *a) {
  std::ifstream f(a->alias_tmp);
  if (!f) return;  // shell hasn't written it yet
  std::vector<std::string> names;
  std::string ln;
  bool done = false;
  while (std::getline(f, ln)) {
    std::string s = strip(ln);
    if (s == "__SRCTERM_END__") { done = true; break; }
    if (!s.empty()) names.push_back(s);
  }
  if (!done) return;  // sentinel missing → still being written; try next tick
  a->aliases = std::move(names);
  rebuild_commands(a);
  unlink(a->alias_tmp.c_str());
  a->alias_pending = false;
  a->dirty = true;
}
// Complete the partial path `tok` against the filesystem, resolving relative
// paths against the shell's cwd. `dirs_only` restricts to directories (for cd).
// Each match is the full token to insert; directory matches get a trailing '/'.
static void complete_path(App *a, const std::string &tok, bool dirs_only) {
  // Split the token into a directory part (kept verbatim) and a name prefix.
  size_t slash = tok.find_last_of('/');
  std::string dirpart = slash == std::string::npos ? "" : tok.substr(0, slash + 1);
  std::string prefix = slash == std::string::npos ? tok : tok.substr(slash + 1);
  // Resolve the directory we actually scan on disk.
  std::string scan;
  const char *home = getenv("HOME");
  if (dirpart.empty())
    scan = ".";  // opendir(".") uses *our* cwd; fix below via the shell's cwd
  else if (dirpart[0] == '/')
    scan = dirpart;
  else if (dirpart[0] == '~' && home)
    scan = std::string(home) + dirpart.substr(1);
  else
    scan = dirpart;
  // Relative scans must be anchored to the shell's cwd, not srcterm's process.
  if (scan == "." || (scan[0] != '/' && scan[0] != '~')) {
    std::string base = a->cwd;
    if (!base.empty() && base[0] == '~' && home) base = std::string(home) + base.substr(1);
    if (base.empty()) base = ".";
    scan = scan == "." ? base : base + "/" + scan;
  }
  DIR *d = opendir(scan.c_str());
  if (!d) return;
  std::string lpfx = lower(prefix);
  std::vector<std::pair<bool, std::string>> hits;  // (is_dir, token)
  for (struct dirent *e; (e = readdir(d));) {
    std::string name = e->d_name;
    if (name == "." || name == "..") continue;
    if (prefix.empty() && name[0] == '.') continue;  // hide dotfiles unless asked
    if (lower(name).compare(0, lpfx.size(), lpfx) != 0) continue;
    struct stat st;
    bool isdir = stat((scan + "/" + name).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    if (dirs_only && !isdir) continue;
    hits.push_back({isdir, dirpart + name + (isdir ? "/" : "")});
  }
  closedir(d);
  std::sort(hits.begin(), hits.end(), [](const auto &x, const auto &y) {
    if (x.first != y.first) return x.first > y.first;  // directories first
    return lower(x.second) < lower(y.second);
  });
  for (auto &h : hits) {
    a->matches.push_back(h.second);
    size_t s = h.second.find_last_of('/', h.second.size() - 2);
    a->matchdisp.push_back(s == std::string::npos ? h.second
                                                   : h.second.substr(s + 1));
    if ((int)a->matches.size() >= MAXCOMP) break;
  }
}
void update_completion(App *a) {
  a->matches.clear();
  a->matchdisp.clear();
  a->popup = false;
  a->sel = -1;
  a->match_is_path = false;
  if (a->input.empty()) return;
  size_t sp = a->input.find_last_of(' ');
  if (sp == std::string::npos) {  // first word → complete command names
    std::string pfx = lower(a->input);
    for (const auto &c : a->commands)
      if (lower(c).compare(0, pfx.size(), pfx) == 0) {
        a->matches.push_back(c);
        a->matchdisp.push_back(c);
        if ((int)a->matches.size() >= MAXCOMP) break;
      }
  } else {  // an argument → complete filesystem paths
    a->match_is_path = true;
    std::string cmd0 = a->input.substr(0, a->input.find(' '));
    bool dirs_only = cmd0 == "cd" || cmd0 == "pushd" || cmd0 == "rmdir";
    complete_path(a, a->input.substr(sp + 1), dirs_only);
  }
  if (!a->matches.empty()) {
    a->popup = true;
    a->sel = 0;
  }
}
void accept_completion(App *a) {
  if (a->sel < 0 || a->sel >= (int)a->matches.size()) return;
  // Replace just the current token (after the last space) with the match.
  size_t sp = a->input.find_last_of(' ');
  std::string head = sp == std::string::npos ? "" : a->input.substr(0, sp + 1);
  std::string match = a->matches[a->sel];
  // Directories end in '/': keep typing into them; otherwise finish the token.
  bool dir = !match.empty() && match.back() == '/';
  a->input = head + match + (dir ? "" : " ");
  a->caret = a->input.size();
  a->popup = false;
  a->matches.clear();
  a->matchdisp.clear();
  // After completing a directory, immediately offer its contents.
  if (dir) update_completion(a);
  a->dirty = true;
}
