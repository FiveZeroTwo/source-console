// Theme = every configurable color, font and behavior toggle. Loaded from
// colors.conf at startup (see load_theme); fields keep their defaults otherwise.
#pragma once
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
};

// Load colors/font from $XDG_CONFIG_HOME/srcterm/colors.conf (or ~/.config/…),
// else the shipped colors.conf, else the built-in defaults. Format: key = value.
void load_theme(Theme &t);
