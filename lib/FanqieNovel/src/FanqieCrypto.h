#pragma once

#include <cstddef>
#include <cstdint>

// Byte helpers for the Fanqie web login cookies and the app-gateway chapter
// cipher. Randomness is supplied by the caller so host tests stay deterministic.
// Nothing here logs or stores account secrets.

namespace FanqieCrypto {

constexpr size_t kSm3Len = 32;
constexpr size_t kMd5Len = 16;
constexpr size_t kAesBlock = 16;

struct SignMaterial {
  const char* query = nullptr;
  const uint8_t* body = nullptr;
  size_t bodyLen = 0;
  uint32_t timestamp = 0;
  const char* deviceId = nullptr;
  const char* installId = nullptr;
  uint32_t randField = 0;
  uint8_t ladonRandom[4] = {};
  uint32_t aid = 1967;
  uint32_t licenseId = 1611921764;
};

struct SignHeaders {
  char argus[768] = {};
  char ladon[384] = {};
  char khronos[16] = {};
  char ticket[24] = {};
  char stub[33] = {};
};

bool base64Decode(const char* in, size_t inLen, uint8_t* out, size_t outCap, size_t& outLen);
bool base64Encode(const uint8_t* in, size_t inLen, char* out, size_t outCap, size_t& outLen);

bool aes128CbcEncrypt(const uint8_t key[kAesBlock], const uint8_t iv[kAesBlock], const uint8_t* plain, size_t plainLen,
                      uint8_t* out, size_t outCap, size_t& outLen);
bool aes128CbcDecrypt(const uint8_t key[kAesBlock], const uint8_t iv[kAesBlock], const uint8_t* cipher, size_t cipherLen,
                      uint8_t* out, size_t outCap, size_t& outLen);

void sm3(const uint8_t* data, size_t len, uint8_t out[kSm3Len]);
void md5(const uint8_t* data, size_t len, uint8_t out[kMd5Len]);

bool gzipStore(const uint8_t* in, size_t inLen, uint8_t* out, size_t outCap, size_t& outLen);
bool gunzip(const uint8_t* in, size_t inLen, uint8_t* out, size_t outCap, size_t& outLen);

// App-gateway chapter body: base64(iv || aes-128-cbc), then optional gzip.
bool decryptContent(const char* base64, size_t base64Len, const char* keyHex, uint8_t* out, size_t outCap,
                    size_t& outLen);
bool decryptRegisterKey(const char* encryptedKeyBase64, uint8_t* out, size_t outCap, size_t& outLen);
bool encryptRegisterKeyRequest(const char* deviceId, const uint8_t iv[kAesBlock], char* out, size_t outCap,
                               size_t& outLen);

bool signHeaders(const SignMaterial& material, SignHeaders& headers);

}  // namespace FanqieCrypto
