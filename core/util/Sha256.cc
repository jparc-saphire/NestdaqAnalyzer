#include "util/Sha256.hh"

#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

namespace nda::util {

namespace {

constexpr std::array<std::uint32_t, 64> kK = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

std::uint32_t Rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

class Sha256 {
 public:
  void Update(const unsigned char* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
      fBlock[fUsed++] = data[i];
      if (fUsed == 64) {
        Compress();
        fUsed = 0;
      }
    }
    fBits += std::uint64_t{size} * 8;
  }

  std::string Hex() {
    const std::uint64_t bits = fBits;
    const unsigned char pad = 0x80;
    Update(&pad, 1);
    const unsigned char zero = 0;
    while (fUsed != 56) Update(&zero, 1);
    unsigned char length[8];
    for (int i = 0; i < 8; ++i) length[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
    Update(length, 8);
    std::string out;
    char buf[9];
    for (auto h : fH) {
      std::snprintf(buf, sizeof buf, "%08x", h);
      out += buf;
    }
    return out;
  }

 private:
  void Compress() {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (std::uint32_t{fBlock[4 * i]} << 24) | (std::uint32_t{fBlock[4 * i + 1]} << 16) |
             (std::uint32_t{fBlock[4 * i + 2]} << 8) | std::uint32_t{fBlock[4 * i + 3]};
    }
    for (int i = 16; i < 64; ++i) {
      const std::uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = fH[0], b = fH[1], c = fH[2], d = fH[3], e = fH[4], f = fH[5], g = fH[6], h = fH[7];
    for (int i = 0; i < 64; ++i) {
      const std::uint32_t t1 = h + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
      const std::uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    fH[0] += a;
    fH[1] += b;
    fH[2] += c;
    fH[3] += d;
    fH[4] += e;
    fH[5] += f;
    fH[6] += g;
    fH[7] += h;
  }

  std::array<std::uint32_t, 8> fH = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  unsigned char fBlock[64] = {};
  std::size_t fUsed = 0;
  std::uint64_t fBits = 0;
};

}  // namespace

std::string Sha256Hex(const std::string& data) {
  Sha256 s;
  s.Update(reinterpret_cast<const unsigned char*>(data.data()), data.size());
  return s.Hex();
}

bool Sha256File(const std::string& path, std::string& hex, std::string& error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot open " + path;
    return false;
  }
  Sha256 s;
  std::vector<char> buffer(1 << 16);
  while (in) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto n = in.gcount();
    if (n > 0) s.Update(reinterpret_cast<const unsigned char*>(buffer.data()), static_cast<std::size_t>(n));
  }
  if (in.bad()) {
    error = "cannot read " + path;
    return false;
  }
  hex = s.Hex();
  return true;
}

}  // namespace nda::util
