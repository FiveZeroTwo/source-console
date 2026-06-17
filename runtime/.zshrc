# srcterm runtime rc — loaded via ZDOTDIR. Output-only console mode (Source/
# Quake style): the panel shows the OUTPUT of commands run from the box, with no
# shell prompt and no echo of the typed line. Your real ~/.zshrc is still the
# source of truth for aliases / env / functions / PATH; we just load it and then
# strip the interactive chrome on top.
[[ -f "$HOME/.zshrc" ]] && source "$HOME/.zshrc"

# Drop prompt-drawing hooks (starship et al. register a precmd that sets PROMPT).
precmd_functions=(); preexec_functions=()
# No prompt, no continuation prompt, no end-of-partial-line mark.
PROMPT=''; RPROMPT=''; PS2=''; PROMPT_EOL_MARK=''
# Disable the line editor (zle echoes typed input) and the partial-line padding
# zsh prints around prompts, so nothing but command output reaches the screen.
unsetopt zle PROMPT_SP PROMPT_CR 2>/dev/null
# Belt-and-braces: also turn off the tty's own echo.
stty -echo 2>/dev/null
