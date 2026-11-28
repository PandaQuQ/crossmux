#include "FanqieProtocol.h"

#if !FANQIE_NOVEL

namespace fanqie_internal {
constexpr int kProtocolOmitted = 0;
}

#else

#include <cstdio>
#include <cstring>

#include "FanqieCrypto.h"

namespace FanqieProtocol {
namespace {

bool copyText(char* out, size_t cap, const char* begin, size_t len) {
  if (!out || cap == 0 || len >= cap) return false;
  memcpy(out, begin, len);
  out[len] = '\0';
  return true;
}

void appendJsonString(char* out, size_t cap, size_t& used, const char* text) {
  auto put = [&](char c) {
    if (used + 1 < cap) out[used++] = c;
  };
  put('"');
  for (const char* p = text ? text : ""; *p; ++p) {
    if (*p == '"' || *p == '\\') put('\\');
    put(*p);
  }
  put('"');
}

const char* skipWs(const char* p, const char* end) {
  while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
  return p;
}

bool keyAt(const char* json, const char* end, const char* key) {
  const size_t n = strlen(key);
  return json + n <= end && memcmp(json, key, n) == 0 && (json + n == end || json[n] == '"');
}

bool readJsonString(const char* p, const char* end, const char*& value, size_t& valueLen, const char*& after) {
  p = skipWs(p, end);
  if (p >= end || *p != '"') return false;
  ++p;
  value = p;
  while (p < end && *p != '"') {
    if (*p == '\\' && p + 1 < end)
      p += 2;
    else
      ++p;
  }
  if (p >= end) return false;
  valueLen = static_cast<size_t>(p - value);
  after = p + 1;
  return true;
}

bool unescape(const char* in, size_t len, char* out, size_t cap) {
  size_t o = 0;
  for (size_t i = 0; i < len; ++i) {
    char c = in[i];
    if (c == '\\' && i + 1 < len) {
      const char n = in[++i];
      if (n == 'n')
        c = '\n';
      else if (n == 't')
        c = '\t';
      else
        c = n;
    }
    if (o + 1 >= cap) return false;
    out[o++] = c;
  }
  out[o] = '\0';
  return true;
}

bool findKey(const char* json, size_t len, const char* key, size_t from, const char*& value, size_t& valueLen,
             size_t& after) {
  const char* begin = json + (from < len ? from : len);
  const char* end = json + len;
  const size_t keyLen = strlen(key);
  for (const char* p = begin; p + keyLen + 2 < end; ++p) {
    if (*p != '"') continue;
    if (!keyAt(p + 1, end, key)) continue;
    const char* cursor = skipWs(p + 1 + keyLen + 1, end);
    if (cursor >= end || *cursor != ':') continue;
    const char* raw = nullptr;
    size_t rawLen = 0;
    const char* next = nullptr;
    if (!readJsonString(cursor + 1, end, raw, rawLen, next)) continue;
    value = raw;
    valueLen = rawLen;
    after = static_cast<size_t>(next - json);
    return true;
  }
  return false;
}

bool readNumber(const char* p, const char* end, double& number, uint32_t& asUint, bool& isUint, const char*& after) {
  p = skipWs(p, end);
  if (p >= end) return false;
  char buf[48];
  size_t n = 0;
  const char* start = p;
  if (*p == '-') {
    buf[n++] = *p++;
  }
  if (p >= end || *p < '0' || *p > '9') return false;
  while (p < end && n + 1 < sizeof(buf) && ((*p >= '0' && *p <= '9') || *p == '.')) buf[n++] = *p++;
  buf[n] = '\0';
  number = 0;
  isUint = true;
  asUint = 0;
  for (size_t i = 0; buf[i]; ++i) {
    if (buf[i] == '.') {
      isUint = false;
      break;
    }
    if (buf[i] < '0' || buf[i] > '9') continue;
    if (asUint > (UINT32_MAX - static_cast<uint32_t>(buf[i] - '0')) / 10)
      isUint = false;
    else
      asUint = asUint * 10 + static_cast<uint32_t>(buf[i] - '0');
  }
  // Small decimal parser, enough for progress fractions and unix times.
  double sign = buf[0] == '-' ? -1 : 1;
  double whole = 0;
  double frac = 0;
  double place = 1;
  bool dot = false;
  for (size_t i = buf[0] == '-' ? 1 : 0; buf[i]; ++i) {
    if (buf[i] == '.') {
      dot = true;
      continue;
    }
    if (!dot)
      whole = whole * 10 + (buf[i] - '0');
    else {
      place *= 10;
      frac = frac * 10 + (buf[i] - '0');
    }
  }
  number = sign * (whole + (place > 1 ? frac / place : 0));
  after = p > start ? p : start + 1;
  return true;
}

bool findNumberAfter(const char* json, size_t len, const char* key, size_t from, size_t until, double& number,
                     uint32_t& asUint, bool& isUint) {
  const char* end = json + (until < len ? until : len);
  const char* begin = json + from;
  const size_t keyLen = strlen(key);
  for (const char* p = begin; p + keyLen + 2 < end; ++p) {
    if (*p != '"' || !keyAt(p + 1, end, key)) continue;
    const char* cursor = skipWs(p + 2 + keyLen, end);
    if (cursor < end && *cursor == '"') ++cursor;  // tolerate a quoted number
    if (cursor >= end || *cursor != ':') continue;
    const char* after = nullptr;
    const char* valueStart = skipWs(cursor + 1, end);
    if (valueStart < end && *valueStart == '"') {
      const char* raw = nullptr;
      size_t rawLen = 0;
      if (!readJsonString(valueStart, end, raw, rawLen, after)) return false;
      char tmp[48];
      if (!copyText(tmp, sizeof(tmp), raw, rawLen)) return false;
      const char* ignored = nullptr;
      return readNumber(tmp, tmp + strlen(tmp), number, asUint, isUint, ignored);
    }
    return readNumber(cursor + 1, end, number, asUint, isUint, after);
  }
  return false;
}

bool appendUrl(char* out, size_t cap, const char* a, const char* b = nullptr, const char* c = nullptr) {
  const size_t na = strlen(a);
  const size_t nb = b ? strlen(b) : 0;
  const size_t nc = c ? strlen(c) : 0;
  if (na + nb + nc + 1 > cap) return false;
  memcpy(out, a, na);
  if (nb) memcpy(out + na, b, nb);
  if (nc) memcpy(out + na + nb, c, nc);
  out[na + nb + nc] = '\0';
  return true;
}

bool idOk(const char* id) {
  if (!id || !id[0] || strlen(id) >= kMaxId) return false;
  for (const char* p = id; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
  }
  return true;
}

}  // namespace

void CookieJar::clear() { header[0] = '\0'; }

bool CookieJar::has(const char* name) const {
  if (!name || !name[0]) return false;
  const size_t n = strlen(name);
  const char* p = header;
  while (*p) {
    if (strncmp(p, name, n) == 0 && p[n] == '=') return true;
    const char* semi = strchr(p, ';');
    if (!semi) return false;
    p = semi + 1;
    if (*p == ' ') ++p;
  }
  return false;
}

bool CookieJar::set(const char* name, const char* value) {
  if (!name || !name[0] || strchr(name, '=') || strchr(name, ';')) return false;
  char next[kMaxCookie];
  size_t used = 0;
  const char* p = header;
  bool replaced = false;
  while (*p) {
    const char* semi = strchr(p, ';');
    const char* end = semi ? semi : p + strlen(p);
    const char* eq = static_cast<const char*>(memchr(p, '=', static_cast<size_t>(end - p)));
    const bool same = eq && static_cast<size_t>(eq - p) == strlen(name) && memcmp(p, name, strlen(name)) == 0;
    if (!same) {
      if (used && used + 2 < sizeof(next)) {
        next[used++] = ';';
        next[used++] = ' ';
      }
      const size_t n = static_cast<size_t>(end - p);
      if (used + n >= sizeof(next)) return false;
      memcpy(next + used, p, n);
      used += n;
    }
    if (!semi) break;
    p = semi + 1;
    if (*p == ' ') ++p;
  }
  if (value && value[0]) {
    if (used && used + 2 < sizeof(next)) {
      next[used++] = ';';
      next[used++] = ' ';
    }
    const size_t nn = strlen(name);
    const size_t nv = strlen(value);
    if (used + nn + 1 + nv >= sizeof(next)) return false;
    memcpy(next + used, name, nn);
    used += nn;
    next[used++] = '=';
    memcpy(next + used, value, nv);
    used += nv;
    replaced = true;
  }
  next[used] = '\0';
  memcpy(header, next, used + 1);
  return replaced || !value || !value[0];
}

bool CookieJar::absorbSetCookie(const char* line) {
  if (!line) return false;
  const char* domain = strstr(line, "domain=");
  if (!domain) domain = strstr(line, "Domain=");
  if (domain) {
    domain += 7;
    if (*domain == '.') ++domain;
    if (strncmp(domain, "fanqienovel.com", 15) != 0) return true;
  }
  const char* eq = strchr(line, '=');
  if (!eq || eq == line) return false;
  char name[64];
  if (!copyText(name, sizeof(name), line, static_cast<size_t>(eq - line))) return false;
  const char* semi = strchr(eq + 1, ';');
  const size_t valueLen = semi ? static_cast<size_t>(semi - (eq + 1)) : strlen(eq + 1);
  char value[512];
  if (valueLen >= sizeof(value)) return false;
  memcpy(value, eq + 1, valueLen);
  value[valueLen] = '\0';
  const bool clear = value[0] == '\0' || strstr(line, "Max-Age=0") || strstr(line, "max-age=0");
  return set(name, clear ? "" : value);
}

bool parseQrStart(const char* json, size_t len, char* token, size_t tokenCap, uint8_t* png, size_t pngCap,
                  size_t& pngLen, uint32_t& expireUnix) {
  pngLen = 0;
  expireUnix = 0;
  if (token && tokenCap) token[0] = '\0';
  if (!json || !strstr(json, "\"success\"")) return false;
  const char* raw = nullptr;
  size_t rawLen = 0;
  size_t after = 0;
  if (!findKey(json, len, "token", 0, raw, rawLen, after) || !unescape(raw, rawLen, token, tokenCap)) return false;
  if (!findKey(json, len, "qrcode", 0, raw, rawLen, after)) return false;
  if (png && pngCap && !FanqieCrypto::base64Decode(raw, rawLen, png, pngCap, pngLen)) return false;
  double number = 0;
  uint32_t asUint = 0;
  bool isUint = false;
  if (findNumberAfter(json, len, "expire_time", 0, len, number, asUint, isUint) && isUint) expireUnix = asUint;
  return token[0] != '\0';
}

bool copyLoginHint(const char* json, size_t len, char* out, size_t cap) {
  if (!out || cap < 2) return false;
  out[0] = '\0';
  if (!json || !len) return false;
  if (findJsonString(json, len, "description", out, cap) && out[0]) return true;
  if (findJsonString(json, len, "message", out, cap) && out[0] && strcmp(out, "success") != 0) return true;
  out[0] = '\0';
  return false;
}

LoginPoll parseLoginPoll(const char* json, size_t len) {
  if (!json || !len) return LoginPoll::Failed;
  char status[32];
  if (!findJsonString(json, len, "status", status, sizeof(status))) return LoginPoll::Waiting;
  if (strcmp(status, "scanned") == 0 || strcmp(status, "confirm") == 0 || strcmp(status, "confirming") == 0)
    return LoginPoll::Scanned;
  if (strcmp(status, "expired") == 0) return LoginPoll::Expired;
  if (strcmp(status, "cancelled") == 0 || strcmp(status, "canceled") == 0) return LoginPoll::Cancelled;
  return LoginPoll::Waiting;
}

bool findJsonString(const char* json, size_t len, const char* key, char* out, size_t outCap) {
  const char* raw = nullptr;
  size_t rawLen = 0;
  size_t after = 0;
  if (!json || !findKey(json, len, key, 0, raw, rawLen, after)) return false;
  return unescape(raw, rawLen, out, outCap);
}

bool parseProgress(const char* json, size_t len, ProgressFn fn, void* ctx) {
  if (!json || !fn) return false;
  size_t from = 0;
  while (from < len) {
    const char* raw = nullptr;
    size_t rawLen = 0;
    size_t after = 0;
    if (!findKey(json, len, "book_id", from, raw, rawLen, after)) break;
    ProgressRecord record;
    if (!unescape(raw, rawLen, record.bookId, sizeof(record.bookId)) || !idOk(record.bookId)) {
      from = after;
      continue;
    }
    const size_t window = after + 480 < len ? after + 480 : len;
    if (findKey(json, window, "item_id", after, raw, rawLen, after)) {
      unescape(raw, rawLen, record.itemId, sizeof(record.itemId));
    }
    double number = 0;
    uint32_t asUint = 0;
    bool isUint = false;
    if (findNumberAfter(json, len, "index", after > 64 ? after - 64 : 0, window, number, asUint, isUint))
      record.index = static_cast<int>(number);
    if (findNumberAfter(json, len, "read_progress", after > 64 ? after - 64 : 0, window, number, asUint, isUint)) {
      record.rawProgress = number;
      record.tenThousand = number > 1.0;
    }
    if (findNumberAfter(json, len, "read_timestamp", after > 64 ? after - 64 : 0, window, number, asUint, isUint) &&
        isUint)
      record.readUnix = asUint;
    if (!fn(ctx, record)) return false;
    from = after;
  }
  return true;
}

bool parseShelfIds(const char* json, size_t len, BookIdFn fn, void* ctx) {
  if (!json || !fn) return false;
  const char* marker = strstr(json, "book_shelf_info");
  if (!marker) return false;
  size_t from = static_cast<size_t>(marker - json);
  int depth = 0;
  bool inArray = false;
  for (size_t i = from; i < len; ++i) {
    if (json[i] == '[') {
      ++depth;
      inArray = true;
    } else if (json[i] == ']') {
      if (depth == 0 || --depth == 0) break;
    }
  }
  const size_t end = from;
  (void)end;
  size_t cursor = from;
  const size_t limit = len;
  while (cursor < limit) {
    const char* raw = nullptr;
    size_t rawLen = 0;
    size_t after = 0;
    if (!findKey(json, inArray ? len : len, "book_id", cursor, raw, rawLen, after)) break;
    if (inArray && after > from && json[after] == '\0') break;
    char id[kMaxId];
    if (unescape(raw, rawLen, id, sizeof(id)) && idOk(id)) {
      if (!fn(ctx, id)) return false;
    }
    cursor = after;
    if (inArray) {
      // Stop once we pass the shelf array. A later book_id belongs to another object.
      const char* close = static_cast<const char*>(memchr(json + from, ']', len - from));
      if (close && cursor > static_cast<size_t>(close - json)) break;
    }
  }
  return true;
}

bool parseBookDetails(const char* json, size_t len, ShelfBook* books, int cap, int& count) {
  count = 0;
  if (!json || !books || cap <= 0) return false;
  size_t from = 0;
  while (count < cap) {
    const char* raw = nullptr;
    size_t rawLen = 0;
    size_t after = 0;
    if (!findKey(json, len, "book_id", from, raw, rawLen, after)) break;
    ShelfBook book;
    if (!unescape(raw, rawLen, book.bookId, sizeof(book.bookId)) || !idOk(book.bookId)) {
      from = after;
      continue;
    }
    const size_t window = after + 800 < len ? after + 800 : len;
    if (findKey(json, window, "book_name", after, raw, rawLen, after))
      unescape(raw, rawLen, book.title, sizeof(book.title));
    size_t nameAfter = after;
    if (findKey(json, window, "author", after > 32 ? after - 32 : after, raw, rawLen, nameAfter))
      unescape(raw, rawLen, book.author, sizeof(book.author));
    double number = 0;
    uint32_t asUint = 0;
    bool isUint = false;
    if (findNumberAfter(json, len, "serial_count", after > 32 ? after - 32 : after, window, number, asUint, isUint))
      book.chapterCount = static_cast<uint32_t>(number);
    books[count++] = book;
    from = after;
  }
  return true;
}

bool parseChapters(const char* json, size_t len, ChapterFn fn, void* ctx) {
  if (!json || !fn) return false;
  size_t from = 0;
  while (from < len) {
    const char* raw = nullptr;
    size_t rawLen = 0;
    size_t after = 0;
    if (!findKey(json, len, "itemId", from, raw, rawLen, after)) break;
    ChapterRef chapter;
    if (!unescape(raw, rawLen, chapter.itemId, sizeof(chapter.itemId)) || !idOk(chapter.itemId)) {
      from = after;
      continue;
    }
    size_t objectFrom = after;
    while (objectFrom > 0 && json[objectFrom] != '{') --objectFrom;
    size_t objectTo = after;
    int depth = 0;
    for (; objectTo < len; ++objectTo) {
      if (json[objectTo] == '{')
        ++depth;
      else if (json[objectTo] == '}') {
        if (depth == 0) {
          ++objectTo;
          break;
        }
        --depth;
      }
    }
    size_t titleAfter = 0;
    if (findKey(json, objectTo, "title", objectFrom, raw, rawLen, titleAfter))
      unescape(raw, rawLen, chapter.title, sizeof(chapter.title));
    if (!fn(ctx, chapter)) return false;
    from = after;
  }
  return true;
}

ChapterKind classifyChapter(const char* content, size_t len) {
  if (!content || len == 0) return ChapterKind::Empty;
  bool pua = false;
  bool markup = false;
  bool text = false;
  for (size_t i = 0; i + 2 < len; ++i) {
    const auto b0 = static_cast<uint8_t>(content[i]);
    const auto b1 = static_cast<uint8_t>(content[i + 1]);
    const auto b2 = static_cast<uint8_t>(content[i + 2]);
    if (b0 == 0xee && b1 >= 0x80 && b1 <= 0xbf) pua = true;
    if (b0 == 0xef && b1 >= 0x80 && b1 <= 0xa3 && b2 >= 0x80) pua = true;
    if (content[i] == '<') markup = true;
    if ((b0 & 0x80) == 0 && content[i] > ' ') text = true;
  }
  if (pua) return ChapterKind::Obfuscated;
  if (len < 20) return ChapterKind::Empty;
  if (markup || text) return ChapterKind::Text;
  return ChapterKind::Cipher;
}

bool stripChapterText(const char* content, size_t len, char* out, size_t outCap, size_t& outLen) {
  outLen = 0;
  if (!content || !out || outCap < 2) return false;
  bool tag = false;
  for (size_t i = 0; i < len; ++i) {
    const char c = content[i];
    if (c == '<') {
      tag = true;
      if (outLen && out[outLen - 1] != '\n') {
        if (outLen + 1 >= outCap) break;
        out[outLen++] = '\n';
      }
      continue;
    }
    if (c == '>') {
      tag = false;
      continue;
    }
    if (tag) continue;
    if (c == '\r') continue;
    if (outLen + 1 >= outCap) break;
    out[outLen++] = c;
  }
  out[outLen] = '\0';
  return outLen > 0;
}

bool findChapterTitle(const char* json, size_t len, char* title, size_t titleCap) {
  return findJsonString(json, len, "title", title, titleCap);
}

bool progressUpdateBody(const char* bookId, const char* itemId, int index, double fraction, uint32_t unixTime,
                        char* out, size_t outCap) {
  if (!idOk(bookId) || !idOk(itemId) || !out) return false;
  if (fraction < 0) fraction = 0;
  if (fraction > 1) fraction = 1;
  if (index < 0) index = 0;
  const int milli = static_cast<int>(fraction * 10000 + 0.5);
  const int written = snprintf(out, outCap,
                               "{\"book_id\":\"%s\",\"item_id\":\"%s\",\"read_progress\":%d.%04d,\"index\":%d,"
                               "\"read_timestamp\":\"%u\",\"genre_type\":0}",
                               bookId, itemId, milli / 10000, milli % 10000, index, unixTime);
  return written > 0 && static_cast<size_t>(written) < outCap;
}

bool multidetailBody(const ShelfBook* books, int count, char* out, size_t outCap) {
  if (!out || count < 0) return false;
  size_t used = 0;
  auto put = [&](const char* text) {
    const size_t n = strlen(text);
    if (used + n >= outCap) return false;
    memcpy(out + used, text, n);
    used += n;
    return true;
  };
  if (!put("{\"books\":[")) return false;
  for (int i = 0; i < count; ++i) {
    if (!idOk(books[i].bookId)) return false;
    if (i && !put(",")) return false;
    if (!put("{\"book_id\":")) return false;
    char id[kMaxId + 2];
    size_t idUsed = 0;
    appendJsonString(id, sizeof(id), idUsed, books[i].bookId);
    id[idUsed] = '\0';
    if (!put(id) || !put(",\"item_id\":")) return false;
    char item[kMaxId + 2];
    size_t itemUsed = 0;
    appendJsonString(item, sizeof(item), itemUsed, books[i].itemId[0] ? books[i].itemId : "0");
    item[itemUsed] = '\0';
    if (!put(item) || !put("}")) return false;
  }
  if (!put("]}")) return false;
  out[used] = '\0';
  return true;
}

int shelfPercent(int chapterIndex, int chapterCount, double rawProgress, bool tenThousand) {
  if (tenThousand) {
    if (rawProgress < 0) return 0;
    if (rawProgress > 10000) return 100;
    return static_cast<int>(rawProgress / 100.0 + 0.5);
  }
  if (chapterCount <= 0) return 0;
  if (chapterIndex < 0) chapterIndex = 0;
  if (chapterIndex >= chapterCount) chapterIndex = chapterCount - 1;
  double intra = rawProgress;
  if (intra < 0) intra = 0;
  if (intra > 1) intra = 1;
  const double whole = (static_cast<double>(chapterIndex) + intra) / static_cast<double>(chapterCount);
  int percent = static_cast<int>(whole * 100.0 + 0.5);
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  return percent;
}

ProgressChoice chooseProgress(bool dirty, uint32_t localUnix, uint32_t cloudUnix) {
  if (dirty) return ProgressChoice::KeepLocal;
  if (cloudUnix > localUnix) return ProgressChoice::ApplyCloud;
  return ProgressChoice::KeepLocal;
}

int chapterIndexForItem(const char* itemId, const ChapterRef* chapters, int count) {
  if (!itemId || !chapters) return -1;
  for (int i = 0; i < count; ++i) {
    if (strcmp(chapters[i].itemId, itemId) == 0) return i;
  }
  return -1;
}

bool qrStartUrl(char* out, size_t outCap) {
  return appendUrl(out, outCap,
                   "https://fanqienovel.com/passport/web/get_qrcode/?passport_jssdk_version=3.0.16&"
                   "passport_jssdk_type=normal&aid=2503&language=zh&account_sdk_source=web&need_logo=true&next="
                   "https%3A%2F%2Ffanqienovel.com%2Fmain%2Fwriter%2Flogin");
}

bool qrPollUrl(const char* token, char* out, size_t outCap) {
  if (!token || !token[0] || strpbrk(token, "&? \r\n")) return false;
  return appendUrl(out, outCap,
                   "https://fanqienovel.com/passport/web/check_qrconnect/?passport_jssdk_version=3.0.16&"
                   "passport_jssdk_type=normal&aid=2503&language=zh&account_sdk_source=web&token=",
                   token, "&next=%2F");
}

bool progressUrl(char* out, size_t outCap) {
  return appendUrl(out, outCap, "https://fanqienovel.com/api/reader/book/progress");
}

bool updateProgressUrl(char* out, size_t outCap) {
  return appendUrl(out, outCap, "https://fanqienovel.com/api/reader/book/update_progress");
}

bool shelfUrl(char* out, size_t outCap) {
  return appendUrl(out, outCap,
                   "https://fanqienovel.com/reading/bookapi/bookshelf/info/v:version/?aid=1967&iid=0&"
                   "version_code=57700&update_version_code=57700");
}

bool multidetailUrl(char* out, size_t outCap) {
  return appendUrl(out, outCap, "https://fanqienovel.com/api/bookshelf/multidetail");
}

bool directoryUrl(const char* bookId, char* out, size_t outCap) {
  if (!idOk(bookId)) return false;
  return appendUrl(out, outCap, "https://fanqienovel.com/api/reader/directory/detail?bookId=", bookId);
}

bool webChapterUrl(const char* bookId, const char* itemId, char* out, size_t outCap) {
  if (!idOk(bookId) || !idOk(itemId)) return false;
  const int written =
      snprintf(out, outCap, "https://fanqienovel.com/api/reader/chapter/content?book_id=%s&item_id=%s", bookId, itemId);
  return written > 0 && static_cast<size_t>(written) < outCap;
}

bool loginPageUrl(char* out, size_t outCap) {
  return appendUrl(out, outCap, "https://fanqienovel.com/main/writer/login");
}

bool appChapterQuery(const char* deviceId, const char* itemId, char* out, size_t outCap) {
  if (!idOk(deviceId) || !idOk(itemId)) return false;
  const int written =
      snprintf(out, outCap,
               "aid=1967&app_name=novelapp&channel=0&device_platform=android&device_id=%s&device_type=Honor10&"
               "os_version=0&version_code=66.9&novel_text_type=0&item_id=%s",
               deviceId, itemId);
  return written > 0 && static_cast<size_t>(written) < outCap;
}

bool appChapterUrl(const char* query, char* out, size_t outCap) {
  if (!query || !query[0]) return false;
  return appendUrl(out, outCap, "https://reading.snssdk.com/reading/reader/full/v/?", query);
}

bool registerKeyQuery(const char* deviceId, char* out, size_t outCap) {
  if (!idOk(deviceId)) return false;
  const int written = snprintf(out, outCap,
                               "aid=1967&app_name=novelapp&channel=0&device_id=%s&device_platform=android&iid=0&"
                               "os_version=0&version_code=6.1.5.32&version_name=6.1.5.32",
                               deviceId);
  return written > 0 && static_cast<size_t>(written) < outCap;
}

bool registerKeyUrl(const char* query, char* out, size_t outCap) {
  if (!query || !query[0]) return false;
  return appendUrl(out, outCap, "https://reading.snssdk.com/reading/crypt/registerkey?", query);
}

}  // namespace FanqieProtocol

#endif
