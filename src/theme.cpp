#include "theme.h"

#include "util.h"

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

// The active rice theme's colors live in Alacritty's active.toml (rice copies
// the chosen theme there). Path is fixed by the user's rice setup.
static std::string rice_toml() {
  const char *home = getenv("HOME");
  return home ? std::string(home) + "/.config/alacritty/themes/active.toml" : "";
}
static std::string srcterm_conf() {
  const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
  if (xdg && *xdg) return std::string(xdg) + "/srcterm/colors.conf";
  if (home) return std::string(home) + "/.config/srcterm/colors.conf";
  return "";
}

// Overlay the terminal palette (fg/bg/cursor/highlight + the 16 ANSI colors)
// from the active rice theme. Chrome colors are left to colors.conf. Minimal
// TOML scan: track the current [section], read `key = "#rrggbb"` lines.
static void load_rice_palette(Theme &t) {
  std::ifstream f(rice_toml());
  if (!f) return;
  std::string line, section;
  auto hex = [](const std::string &v) {  // strip quotes/inline comment
    size_t a = v.find('#');
    return a == std::string::npos ? "" : v.substr(a, 7);
  };
  auto ansi = [&](const std::string &k, const std::string &v, int base) {
    static const char *names[] = {"black", "red",     "green", "yellow",
                                  "blue",  "magenta", "cyan",  "white"};
    for (int i = 0; i < 8; i++)
      if (k == names[i]) {
        int r, g, b;
        std::string h = hex(v);
        if (parse_hex(h, r, g, b)) t.palette[base + i] = h;
      }
  };
  while (std::getline(f, line)) {
    std::string s = strip(line);
    if (s.empty() || s[0] == '#') continue;
    if (s[0] == '[') { section = s; continue; }
    size_t eq = s.find('=');
    if (eq == std::string::npos) continue;
    std::string k = strip(s.substr(0, eq)), v = strip(s.substr(eq + 1));
    std::string h = hex(v);
    int r, g, b;
    if (section == "[colors.primary]") {
      if (k == "background" && parse_hex(h, r, g, b)) t.bg = h;
      else if (k == "foreground" && parse_hex(h, r, g, b)) t.fg = h;
    } else if (section == "[colors.cursor]") {
      if (k == "cursor" && parse_hex(h, r, g, b)) t.cursor = h;
    } else if (section == "[colors.selection]") {
      if (k == "background" && parse_hex(h, r, g, b)) t.highlight = h;
    } else if (section == "[colors.normal]") {
      ansi(k, v, 0);
    } else if (section == "[colors.bright]") {
      ansi(k, v, 8);
    }
  }
}

void load_theme(Theme &t) {
  std::vector<std::string> paths;
  const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
  if (xdg && *xdg)
    paths.push_back(std::string(xdg) + "/srcterm/colors.conf");
  else if (home)
    paths.push_back(std::string(home) + "/.config/srcterm/colors.conf");
  paths.push_back(exe_dir() + "/colors.conf");
  for (const auto &path : paths) {
    std::ifstream f(path);
    if (!f) continue;
    std::string line;
    while (std::getline(f, line)) {
      std::string s = strip(line);
      if (s.empty() || s[0] == '#') continue;
      size_t eq = s.find('=');
      if (eq == std::string::npos) continue;
      std::string k = lower(strip(s.substr(0, eq))), v = strip(s.substr(eq + 1));
      int r, g, b;
      auto col = [&](std::string &fld) {
        if (parse_hex(v, r, g, b))
          fld = v;
        else
          fprintf(stderr, "srcterm: bad color '%s': %s\n", k.c_str(), v.c_str());
      };
      if (k == "foreground" || k == "fg") col(t.fg);
      else if (k == "background" || k == "bg") col(t.bg);
      else if (k == "cursor") col(t.cursor);
      else if (k == "highlight" || k == "selection") col(t.highlight);
      else if (k == "font") t.font = v;
      else if (k == "font_size" || k == "size") t.font_size = atoi(v.c_str());
      else if (k == "ui_scale" || k == "scale") t.ui_scale = atof(v.c_str());
      else if (k == "show_cwd" || k == "cwd") t.show_cwd = parse_bool(v, t.show_cwd);
      else if (k == "echo_command" || k == "echo")
        t.echo_command = parse_bool(v, t.echo_command);
      else if (k == "color_lines") t.color_lines = parse_bool(v, t.color_lines);
      else if (k == "rice") t.rice = parse_bool(v, t.rice);
      else if (k == "warn" || k == "warn_color" || k == "warning") col(t.warn);
      else if (k == "error" || k == "error_color") col(t.error);
      else if (k == "face" || k == "frame_face") col(t.face);
      else if (k == "light" || k == "frame_light") col(t.light);
      else if (k == "dark" || k == "frame_dark") col(t.dark);
      else if (k == "outer" || k == "frame_outer") col(t.outer);
      else if (k == "title_text") col(t.title_text);
      else if (k == "accent") col(t.accent);
      else if (k == "entry_bg") col(t.entry_bg);
      else if (k.rfind("color", 0) == 0) {
        int i = atoi(k.c_str() + 5);
        if (i >= 0 && i < 16) col(t.palette[i]);
      }
    }
    break;
  }
  // colors.conf wins for chrome + toggles; the rice theme drives the terminal
  // palette afterwards (unless disabled with `rice = false`).
  if (t.rice) load_rice_palette(t);
}

time_t theme_files_mtime() {
  time_t newest = 0;
  for (const std::string &p : {srcterm_conf(), rice_toml()}) {
    struct stat st;
    if (!p.empty() && stat(p.c_str(), &st) == 0 && st.st_mtime > newest)
      newest = st.st_mtime;
  }
  return newest;
}
