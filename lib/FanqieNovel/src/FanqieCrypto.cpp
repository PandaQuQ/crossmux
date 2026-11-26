#include "FanqieCrypto.h"

#if !FANQIE_NOVEL

namespace fanqie_internal {
constexpr int kOmitted = 0;
}

#else

#include "../../miniz/src/MinizConfig.h"

#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

namespace FanqieCrypto {
namespace {

constexpr uint8_t kSbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9,
    0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f,
    0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07,
    0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3,
    0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58,
    0xcf, 0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3,
    0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f,
    0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73, 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
    0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac,
    0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a,
    0xae, 0x08, 0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70,
    0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11,
    0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf, 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42,
    0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

constexpr uint8_t kInvSbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb, 0x7c, 0xe3, 0x39,
    0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb, 0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2,
    0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e, 0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76,
    0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25, 0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc,
    0x5d, 0x65, 0xb6, 0x92, 0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d,
    0x84, 0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06, 0xd0, 0x2c,
    0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b, 0x3a, 0x91, 0x11, 0x41, 0x4f,
    0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73, 0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85,
    0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e, 0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62,
    0x0e, 0xaa, 0x18, 0xbe, 0x1b, 0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd,
    0x5a, 0xf4, 0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f, 0x60,
    0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef, 0xa0, 0xe0, 0x3b, 0x4d,
    0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61, 0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6,
    0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d};

constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr char kArgusSignKey[] = "fc78e0a9657a0c748ce51559903ccf03510e51d3cff232d71343e88a321c5304";
constexpr char kArgusAesKey[] = "d31e3718288a1027baab59f146a09a9c";
constexpr char kArgusAesIv[] = "ea180a0336ed352fcd24e4d50018ae54";
constexpr uint8_t kArgusPrefix[2] = {0xf2, 0x81};
constexpr uint8_t kArgusMagic[8] = {0xf2, 0xf7, 0xfc, 0xff, 0xf2, 0xf7, 0xfc, 0xff};
constexpr uint8_t kArgusTail[9] = {0xea, 0xfb, 0x2c, 0xfe, 0x85, 0x68, 0x51, 0x91, 0x54};
constexpr char kRegisterMaster[] = "ac25c67ddd8f38c1b37a2348828e222e";
constexpr char kRegisterReqKey[] = "rCXGfd2POMGzeiNIgo4iLg==";
constexpr uint64_t kSimonZ = 0x3dc94c3a046d678bULL;

uint8_t xtime(uint8_t value) { return static_cast<uint8_t>((value << 1) ^ ((value & 0x80) ? 0x1b : 0)); }

uint8_t gmul(uint8_t a, uint8_t b) {
  uint8_t result = 0;
  while (b) {
    if (b & 1) result = static_cast<uint8_t>(result ^ a);
    a = xtime(a);
    b = static_cast<uint8_t>(b >> 1);
  }
  return result;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool hexToBytes(const char* hex, uint8_t* out, size_t outLen) {
  if (!hex) return false;
  for (size_t i = 0; i < outLen; ++i) {
    const int hi = hexValue(hex[i * 2]);
    const int lo = hexValue(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return hex[outLen * 2] == '\0';
}

void bytesToHex(const uint8_t* in, size_t len, char* out, bool upper) {
  constexpr char kLower[] = "0123456789abcdef";
  constexpr char kUpper[] = "0123456789ABCDEF";
  const char* digits = upper ? kUpper : kLower;
  for (size_t i = 0; i < len; ++i) {
    out[i * 2] = digits[in[i] >> 4];
    out[i * 2 + 1] = digits[in[i] & 0x0f];
  }
  out[len * 2] = '\0';
}

void expandAes128(const uint8_t key[kAesBlock], uint8_t roundKeys[176]) {
  memcpy(roundKeys, key, kAesBlock);
  uint8_t rcon = 1;
  for (int i = kAesBlock; i < 176; i += 4) {
    uint8_t t0 = roundKeys[i - 4];
    uint8_t t1 = roundKeys[i - 3];
    uint8_t t2 = roundKeys[i - 2];
    uint8_t t3 = roundKeys[i - 1];
    if (i % kAesBlock == 0) {
      const uint8_t tmp = t0;
      t0 = static_cast<uint8_t>(kSbox[t1] ^ rcon);
      t1 = kSbox[t2];
      t2 = kSbox[t3];
      t3 = kSbox[tmp];
      rcon = xtime(rcon);
    }
    roundKeys[i] = static_cast<uint8_t>(roundKeys[i - kAesBlock] ^ t0);
    roundKeys[i + 1] = static_cast<uint8_t>(roundKeys[i - kAesBlock + 1] ^ t1);
    roundKeys[i + 2] = static_cast<uint8_t>(roundKeys[i - kAesBlock + 2] ^ t2);
    roundKeys[i + 3] = static_cast<uint8_t>(roundKeys[i - kAesBlock + 3] ^ t3);
  }
}

void encryptBlock(const uint8_t in[kAesBlock], const uint8_t* roundKeys, uint8_t out[kAesBlock]) {
  uint8_t state[kAesBlock];
  for (int i = 0; i < 16; ++i) state[i] = static_cast<uint8_t>(in[i] ^ roundKeys[i]);
  for (int round = 1; round <= 10; ++round) {
    for (int i = 0; i < 16; ++i) state[i] = kSbox[state[i]];
    uint8_t t = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = t;
    t = state[2];
    state[2] = state[10];
    state[10] = t;
    t = state[6];
    state[6] = state[14];
    state[14] = t;
    t = state[3];
    state[3] = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = t;
    if (round != 10) {
      for (int c = 0; c < 4; ++c) {
        const int i = c * 4;
        const uint8_t a0 = state[i];
        const uint8_t a1 = state[i + 1];
        const uint8_t a2 = state[i + 2];
        const uint8_t a3 = state[i + 3];
        state[i] = static_cast<uint8_t>(gmul(a0, 2) ^ gmul(a1, 3) ^ a2 ^ a3);
        state[i + 1] = static_cast<uint8_t>(a0 ^ gmul(a1, 2) ^ gmul(a2, 3) ^ a3);
        state[i + 2] = static_cast<uint8_t>(a0 ^ a1 ^ gmul(a2, 2) ^ gmul(a3, 3));
        state[i + 3] = static_cast<uint8_t>(gmul(a0, 3) ^ a1 ^ a2 ^ gmul(a3, 2));
      }
    }
    for (int i = 0; i < 16; ++i) state[i] = static_cast<uint8_t>(state[i] ^ roundKeys[round * 16 + i]);
  }
  memcpy(out, state, kAesBlock);
}

void decryptBlock(const uint8_t in[kAesBlock], const uint8_t* roundKeys, uint8_t out[kAesBlock]) {
  uint8_t state[kAesBlock];
  for (int i = 0; i < 16; ++i) state[i] = static_cast<uint8_t>(in[i] ^ roundKeys[160 + i]);
  for (int round = 9; round >= 0; --round) {
    uint8_t t = state[13];
    state[13] = state[9];
    state[9] = state[5];
    state[5] = state[1];
    state[1] = t;
    t = state[2];
    state[2] = state[10];
    state[10] = t;
    t = state[6];
    state[6] = state[14];
    state[14] = t;
    t = state[3];
    state[3] = state[7];
    state[7] = state[11];
    state[11] = state[15];
    state[15] = t;
    for (int i = 0; i < 16; ++i) state[i] = kInvSbox[state[i]];
    for (int i = 0; i < 16; ++i) state[i] = static_cast<uint8_t>(state[i] ^ roundKeys[round * 16 + i]);
    if (round != 0) {
      for (int c = 0; c < 4; ++c) {
        const int i = c * 4;
        const uint8_t a0 = state[i];
        const uint8_t a1 = state[i + 1];
        const uint8_t a2 = state[i + 2];
        const uint8_t a3 = state[i + 3];
        state[i] = static_cast<uint8_t>(gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9));
        state[i + 1] = static_cast<uint8_t>(gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13));
        state[i + 2] = static_cast<uint8_t>(gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11));
        state[i + 3] = static_cast<uint8_t>(gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14));
      }
    }
  }
  memcpy(out, state, kAesBlock);
}

bool pkcs7Unpad(uint8_t* data, size_t len, size_t& outLen) {
  if (len == 0 || (len % kAesBlock) != 0) return false;
  const uint8_t pad = data[len - 1];
  if (pad == 0 || pad > kAesBlock || pad > len) return false;
  for (size_t i = 0; i < pad; ++i) {
    if (data[len - 1 - i] != pad) return false;
  }
  outLen = len - pad;
  return true;
}

uint32_t rotl32(uint32_t value, unsigned shift) {
  shift &= 31;
  if (shift == 0) return value;
  return (value << shift) | (value >> (32 - shift));
}

uint64_t rotl64(uint64_t value, unsigned shift) {
  shift &= 63;
  if (shift == 0) return value;
  return (value << shift) | (value >> (64 - shift));
}

uint64_t rotr64(uint64_t value, unsigned shift) {
  shift &= 63;
  if (shift == 0) return value;
  return (value >> shift) | (value << (64 - shift));
}

uint64_t readU64Le(const uint8_t* p) {
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i) value |= static_cast<uint64_t>(p[i]) << (8 * i);
  return value;
}

void writeU64Le(uint8_t* p, uint64_t value) {
  for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(value >> (8 * i));
}

void writeU32Be(uint8_t* p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value >> 24);
  p[1] = static_cast<uint8_t>(value >> 16);
  p[2] = static_cast<uint8_t>(value >> 8);
  p[3] = static_cast<uint8_t>(value);
}

uint32_t sm3P0(uint32_t x) { return x ^ rotl32(x, 9) ^ rotl32(x, 17); }
uint32_t sm3P1(uint32_t x) { return x ^ rotl32(x, 15) ^ rotl32(x, 23); }

void sm3Compress(uint32_t v[8], const uint8_t block[64]) {
  uint32_t w[68];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | block[i * 4 + 3];
  }
  for (int j = 16; j < 68; ++j) {
    w[j] = sm3P1(w[j - 16] ^ w[j - 9] ^ rotl32(w[j - 3], 15)) ^ rotl32(w[j - 13], 7) ^ w[j - 6];
  }
  uint32_t a = v[0], b = v[1], c = v[2], d = v[3], e = v[4], f = v[5], g = v[6], h = v[7];
  for (int j = 0; j < 64; ++j) {
    const uint32_t w1 = w[j] ^ w[j + 4];
    const uint32_t tj = j < 16 ? 0x79cc4519u : 0x7a879d8au;
    const uint32_t ss1 = rotl32(rotl32(a, 12) + e + rotl32(tj, static_cast<unsigned>(j)), 7);
    const uint32_t ss2 = ss1 ^ rotl32(a, 12);
    const uint32_t ff = j < 16 ? (a ^ b ^ c) : ((a & b) | (a & c) | (b & c));
    const uint32_t gg = j < 16 ? (e ^ f ^ g) : ((e & f) | (~e & g));
    const uint32_t tt1 = ff + d + ss2 + w1;
    const uint32_t tt2 = gg + h + ss1 + w[j];
    d = c;
    c = rotl32(b, 9);
    b = a;
    a = tt1;
    h = g;
    g = rotl32(f, 19);
    f = e;
    e = sm3P0(tt2);
  }
  v[0] ^= a;
  v[1] ^= b;
  v[2] ^= c;
  v[3] ^= d;
  v[4] ^= e;
  v[5] ^= f;
  v[6] ^= g;
  v[7] ^= h;
}

constexpr uint32_t kMd5S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                                14, 20, 5,  9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                4, 11, 16, 23, 6,  10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
constexpr uint32_t kMd5K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

uint32_t readU32Le(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

void writeU32Le(uint8_t* p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value);
  p[1] = static_cast<uint8_t>(value >> 8);
  p[2] = static_cast<uint8_t>(value >> 16);
  p[3] = static_cast<uint8_t>(value >> 24);
}

void md5Block(uint32_t state[4], const uint8_t block[64]) {
  uint32_t m[16];
  for (int i = 0; i < 16; ++i) m[i] = readU32Le(block + i * 4);
  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  for (int i = 0; i < 64; ++i) {
    uint32_t f = 0;
    int g = 0;
    if (i < 16) {
      f = (b & c) | (~b & d);
      g = i;
    } else if (i < 32) {
      f = (d & b) | (~d & c);
      g = (5 * i + 1) % 16;
    } else if (i < 48) {
      f = b ^ c ^ d;
      g = (3 * i + 5) % 16;
    } else {
      f = c ^ (b | ~d);
      g = (7 * i) % 16;
    }
    const uint32_t tmp = d;
    d = c;
    c = b;
    b = b + rotl32(a + f + kMd5K[i] + m[g], kMd5S[i]);
    a = tmp;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
}

bool appendBytes(uint8_t* buf, size_t cap, size_t& len, const uint8_t* data, size_t n) {
  if (len + n > cap) return false;
  if (n) memcpy(buf + len, data, n);
  len += n;
  return true;
}

bool writeVarint(uint8_t* buf, size_t cap, size_t& len, uint32_t value) {
  do {
    uint8_t byte = static_cast<uint8_t>(value & 0x7f);
    value >>= 7;
    if (value) byte = static_cast<uint8_t>(byte | 0x80);
    if (!appendBytes(buf, cap, len, &byte, 1)) return false;
  } while (value);
  return true;
}

bool writeKey(uint8_t* buf, size_t cap, size_t& len, uint32_t field, uint32_t wire) {
  return writeVarint(buf, cap, len, (field << 3) | wire);
}

bool writeVarintField(uint8_t* buf, size_t cap, size_t& len, uint32_t field, uint32_t value) {
  return writeKey(buf, cap, len, field, 0) && writeVarint(buf, cap, len, value);
}

bool writeBytesField(uint8_t* buf, size_t cap, size_t& len, uint32_t field, const uint8_t* data, size_t n) {
  return writeKey(buf, cap, len, field, 2) && writeVarint(buf, cap, len, static_cast<uint32_t>(n)) &&
         appendBytes(buf, cap, len, data, n);
}

bool writeTextField(uint8_t* buf, size_t cap, size_t& len, uint32_t field, const char* text) {
  const size_t n = text ? strlen(text) : 0;
  return writeBytesField(buf, cap, len, field, reinterpret_cast<const uint8_t*>(text ? text : ""), n);
}

void simonExpand(const uint64_t keyIn[4], uint64_t out[72]) {
  for (int i = 0; i < 4; ++i) out[i] = keyIn[i];
  for (int i = 4; i < 72; ++i) {
    uint64_t tmp = rotr64(out[i - 1], 3);
    tmp ^= out[i - 3];
    tmp ^= rotr64(tmp, 1);
    const uint64_t notVal = ~out[i - 4];
    const uint64_t bit = (kSimonZ >> static_cast<unsigned>((i - 4) % 62)) & 1ULL;
    out[i] = notVal ^ tmp ^ bit ^ 3ULL;
  }
}

void simonEncryptBlock(const uint64_t roundKey[72], const uint8_t in[16], uint8_t out[16]) {
  uint64_t xi = readU64Le(in);
  uint64_t xi1 = readU64Le(in + 8);
  for (int i = 0; i < 72; ++i) {
    const uint64_t tmp = xi1;
    const uint64_t f = rotl64(xi1, 1) & rotl64(xi1, 8);
    xi1 = xi ^ f ^ rotl64(xi1, 2) ^ roundKey[i];
    xi = tmp;
  }
  writeU64Le(out, xi);
  writeU64Le(out + 8, xi1);
}

void reverseBytes(uint8_t* data, size_t len) {
  for (size_t i = 0; i < len / 2; ++i) {
    const uint8_t tmp = data[i];
    data[i] = data[len - 1 - i];
    data[len - 1 - i] = tmp;
  }
}

bool queryParam(const char* query, const char* name, char* out, size_t outCap) {
  if (!out || outCap == 0) return false;
  out[0] = '\0';
  if (!query || !name) return true;
  const size_t nameLen = strlen(name);
  const char* cursor = query;
  while (*cursor) {
    const char* end = strchr(cursor, '&');
    if (!end) end = cursor + strlen(cursor);
    const char* eq = static_cast<const char*>(memchr(cursor, '=', static_cast<size_t>(end - cursor)));
    if (eq && static_cast<size_t>(eq - cursor) == nameLen && memcmp(cursor, name, nameLen) == 0) {
      const size_t n = static_cast<size_t>(end - (eq + 1));
      if (n >= outCap) return false;
      memcpy(out, eq + 1, n);
      out[n] = '\0';
      return true;
    }
    cursor = *end ? end + 1 : end;
  }
  return true;
}

void ladonExpand(const char* md5Hex, uint8_t ht[280]) {
  memset(ht, 0, 280);
  const size_t n = md5Hex ? strlen(md5Hex) : 0;
  memcpy(ht, md5Hex, n < 32 ? n : 32);
  uint64_t temp[40];
  int tempLen = 0;
  for (int i = 0; i < 4; ++i) temp[tempLen++] = readU64Le(ht + i * 8);
  uint64_t buf0 = temp[0];
  uint64_t buf8 = temp[1];
  memmove(temp, temp + 2, static_cast<size_t>(tempLen - 2) * sizeof(uint64_t));
  tempLen -= 2;
  for (int i = 0; i < 34; ++i) {
    const uint64_t x9 = buf0;
    uint64_t x8 = rotr64(buf8, 8);
    x8 = (x8 + x9) ^ static_cast<uint64_t>(i);
    temp[tempLen++] = x8;
    x8 ^= rotl64(x9, 3);
    writeU64Le(ht + (i + 1) * 8, x8);
    buf0 = x8;
    buf8 = temp[0];
    memmove(temp, temp + 1, static_cast<size_t>(tempLen - 1) * sizeof(uint64_t));
    --tempLen;
  }
}

void ladonEncryptBlock(const uint8_t ht[280], const uint8_t in[16], uint8_t out[16]) {
  uint64_t data0 = readU64Le(in);
  uint64_t data1 = readU64Le(in + 8);
  for (int i = 0; i < 34; ++i) {
    const uint64_t hash = readU64Le(ht + i * 8);
    data1 = hash ^ (data0 + rotr64(data1, 8));
    data0 = data1 ^ rotl64(data0, 3);
  }
  writeU64Le(out, data0);
  writeU64Le(out + 8, data1);
}

uint32_t crc32(const uint8_t* data, size_t len) { return static_cast<uint32_t>(mz_crc32(0, data, len)); }

bool skipGzipHeader(const uint8_t* in, size_t len, size_t& offset) {
  if (len < 10 || in[0] != 0x1f || in[1] != 0x8b || in[2] != 8) return false;
  const uint8_t flags = in[3];
  offset = 10;
  if (flags & 0x04) {
    if (offset + 2 > len) return false;
    const size_t extra = static_cast<size_t>(in[offset]) | (static_cast<size_t>(in[offset + 1]) << 8);
    offset += 2 + extra;
  }
  if (flags & 0x08) {
    while (offset < len && in[offset] != 0) ++offset;
    if (offset >= len) return false;
    ++offset;
  }
  if (flags & 0x10) {
    while (offset < len && in[offset] != 0) ++offset;
    if (offset >= len) return false;
    ++offset;
  }
  if (flags & 0x02) offset += 2;
  return offset + 8 <= len;
}

}  // namespace

bool base64Decode(const char* in, size_t inLen, uint8_t* out, size_t outCap, size_t& outLen) {
  outLen = 0;
  if (!in || !out) return false;
  int val = 0;
  int valb = -8;
  for (size_t i = 0; i < inLen; ++i) {
    const char c = in[i];
    if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
    const char* pos = strchr(kB64, c);
    if (!pos) return false;
    val = (val << 6) | static_cast<int>(pos - kB64);
    valb += 6;
    if (valb >= 0) {
      if (outLen >= outCap) return false;
      out[outLen++] = static_cast<uint8_t>((val >> valb) & 0xff);
      valb -= 8;
    }
  }
  return true;
}

bool base64Encode(const uint8_t* in, size_t inLen, char* out, size_t outCap, size_t& outLen) {
  if (!out) return false;
  outLen = 4 * ((inLen + 2) / 3);
  if (outLen + 1 > outCap) return false;
  size_t o = 0;
  for (size_t i = 0; i < inLen; i += 3) {
    const uint32_t n = (static_cast<uint32_t>(in[i]) << 16) | ((i + 1 < inLen ? in[i + 1] : 0) << 8) |
                       (i + 2 < inLen ? in[i + 2] : 0);
    out[o++] = kB64[(n >> 18) & 63];
    out[o++] = kB64[(n >> 12) & 63];
    out[o++] = i + 1 < inLen ? kB64[(n >> 6) & 63] : '=';
    out[o++] = i + 2 < inLen ? kB64[n & 63] : '=';
  }
  out[o] = '\0';
  outLen = o;
  return true;
}

bool aes128CbcEncrypt(const uint8_t key[kAesBlock], const uint8_t iv[kAesBlock], const uint8_t* plain, size_t plainLen,
                      uint8_t* out, size_t outCap, size_t& outLen) {
  if (!key || !iv || (!plain && plainLen) || !out) return false;
  const size_t pad = kAesBlock - (plainLen % kAesBlock);
  outLen = plainLen + pad;
  if (outLen > outCap) return false;
  uint8_t roundKeys[176];
  expandAes128(key, roundKeys);
  uint8_t prev[kAesBlock];
  memcpy(prev, iv, kAesBlock);
  for (size_t offset = 0; offset < outLen; offset += kAesBlock) {
    uint8_t block[kAesBlock];
    for (size_t i = 0; i < kAesBlock; ++i) {
      const uint8_t src = offset + i < plainLen ? plain[offset + i] : static_cast<uint8_t>(pad);
      block[i] = static_cast<uint8_t>(src ^ prev[i]);
    }
    encryptBlock(block, roundKeys, out + offset);
    memcpy(prev, out + offset, kAesBlock);
  }
  return true;
}

bool aes128CbcDecrypt(const uint8_t key[kAesBlock], const uint8_t iv[kAesBlock], const uint8_t* cipher, size_t cipherLen,
                      uint8_t* out, size_t outCap, size_t& outLen) {
  if (!key || !iv || !cipher || !out || cipherLen == 0 || (cipherLen % kAesBlock) != 0 || cipherLen > outCap)
    return false;
  uint8_t roundKeys[176];
  expandAes128(key, roundKeys);
  uint8_t prev[kAesBlock];
  memcpy(prev, iv, kAesBlock);
  for (size_t offset = 0; offset < cipherLen; offset += kAesBlock) {
    uint8_t decrypted[kAesBlock];
    decryptBlock(cipher + offset, roundKeys, decrypted);
    for (size_t i = 0; i < kAesBlock; ++i) out[offset + i] = static_cast<uint8_t>(decrypted[i] ^ prev[i]);
    memcpy(prev, cipher + offset, kAesBlock);
  }
  return pkcs7Unpad(out, cipherLen, outLen);
}

void sm3(const uint8_t* data, size_t len, uint8_t out[kSm3Len]) {
  uint32_t v[8] = {0x7380166f, 0x4914b2b9, 0x172442d7, 0xda8a0600, 0xa96f30bc, 0x163138aa, 0xe38dee4d, 0xb0fb0e4e};
  uint8_t block[64];
  size_t offset = 0;
  while (len - offset >= 64) {
    sm3Compress(v, data + offset);
    offset += 64;
  }
  const size_t rest = len - offset;
  memset(block, 0, sizeof(block));
  if (rest) memcpy(block, data + offset, rest);
  block[rest] = 0x80;
  if (rest >= 56) {
    sm3Compress(v, block);
    memset(block, 0, sizeof(block));
  }
  const uint64_t bits = static_cast<uint64_t>(len) * 8;
  writeU32Be(block + 56, static_cast<uint32_t>(bits >> 32));
  writeU32Be(block + 60, static_cast<uint32_t>(bits));
  sm3Compress(v, block);
  for (int i = 0; i < 8; ++i) writeU32Be(out + i * 4, v[i]);
}

void md5(const uint8_t* data, size_t len, uint8_t out[kMd5Len]) {
  uint32_t state[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
  uint8_t block[64];
  size_t offset = 0;
  while (len - offset >= 64) {
    md5Block(state, data + offset);
    offset += 64;
  }
  const size_t rest = len - offset;
  memset(block, 0, sizeof(block));
  if (rest) memcpy(block, data + offset, rest);
  block[rest] = 0x80;
  if (rest >= 56) {
    md5Block(state, block);
    memset(block, 0, sizeof(block));
  }
  const uint64_t bits = static_cast<uint64_t>(len) * 8;
  writeU32Le(block + 56, static_cast<uint32_t>(bits));
  writeU32Le(block + 60, static_cast<uint32_t>(bits >> 32));
  md5Block(state, block);
  for (int i = 0; i < 4; ++i) writeU32Le(out + i * 4, state[i]);
}

bool gzipStore(const uint8_t* in, size_t inLen, uint8_t* out, size_t outCap, size_t& outLen) {
  if (inLen > 65535 || !out || (!in && inLen)) return false;
  outLen = 10 + 5 + inLen + 8;
  if (outLen > outCap) return false;
  out[0] = 0x1f;
  out[1] = 0x8b;
  out[2] = 8;
  memset(out + 3, 0, 6);
  out[9] = 0xff;
  out[10] = 0x01;
  out[11] = static_cast<uint8_t>(inLen);
  out[12] = static_cast<uint8_t>(inLen >> 8);
  const uint16_t nlen = static_cast<uint16_t>(~static_cast<uint16_t>(inLen));
  out[13] = static_cast<uint8_t>(nlen);
  out[14] = static_cast<uint8_t>(nlen >> 8);
  if (inLen) memcpy(out + 15, in, inLen);
  const uint8_t empty = 0;
  const uint32_t crc = crc32(inLen ? in : &empty, inLen);
  writeU32Le(out + 15 + inLen, crc);
  writeU32Le(out + 19 + inLen, static_cast<uint32_t>(inLen));
  return true;
}

bool gunzip(const uint8_t* in, size_t inLen, uint8_t* out, size_t outCap, size_t& outLen) {
  size_t offset = 0;
  if (!in || !out || !skipGzipHeader(in, inLen, offset) || offset + 8 > inLen) return false;
  const size_t deflateLen = inLen - offset - 8;
  const size_t produced =
      tinfl_decompress_mem_to_mem(out, outCap, in + offset, deflateLen, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  if (produced == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) return false;
  outLen = produced;
  return true;
}

bool decryptContent(const char* base64, size_t base64Len, const char* keyHex, uint8_t* out, size_t outCap,
                    size_t& outLen) {
  // One chapter body. Heap, not stack: the caller may be the UI task.
  constexpr size_t kCap = 48 * 1024;
  uint8_t key[kAesBlock];
  if (!hexToBytes(keyHex, key, kAesBlock)) return false;
  auto raw = makeUniqueNoThrow<uint8_t[]>(kCap);
  if (!raw) {
    LOG_ERR("FQ", "OOM: chapter cipher %u", static_cast<unsigned>(kCap));
    return false;
  }
  size_t rawLen = 0;
  if (!base64Decode(base64, base64Len, raw.get(), kCap, rawLen) || rawLen < kAesBlock * 2) return false;
  const size_t cipherLen = rawLen - kAesBlock;
  if ((cipherLen % kAesBlock) != 0 || cipherLen > outCap) return false;
  size_t plainLen = 0;
  if (!aes128CbcDecrypt(key, raw.get(), raw.get() + kAesBlock, cipherLen, out, outCap, plainLen)) return false;
  if (plainLen >= 2 && out[0] == 0x1f && out[1] == 0x8b) {
    auto inflated = makeUniqueNoThrow<uint8_t[]>(kCap);
    if (!inflated) {
      LOG_ERR("FQ", "OOM: chapter inflate %u", static_cast<unsigned>(kCap));
      return false;
    }
    size_t inflatedLen = 0;
    if (!gunzip(out, plainLen, inflated.get(), kCap, inflatedLen) || inflatedLen > outCap) return false;
    memcpy(out, inflated.get(), inflatedLen);
    outLen = inflatedLen;
    return true;
  }
  outLen = plainLen;
  return true;
}

bool decryptRegisterKey(const char* encryptedKeyBase64, uint8_t* out, size_t outCap, size_t& outLen) {
  uint8_t key[kAesBlock];
  if (!hexToBytes(kRegisterMaster, key, kAesBlock)) return false;
  uint8_t raw[128];
  size_t rawLen = 0;
  if (!encryptedKeyBase64 || !base64Decode(encryptedKeyBase64, strlen(encryptedKeyBase64), raw, sizeof(raw), rawLen) ||
      rawLen <= kAesBlock)
    return false;
  return aes128CbcDecrypt(key, raw, raw + kAesBlock, rawLen - kAesBlock, out, outCap, outLen);
}

bool encryptRegisterKeyRequest(const char* deviceId, const uint8_t iv[kAesBlock], char* out, size_t outCap,
                               size_t& outLen) {
  if (!deviceId || !iv || deviceId[0] == '\0') return false;
  for (const char* p = deviceId; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
  }
  uint8_t keyRaw[32];
  size_t keyLen = 0;
  if (!base64Decode(kRegisterReqKey, strlen(kRegisterReqKey), keyRaw, sizeof(keyRaw), keyLen) || keyLen != kAesBlock)
    return false;
  uint8_t be[16] = {};
  for (const char* p = deviceId; *p; ++p) {
    unsigned carry = static_cast<unsigned>(*p - '0');
    for (int i = 15; i >= 0; --i) {
      const unsigned value = static_cast<unsigned>(be[i]) * 10u + carry;
      be[i] = static_cast<uint8_t>(value);
      carry = value >> 8;
    }
    if (carry) return false;
  }
  uint8_t reversed[16];
  for (int i = 0; i < 16; ++i) reversed[i] = be[15 - i];
  uint8_t cipher[64];
  size_t cipherLen = 0;
  if (!aes128CbcEncrypt(keyRaw, iv, reversed, sizeof(reversed), cipher, sizeof(cipher), cipherLen)) return false;
  uint8_t packed[80];
  memcpy(packed, iv, kAesBlock);
  memcpy(packed + kAesBlock, cipher, cipherLen);
  return base64Encode(packed, kAesBlock + cipherLen, out, outCap, outLen);
}

bool signHeaders(const SignMaterial& material, SignHeaders& headers) {
  headers = {};
  snprintf(headers.khronos, sizeof(headers.khronos), "%u", material.timestamp);
  snprintf(headers.ticket, sizeof(headers.ticket), "%llu",
           static_cast<unsigned long long>(material.timestamp) * 1000ULL);

  uint8_t bodyMd5[kMd5Len] = {};
  char stubHex[33] = {};
  if (material.body && material.bodyLen) {
    md5(material.body, material.bodyLen, bodyMd5);
    bytesToHex(bodyMd5, kMd5Len, stubHex, false);
    memcpy(headers.stub, stubHex, sizeof(headers.stub));
  }

  uint8_t queryHash[kSm3Len];
  uint8_t bodyHash[kSm3Len];
  if (material.query && material.query[0]) {
    sm3(reinterpret_cast<const uint8_t*>(material.query), strlen(material.query), queryHash);
  } else {
    uint8_t zeros[16] = {};
    sm3(zeros, sizeof(zeros), queryHash);
  }
  if (stubHex[0]) {
    uint8_t stubBytes[kMd5Len];
    if (!hexToBytes(stubHex, stubBytes, kMd5Len)) return false;
    sm3(stubBytes, kMd5Len, bodyHash);
  } else {
    uint8_t zeros[16] = {};
    sm3(zeros, sizeof(zeros), bodyHash);
  }

  char aidText[16];
  char licenseText[16];
  char versionName[64] = {};
  snprintf(aidText, sizeof(aidText), "%u", material.aid);
  snprintf(licenseText, sizeof(licenseText), "%u", material.licenseId);
  if (!queryParam(material.query, "version_name", versionName, sizeof(versionName))) return false;

  uint8_t bean[512];
  size_t beanLen = 0;
  uint8_t sub15[64];
  size_t sub15Len = 0;
  uint8_t sub23[64];
  size_t sub23Len = 0;
  uint8_t zeros8[8] = {};
  if (!writeVarintField(sub15, sizeof(sub15), sub15Len, 1, 1) ||
      !writeVarintField(sub15, sizeof(sub15), sub15Len, 2, 1) ||
      !writeVarintField(sub15, sizeof(sub15), sub15Len, 3, 1) ||
      !writeVarintField(sub15, sizeof(sub15), sub15Len, 7, 0xc792ecccu) ||
      !writeTextField(sub23, sizeof(sub23), sub23Len, 1, "NX551J") ||
      !writeVarintField(sub23, sizeof(sub23), sub23Len, 2, 8196) ||
      !writeVarintField(sub23, sizeof(sub23), sub23Len, 4, 0x80e0d800u))
    return false;
  if (!writeVarintField(bean, sizeof(bean), beanLen, 1, 1077940818u) ||
      !writeVarintField(bean, sizeof(bean), beanLen, 2, 2) ||
      !writeVarintField(bean, sizeof(bean), beanLen, 3, material.randField) ||
      !writeTextField(bean, sizeof(bean), beanLen, 4, aidText) ||
      !writeTextField(bean, sizeof(bean), beanLen, 5, material.deviceId ? material.deviceId : "") ||
      !writeTextField(bean, sizeof(bean), beanLen, 6, licenseText) ||
      !writeTextField(bean, sizeof(bean), beanLen, 7, versionName) ||
      !writeTextField(bean, sizeof(bean), beanLen, 8, "v04.04.05-ov-android") ||
      !writeVarintField(bean, sizeof(bean), beanLen, 9, 134744640u) ||
      !writeBytesField(bean, sizeof(bean), beanLen, 10, zeros8, sizeof(zeros8)) ||
      !writeVarintField(bean, sizeof(bean), beanLen, 11, 0) ||
      !writeVarintField(bean, sizeof(bean), beanLen, 12, material.timestamp * 2) ||
      !writeBytesField(bean, sizeof(bean), beanLen, 13, bodyHash, 6) ||
      !writeBytesField(bean, sizeof(bean), beanLen, 14, queryHash, 6) ||
      !writeBytesField(bean, sizeof(bean), beanLen, 15, sub15, sub15Len) ||
      !writeTextField(bean, sizeof(bean), beanLen, 16, material.installId ? material.installId : "") ||
      !writeTextField(bean, sizeof(bean), beanLen, 20, "none") ||
      !writeVarintField(bean, sizeof(bean), beanLen, 21, 738) ||
      !writeBytesField(bean, sizeof(bean), beanLen, 23, sub23, sub23Len) ||
      !writeVarintField(bean, sizeof(bean), beanLen, 25, 2))
    return false;

  const size_t pad = kAesBlock - (beanLen % kAesBlock);
  uint8_t padded[528];
  if (beanLen + pad > sizeof(padded)) return false;
  memcpy(padded, bean, beanLen);
  memset(padded + beanLen, static_cast<int>(pad), pad);
  const size_t paddedLen = beanLen + pad;

  uint8_t signKey[32];
  if (!hexToBytes(kArgusSignKey, signKey, sizeof(signKey))) return false;
  uint64_t words[4];
  for (int i = 0; i < 4; ++i) words[i] = readU64Le(signKey + i * 8);
  uint64_t roundKey[72];
  simonExpand(words, roundKey);
  uint8_t simonOut[528];
  for (size_t i = 0; i < paddedLen; i += 16) simonEncryptBlock(roundKey, padded + i, simonOut + i);

  uint8_t mixed[560];
  const size_t mixedLen = 8 + paddedLen + sizeof(kArgusTail);
  if (mixedLen > sizeof(mixed)) return false;
  memcpy(mixed, kArgusMagic, 8);
  memcpy(mixed + 8, simonOut, paddedLen);
  memcpy(mixed + 8 + paddedLen, kArgusTail, sizeof(kArgusTail));
  for (size_t i = 8; i < mixedLen; ++i) mixed[i] = static_cast<uint8_t>(mixed[i] ^ kArgusMagic[i % 8]);
  reverseBytes(mixed, mixedLen);

  uint8_t withTail[576];
  if (mixedLen + 2 > sizeof(withTail)) return false;
  memcpy(withTail, mixed, mixedLen);
  withTail[mixedLen] = 0x61;
  withTail[mixedLen + 1] = 0x6f;
  uint8_t aesKey[kAesBlock];
  uint8_t aesIv[kAesBlock];
  if (!hexToBytes(kArgusAesKey, aesKey, kAesBlock) || !hexToBytes(kArgusAesIv, aesIv, kAesBlock)) return false;
  uint8_t encrypted[592];
  size_t encryptedLen = 0;
  if (!aes128CbcEncrypt(aesKey, aesIv, withTail, mixedLen + 2, encrypted, sizeof(encrypted), encryptedLen))
    return false;
  uint8_t prefixed[600];
  if (encryptedLen + sizeof(kArgusPrefix) > sizeof(prefixed)) return false;
  memcpy(prefixed, kArgusPrefix, sizeof(kArgusPrefix));
  memcpy(prefixed + sizeof(kArgusPrefix), encrypted, encryptedLen);
  size_t argusLen = 0;
  if (!base64Encode(prefixed, encryptedLen + sizeof(kArgusPrefix), headers.argus, sizeof(headers.argus), argusLen))
    return false;

  char keygen[16];
  memcpy(keygen, material.ladonRandom, 4);
  const size_t aidLen = strlen(aidText);
  memcpy(keygen + 4, aidText, aidLen);
  uint8_t keyMd5[kMd5Len];
  md5(reinterpret_cast<const uint8_t*>(keygen), 4 + aidLen, keyMd5);
  char keyHex[33];
  bytesToHex(keyMd5, kMd5Len, keyHex, false);
  uint8_t table[280];
  ladonExpand(keyHex, table);
  char plain[64];
  snprintf(plain, sizeof(plain), "%u-%u-%u", material.timestamp, material.licenseId, material.aid);
  const size_t plainLen = strlen(plain);
  const size_t ladonPad = kAesBlock - (plainLen % kAesBlock);
  uint8_t ladonIn[80];
  if (plainLen + ladonPad > sizeof(ladonIn)) return false;
  memcpy(ladonIn, plain, plainLen);
  memset(ladonIn + plainLen, static_cast<int>(ladonPad), ladonPad);
  uint8_t ladonOut[80];
  for (size_t i = 0; i < plainLen + ladonPad; i += 16) ladonEncryptBlock(table, ladonIn + i, ladonOut + i);
  uint8_t ladonPacked[84];
  memcpy(ladonPacked, material.ladonRandom, 4);
  memcpy(ladonPacked + 4, ladonOut, plainLen + ladonPad);
  size_t ladonLen = 0;
  return base64Encode(ladonPacked, 4 + plainLen + ladonPad, headers.ladon, sizeof(headers.ladon), ladonLen);
}

}  // namespace FanqieCrypto

#endif
