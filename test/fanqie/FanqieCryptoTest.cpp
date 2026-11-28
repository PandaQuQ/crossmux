#include "FanqieCrypto.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace {

std::string hexOf(const uint8_t* data, size_t len) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out(len * 2, '\0');
  for (size_t i = 0; i < len; ++i) {
    out[i * 2] = kDigits[data[i] >> 4];
    out[i * 2 + 1] = kDigits[data[i] & 0x0f];
  }
  return out;
}

}  // namespace

TEST(FanqieCrypto, Md5AndSm3KnownAnswers) {
  const uint8_t abc[] = {'a', 'b', 'c'};
  uint8_t md[16];
  FanqieCrypto::md5(abc, 3, md);
  EXPECT_EQ(hexOf(md, 16), "900150983cd24fb0d6963f7d28e17f72");
  uint8_t sm[32];
  FanqieCrypto::sm3(abc, 3, sm);
  EXPECT_EQ(hexOf(sm, 32), "66c7f0f462eeedd9d1f2d46bdc10e4e24167c4875cf2f7a2297da02b8f4ba8e0");
}

TEST(FanqieCrypto, Aes128CbcNistBlockRoundTrip) {
  const uint8_t key[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
  const uint8_t iv[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
  const uint8_t plain[16] = {0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a};
  uint8_t cipher[32];
  size_t cipherLen = 0;
  ASSERT_TRUE(FanqieCrypto::aes128CbcEncrypt(key, iv, plain, sizeof(plain), cipher, sizeof(cipher), cipherLen));
  EXPECT_EQ(cipherLen, 32u);
  EXPECT_EQ(hexOf(cipher, 16), "7649abac8119b246cee98e9b12e9197d");
  uint8_t out[32];
  size_t outLen = 0;
  ASSERT_TRUE(FanqieCrypto::aes128CbcDecrypt(key, iv, cipher, cipherLen, out, sizeof(out), outLen));
  ASSERT_EQ(outLen, sizeof(plain));
  EXPECT_EQ(0, memcmp(out, plain, sizeof(plain)));
}

TEST(FanqieCrypto, GzipRoundTripAndChapterDecrypt) {
  const char* text = "fanqie chapter";
  uint8_t gz[128];
  size_t gzLen = 0;
  ASSERT_TRUE(FanqieCrypto::gzipStore(reinterpret_cast<const uint8_t*>(text), strlen(text), gz, sizeof(gz), gzLen));
  uint8_t plain[128];
  size_t plainLen = 0;
  ASSERT_TRUE(FanqieCrypto::gunzip(gz, gzLen, plain, sizeof(plain), plainLen));
  ASSERT_EQ(plainLen, strlen(text));
  EXPECT_EQ(0, memcmp(plain, text, plainLen));

  const uint8_t key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
  const uint8_t iv[16] = {};
  uint8_t cipher[64];
  size_t cipherLen = 0;
  ASSERT_TRUE(FanqieCrypto::aes128CbcEncrypt(key, iv, reinterpret_cast<const uint8_t*>(text), strlen(text), cipher,
                                             sizeof(cipher), cipherLen));
  uint8_t packed[80];
  memcpy(packed, iv, 16);
  memcpy(packed + 16, cipher, cipherLen);
  char b64[160];
  size_t b64Len = 0;
  ASSERT_TRUE(FanqieCrypto::base64Encode(packed, 16 + cipherLen, b64, sizeof(b64), b64Len));
  uint8_t decoded[64];
  size_t decodedLen = 0;
  ASSERT_TRUE(FanqieCrypto::decryptContent(b64, b64Len, "000102030405060708090a0b0c0d0e0f", decoded, sizeof(decoded),
                                           decodedLen));
  ASSERT_EQ(decodedLen, strlen(text));
  EXPECT_EQ(0, memcmp(decoded, text, decodedLen));
}

TEST(FanqieCrypto, RegisterKeyRequestIsStableForFixedIv) {
  const uint8_t iv[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  char out[128];
  size_t outLen = 0;
  ASSERT_TRUE(FanqieCrypto::encryptRegisterKeyRequest("255", iv, out, sizeof(out), outLen));
  EXPECT_GT(outLen, 16u);
  char again[128];
  size_t againLen = 0;
  ASSERT_TRUE(FanqieCrypto::encryptRegisterKeyRequest("255", iv, again, sizeof(again), againLen));
  EXPECT_STREQ(out, again);
}

TEST(FanqieCrypto, SignHeadersAreDeterministicAndWellFormed) {
  FanqieCrypto::SignMaterial material;
  material.query = "aid=1967&item_id=42&version_name=6.1.5.32";
  const uint8_t body[] = {'{', '}'};
  material.body = body;
  material.bodyLen = sizeof(body);
  material.timestamp = 1700000000;
  material.deviceId = "7123456789012345678";
  material.installId = "0";
  material.randField = 0x11223344;
  material.ladonRandom[0] = 1;
  material.ladonRandom[1] = 2;
  material.ladonRandom[2] = 3;
  material.ladonRandom[3] = 4;
  FanqieCrypto::SignHeaders headers;
  ASSERT_TRUE(FanqieCrypto::signHeaders(material, headers));
  EXPECT_STREQ(headers.khronos, "1700000000");
  EXPECT_STREQ(headers.ticket, "1700000000000");
  EXPECT_EQ(strlen(headers.stub), 32u);
  EXPECT_STREQ(headers.stub, "99914b932bd37a50b983c5e7c90ae93b");
  EXPECT_STREQ(headers.argus,
               "8oHyrVYEy1xDCDlx64ko40qC6JC9VYY/ZkjJzEl50iSFDw8Yfph1hupLxfKrA9GswSEwl2CYOIedF/"
               "063nVe5qyocfjrpwwlgWJNKB9gx4QYC6B2xGPCOKn5pmHyPbi5k8O+utaiIzTOI1WxQ7niFcrrAcLQ9BKifI7BHGZjUXsmlIw2uQBQYb6"
               "OsXSL0WtC/5M1DEBAN8LU6Q+I0mNgsNmMQhmiWLIJ96dfEmZBagBUiJOjthea8MuE0HkIsNv2a6+iBzeTMcYX4aJHWhMAuZSFfvHtOXx"
               "kZLYzOUoyczhGiQ==");
  EXPECT_STREQ(headers.ladon, "AQIDBKM7Y7bWnOIUXmTybD6/MpGbuge4sBwUJsyHVivhtMsH");
  FanqieCrypto::SignHeaders again;
  ASSERT_TRUE(FanqieCrypto::signHeaders(material, again));
  EXPECT_STREQ(headers.argus, again.argus);
  EXPECT_STREQ(headers.ladon, again.ladon);
  EXPECT_STREQ(headers.stub, again.stub);
}
