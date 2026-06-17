#include "png.h"

#include <algorithm>
#include <fstream>
#include <vector>

static uint32_t g_crc[256];
static void crc_init() {
  for (uint32_t n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
    g_crc[n] = c;
  }
}
static uint32_t crc32b(uint32_t c, const uint8_t *p, size_t n) {
  c ^= 0xffffffffu;
  for (size_t i = 0; i < n; i++) c = g_crc[(c ^ p[i]) & 0xff] ^ (c >> 8);
  return c ^ 0xffffffffu;
}
static void put32(std::vector<uint8_t> &o, uint32_t v) {
  o.push_back(v >> 24);
  o.push_back(v >> 16);
  o.push_back(v >> 8);
  o.push_back(v);
}
static void chunk(std::vector<uint8_t> &o, const char *type,
                  const std::vector<uint8_t> &d) {
  put32(o, d.size());
  size_t s = o.size();
  o.insert(o.end(), type, type + 4);
  o.insert(o.end(), d.begin(), d.end());
  put32(o, crc32b(0, &o[s], 4 + d.size()));
}
bool write_png(const char *path, int w, int h, const uint8_t *rgb) {
  crc_init();
  std::vector<uint8_t> raw;
  raw.reserve((size_t)h * (w * 3 + 1));
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    raw.insert(raw.end(), rgb + (size_t)y * w * 3, rgb + (size_t)(y + 1) * w * 3);
  }
  std::vector<uint8_t> z;
  z.push_back(0x78);
  z.push_back(0x01);
  size_t pos = 0;
  while (pos < raw.size()) {
    size_t n = std::min<size_t>(65535, raw.size() - pos);
    z.push_back(pos + n >= raw.size() ? 1 : 0);
    z.push_back(n & 0xff);
    z.push_back(n >> 8);
    z.push_back(~n & 0xff);
    z.push_back((~n >> 8) & 0xff);
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  put32(z, (b << 16) | a);
  std::vector<uint8_t> out = {137, 80, 78, 71, 13, 10, 26, 10};
  std::vector<uint8_t> ihdr;
  put32(ihdr, w);
  put32(ihdr, h);
  ihdr.push_back(8);
  ihdr.push_back(2);
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", {});
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f.write((char *)out.data(), out.size());
  return true;
}
