// Small, dependency-free string / parsing / encoding helpers shared across the
// app (config parsing, color hex, UTF-8 encoding, locating the executable).
#pragma once
#include <cstdint>
#include <string>

// Trim leading/trailing ASCII whitespace.
std::string strip(const std::string &s);
// ASCII-lowercase a copy of `s`.
std::string lower(std::string s);
// Parse "#rrggbb" into 0-255 components; false (and r/g/b untouched) if malformed.
bool parse_hex(const std::string &s, int &r, int &g, int &b);
// Parse a boolean (1/true/yes/on vs 0/false/no/off); returns `dflt` if unclear.
bool parse_bool(const std::string &s, bool dflt);
// Directory containing this executable (via /proc/self/exe); "." on failure.
std::string exe_dir();
// Encode codepoint `cp` as UTF-8 into `o` (>=4 bytes); returns the byte count.
int enc_utf8(uint32_t cp, char *o);
