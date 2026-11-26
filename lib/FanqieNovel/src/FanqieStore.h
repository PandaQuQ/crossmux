#pragma once

#include <cstddef>
#include <cstdint>

namespace FanqieStore {

constexpr char kRoot[] = "/.crosspoint/fanqie";
constexpr uint32_t kAccountMagic = 0x31435146;  // FQC1
constexpr uint16_t kAccountVersion = 1;
constexpr uint32_t kChapterMagic = 0x48435146;  // FQCH
constexpr uint16_t kChapterVersion = 1;
constexpr uint32_t kMetaMagic = 0x4D425146;  // FQBM
constexpr uint16_t kMetaVersion = 1;
constexpr uint32_t kIndexMagic = 0x58495146;  // FQIX
constexpr uint16_t kIndexVersion = 1;
constexpr int kMaxAccounts = 4;
constexpr int kMaxBooks = 32;
constexpr int kNickname = 48;
constexpr int kCookie = 1536;

struct Account {
  char nickname[kNickname] = {};
  char cookie[kCookie] = {};
  bool used() const { return cookie[0] != '\0'; }
};

struct ChapterRecord {
  char itemId[24] = {};
  char title[80] = {};
};
static_assert(sizeof(ChapterRecord) == 104, "chapter record layout");

struct BookMeta {
  char bookId[24] = {};
  char title[96] = {};
  char author[64] = {};
  char epubPath[96] = {};
  uint32_t chapterCount = 0;
  char cloudItem[24] = {};
  uint32_t cloudUnix = 0;
  uint32_t localUnix = 0;
  uint8_t dirty = 0;
  uint16_t spine = 0;
  uint16_t page = 0;
  uint16_t pageCount = 0;
};

bool ensureRoot();
bool loadAccounts(Account* accounts, int& count, int& active);
bool saveAccounts(const Account* accounts, int count, int active);
bool loadActiveAccount(Account& account);

bool saveChapters(const char* bookId, const ChapterRecord* records, uint32_t count);
bool readChapter(const char* bookId, uint32_t index, ChapterRecord& record);
bool chapterCount(const char* bookId, uint32_t& count);
bool chapterTextPath(const char* bookId, uint32_t index, char* out, size_t outCap);
bool hasChapterText(const char* bookId, uint32_t index);

bool loadMeta(const char* bookId, BookMeta& meta);
bool saveMeta(const BookMeta& meta);
bool findByEpubPath(const char* epubPath, BookMeta& meta);
bool epubPathFor(const char* bookId, char* out, size_t outCap);

bool loadContentKey(char* hex, size_t cap);
bool saveContentKey(const char* hex);
bool loadDeviceId(char* out, size_t cap);
bool saveDeviceId(const char* id);

}  // namespace FanqieStore
