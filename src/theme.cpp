#include "theme.h"

#include "util.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

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
}
