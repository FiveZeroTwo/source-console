#include "util.h"

#include <unistd.h>

#include <cctype>
#include <cstdlib>

std::string strip(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  size_t b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
  for (char &c : s) c = std::tolower((unsigned char)c);
  return s;
}

bool parse_hex(const std::string &s, int &r, int &g, int &b) {
  if (s.size() != 7 || s[0] != '#') return false;
  auto hx = [](const std::string &t) { return (int)strtol(t.c_str(), 0, 16); };
  r = hx(s.substr(1, 2));
  g = hx(s.substr(3, 2));
  b = hx(s.substr(5, 2));
  return true;
}

bool parse_bool(const std::string &s, bool dflt) {
  std::string v = lower(strip(s));
  if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
  if (v == "0" || v == "false" || v == "no" || v == "off") return false;
  return dflt;
}

std::string exe_dir() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) return ".";
  buf[n] = 0;
  std::string p(buf);
  auto s = p.find_last_of('/');
  return s == std::string::npos ? "." : p.substr(0, s);
}

int enc_utf8(uint32_t cp, char *o) {
  if (cp < 0x80) { o[0] = cp; return 1; }
  if (cp < 0x800) { o[0] = 0xc0|(cp>>6); o[1] = 0x80|(cp&0x3f); return 2; }
  if (cp < 0x10000) {
    o[0]=0xe0|(cp>>12); o[1]=0x80|((cp>>6)&0x3f); o[2]=0x80|(cp&0x3f); return 3;
  }
  o[0]=0xf0|(cp>>18); o[1]=0x80|((cp>>12)&0x3f);
  o[2]=0x80|((cp>>6)&0x3f); o[3]=0x80|(cp&0x3f); return 4;
}
