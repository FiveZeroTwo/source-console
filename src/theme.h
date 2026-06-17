// Theme = every configurable color, font and behavior toggle. Loaded from
// colors.conf at startup (see load_theme); fields keep their defaults otherwise.
#pragma once
#include <ctime>
#include <string>

struct Theme {
  // terminal
  std::string fg = "#1d1d17", bg = "#c6c2b4", cursor = "#2b2b22",
              highlight = "#a6a290";
  std::string font = "JetBrainsMono Nerd Font 11";
  int font_size = 0;      // explicit point size; 0 = take it from `font`
  double ui_scale = 1.0;  // persistent chrome/UI scale (Ctrl+Shift +/- at runtime)
  std::string palette[16] = {"#3a382f", "#9c2f28", "#4f6a1e", "#8a6516",
                             "#2f5680", "#7e3a6c", "#2f6a62", "#56544a",
                             "#5a5848", "#bb3a30", "#62801f", "#a87f1c",
                             "#3a6aa0", "#9a4a86", "#3a8a80", "#2b2b22"};
  // chrome (classic light VGUI)
  std::string face = "#c6c2b4", light = "#e8e4d6", dark = "#86837a",
              outer = "#3f3d36", title_text = "#26261f", accent = "#d08a2e",
              entry_bg = "#e6e3d7";
  // behavior toggles
  bool show_cwd = true;      // show the shell's cwd in the titlebar + prompt echo
  bool echo_command = true;  // echo each submitted command into the output log
  bool rice = true;          // overlay the active rice theme's terminal palette
  // Source-style line coloring: tint default-fg output by content (off = never).
  bool color_lines = true;
  std::string warn = "#e6c46a";   // lines that look like warnings → yellow
  std::string error = "#d6786a";  // lines that look like errors   → red
};

// Load colors/font from $XDG_CONFIG_HOME/srcterm/colors.conf (or ~/.config/…),
// else the shipped colors.conf, else the built-in defaults. Format: key = value.
// If `rice` is set, the active rice theme's palette is overlaid afterwards.
void load_theme(Theme &t);

// Newest mtime across the theme source files (colors.conf + the rice theme),
// for hot-reload: re-load when this changes. 0 if none are readable.
time_t theme_files_mtime();
