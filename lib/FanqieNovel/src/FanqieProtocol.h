#pragma once

#include <cstddef>
#include <cstdint>

// Parsers and URL builders for the Fanqie web passport and reader endpoints.
// Book and chapter ids stay strings: they do not fit in a 53-bit JSON number.

namespace FanqieProtocol {

constexpr int kMaxId = 24;
constexpr int kMaxTitle = 96;
constexpr int kMaxAuthor = 64;
constexpr int kMaxCookie = 1536;
constexpr char kWebOrigin[] = "https://fanqienovel.com";

enum class LoginPoll : uint8_t { Waiting, Scanned, Expired, Cancelled, Failed };

enum class ChapterKind : uint8_t { Empty, Text, Cipher, Obfuscated };

enum class ProgressChoice : uint8_t { KeepLocal, ApplyCloud };

struct ProgressRecord {
  char bookId[kMaxId] = {};
  char itemId[kMaxId] = {};
  int index = 0;
  double rawProgress = 0;
  uint32_t readUnix = 0;
  bool tenThousand = false;
};

struct ShelfBook {
  char bookId[kMaxId] = {};
  char title[kMaxTitle] = {};
  char author[kMaxAuthor] = {};
  uint32_t chapterCount = 0;
  char itemId[kMaxId] = {};
};

struct ChapterRef {
  char itemId[kMaxId] = {};
  char title[kMaxTitle] = {};
};

struct CookieJar {
  char header[kMaxCookie] = {};

  void clear();
  bool has(const char* name) const;
  bool set(const char* name, const char* value);
  bool absorbSetCookie(const char* line);
};

using ChapterFn = bool (*)(void* ctx, const ChapterRef& chapter);
using ProgressFn = bool (*)(void* ctx, const ProgressRecord& record);
using BookIdFn = bool (*)(void* ctx, const char* bookId);

bool parseQrStart(const char* json, size_t len, char* token, size_t tokenCap, uint8_t* png, size_t pngCap,
                  size_t& pngLen, uint32_t& expireUnix);
LoginPoll parseLoginPoll(const char* json, size_t len);
bool parseProgress(const char* json, size_t len, ProgressFn fn, void* ctx);
bool parseShelfIds(const char* json, size_t len, BookIdFn fn, void* ctx);
bool parseBookDetails(const char* json, size_t len, ShelfBook* books, int cap, int& count);
bool parseChapters(const char* json, size_t len, ChapterFn fn, void* ctx);
ChapterKind classifyChapter(const char* content, size_t len);
bool stripChapterText(const char* content, size_t len, char* out, size_t outCap, size_t& outLen);
bool findChapterTitle(const char* json, size_t len, char* title, size_t titleCap);
bool findJsonString(const char* json, size_t len, const char* key, char* out, size_t outCap);

bool progressUpdateBody(const char* bookId, const char* itemId, int index, double fraction, uint32_t unixTime,
                        char* out, size_t outCap);
bool multidetailBody(const ShelfBook* books, int count, char* out, size_t outCap);
int shelfPercent(int chapterIndex, int chapterCount, double rawProgress, bool tenThousand);
ProgressChoice chooseProgress(bool dirty, uint32_t localUnix, uint32_t cloudUnix);
int chapterIndexForItem(const char* itemId, const ChapterRef* chapters, int count);

bool qrStartUrl(char* out, size_t outCap);
bool qrPollUrl(const char* token, char* out, size_t outCap);
bool progressUrl(char* out, size_t outCap);
bool updateProgressUrl(char* out, size_t outCap);
bool shelfUrl(char* out, size_t outCap);
bool multidetailUrl(char* out, size_t outCap);
bool directoryUrl(const char* bookId, char* out, size_t outCap);
bool webChapterUrl(const char* bookId, const char* itemId, char* out, size_t outCap);
bool loginPageUrl(char* out, size_t outCap);
bool appChapterQuery(const char* deviceId, const char* itemId, char* out, size_t outCap);
bool appChapterUrl(const char* query, char* out, size_t outCap);
bool registerKeyQuery(const char* deviceId, char* out, size_t outCap);
bool registerKeyUrl(const char* query, char* out, size_t outCap);

}  // namespace FanqieProtocol
