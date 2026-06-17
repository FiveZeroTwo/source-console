# Source Console (`srcterm`)

A terminal emulator skinned to look like the **Source Engine developer
console**. Built suckless-style — **raw Xlib + Xft** for the window/text and
**libvterm** for the terminal emulation. No GTK, no VTE.

```
window / input / render : Xlib + Xft (FreeType)
terminal engine         : libvterm   (feed pty bytes -> screen grid)
shell                   : zsh via forkpty() (full interactive)
```

A real, modern terminal (on par with Alacritty) wearing Source/VGUI chrome.

- Classic VGUI look: beveled "Console" window, Submit button, scrollbar — all
  hand-drawn. Drag the title bar to move, any edge/corner to resize, `_`/`x` to
  minimise/close.
- **Two input modes** (toggle with **Ctrl+`**, or click):
  - **Passthrough / direct** (default): every keystroke goes to the running
    program, encoded via libvterm's keyboard layer (application cursor-key /
    keypad modes + modifiers) — so vim, htop, less, REPLs, ssh all work like any
    modern terminal. A block cursor marks the shell/app cursor. Click the
    terminal area to enter this mode.
  - **Command box**: type in the bottom field and press Enter / Submit to run a
    line; `Up`/`Down` recall history; a `$PATH` autocomplete dropdown appears
    (`Tab` accepts). Click the input bar to enter this mode.
- Full interactive shell — your real `~/.zshrc` + starship prompt, zle line
  editing, echo. `exit` closes the window (zsh is our child; if it dies, we die).
- **Scrollback**: mouse-wheel over the output pane (50_000-line buffer).

The shell is launched via a private `ZDOTDIR` (`runtime/.zshrc`) that simply
sources your real `~/.zshrc`; your config is never modified.

## Build & run

```sh
cd ~/projects/source-console
make            # compiles ./srcterm
make install    # symlinks ~/.local/bin/srcterm -> ./srcterm
srcterm         # ~/.local/bin is on PATH
```

Runs natively on X11, and on Wayland via XWayland (no flags needed).

## Keys

| Key | Action |
|---|---|
| Ctrl+` | toggle passthrough ⇄ command box (or click the area) |
| *(passthrough)* any key | sent to the running program (vim/htop/shell/…) |
| Enter / Submit *(box)* | run the field's command in the shell |
| Tab *(box)* | accept the highlighted autocomplete entry |
| Up / Down *(box)* | navigate the dropdown, or recall history when closed |
| Esc *(box)* | close the dropdown |
| Ctrl+R *(box)* | rescan the command/alias completion list |
| Mouse wheel (over output) | scroll the scrollback |
| Ctrl + `+` / `-` / `0` | scale the **whole app** (font + chrome) up / down / reset |
| Ctrl+Shift + `+` / `-` / `0` | scale only the UI chrome (fine-tune) |
| Drag any edge/corner (or the orange corner grip) | resize the window |
| Enter on an empty field | sends a newline to the shell (continue prompts) |

## Customising the look

Everything — terminal colors, font, **and** the VGUI chrome — is read from a
config file at startup, no rebuild needed.

- Edit the shipped `colors.conf`, or override per-user by copying it to
  `~/.config/srcterm/colors.conf` (that location wins).
- Format `key = value`, `#` comments. Keys:
  - terminal: `foreground` `background` `cursor` `highlight` `font` `color0`..`color15`
  - sizing: `font_size` (overrides the size in `font`), `ui_scale` (chrome scale) —
    persist what you'd otherwise set live with Ctrl +/- and Ctrl+Shift +/-
  - chrome: `face` `light` `dark` `outer` `title_text` `accent` `entry_bg`
  - behavior: `show_cwd` (current dir in titlebar + before each command),
    `echo_command` (echo the submitted command into the output) — both `true`/`false`
- Omitted keys keep their built-in defaults; invalid colors are reported and
  skipped. Load order: `~/.config/srcterm/colors.conf` → shipped `colors.conf` →
  built-in defaults.

## How it works / deps

libvterm does all PTY/VT emulation: srcterm reads the pty master, feeds bytes to
libvterm (`vterm_input_write`), and renders the resulting cell grid with Xft.
The Submit field is written to the pty; `$PATH` is scanned once at startup for
completion. The window is a borderless (Motif-hint) but WM-managed top-level;
move/resize use `_NET_WM_MOVERESIZE`.

Build deps (`-dev` headers): `libx11-dev`, `libxft-dev`, `libfontconfig-dev`,
`libvterm-dev`, plus `-lutil` (forkpty) and a C++17 compiler. No GTK.

## Layout

The app is split into small modules under `src/`, all sharing the central
`App` state struct (`src/app.h`):

- `src/main.cpp` — startup + the X event loop.
- `src/terminal.*` — libvterm engine, pty/shell spawn, grid + font sizing/zoom.
- `src/render.*` — the VGUI chrome + terminal-grid compositor (Xft), screenshots.
- `src/input.*` — keyboard/mouse handling, passthrough, command submission.
- `src/completion.*` — `$PATH` + alias autocomplete for the command box.
- `src/theme.*` — the `Theme` struct and `colors.conf` loader.
- `src/png.*` — minimal PNG writer (debug screenshots).
- `src/util.*` — shared string / hex / UTF-8 helpers.
- `colors.conf` — colors + font (shipped default).
- `runtime/.zshrc` — pure-output shell rc (loaded via `ZDOTDIR`).

## Possible next steps

- Clipboard (X selections) — copy from the pane, paste into the field.
- Color-coded output lines (warnings yellow / errors red), Source-style.
- Tie the palette to the active `rice` theme.
- Smarter completion (history, aliases, fuzzy, common-prefix on Tab).
