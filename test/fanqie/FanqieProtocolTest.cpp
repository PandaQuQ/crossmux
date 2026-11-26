#include "FanqieProtocol.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

struct ProgressSink {
  std::vector<FanqieProtocol::ProgressRecord> records;
};

bool takeProgress(void* ctx, const FanqieProtocol::ProgressRecord& record) {
  static_cast<ProgressSink*>(ctx)->records.push_back(record);
  return true;
}

struct ChapterSink {
  std::vector<FanqieProtocol::ChapterRef> records;
};

bool takeChapter(void* ctx, const FanqieProtocol::ChapterRef& chapter) {
  static_cast<ChapterSink*>(ctx)->records.push_back(chapter);
  return true;
}

struct IdSink {
  std::vector<std::string> ids;
};

bool takeId(void* ctx, const char* id) {
  static_cast<IdSink*>(ctx)->ids.emplace_back(id);
  return true;
}

}  // namespace

TEST(FanqieProtocol, QrStartAndPoll) {
  const char* json =
      "{\"message\":\"success\",\"data\":{\"token\":\"tok-1\",\"qrcode\":\"YQ==\",\"expire_time\":1700000000}}";
  char token[32];
  uint8_t png[8];
  size_t pngLen = 0;
  uint32_t expire = 0;
  ASSERT_TRUE(FanqieProtocol::parseQrStart(json, strlen(json), token, sizeof(token), png, sizeof(png), pngLen, expire));
  EXPECT_STREQ(token, "tok-1");
  EXPECT_EQ(expire, 1700000000u);
  EXPECT_EQ(pngLen, 1u);
  EXPECT_EQ(png[0], 'a');
  const char* scanned = "{\"data\":{\"status\":\"scanned\"}}";
  const char* expired = "{\"data\":{\"status\":\"expired\"}}";
  const char* canceled = "{\"data\":{\"status\":\"canceled\"}}";
  EXPECT_EQ(FanqieProtocol::parseLoginPoll(scanned, strlen(scanned)), FanqieProtocol::LoginPoll::Scanned);
  EXPECT_EQ(FanqieProtocol::parseLoginPoll(expired, strlen(expired)), FanqieProtocol::LoginPoll::Expired);
  EXPECT_EQ(FanqieProtocol::parseLoginPoll(canceled, strlen(canceled)), FanqieProtocol::LoginPoll::Cancelled);
}

TEST(FanqieProtocol, ProgressShelfAndChapters) {
  const char* progress =
      "{\"code\":0,\"data\":[{\"book_id\":\"7099271295698734095\",\"item_id\":\"7100000000000000001\","
      "\"index\":3,\"read_progress\":0.25,\"read_timestamp\":\"1700000100\"}]}";
  ProgressSink sink;
  ASSERT_TRUE(FanqieProtocol::parseProgress(progress, strlen(progress), takeProgress, &sink));
  ASSERT_EQ(sink.records.size(), 1u);
  EXPECT_STREQ(sink.records[0].bookId, "7099271295698734095");
  EXPECT_STREQ(sink.records[0].itemId, "7100000000000000001");
  EXPECT_EQ(sink.records[0].index, 3);
  EXPECT_NEAR(sink.records[0].rawProgress, 0.25, 0.001);
  EXPECT_FALSE(sink.records[0].tenThousand);
  EXPECT_EQ(sink.records[0].readUnix, 1700000100u);

  const char* shelf =
      "{\"data\":{\"book_shelf_info\":[{\"book_id\":\"11\"},{\"book_id\":\"22\"}]}}";
  IdSink ids;
  ASSERT_TRUE(FanqieProtocol::parseShelfIds(shelf, strlen(shelf), takeId, &ids));
  ASSERT_EQ(ids.ids.size(), 2u);
  EXPECT_EQ(ids.ids[1], "22");

  const char* detail =
      "{\"data\":{\"detail_list\":[{\"book_id\":\"11\",\"book_name\":\"Story\",\"author\":\"Ada\",\"serial_count\":10}]}}";
  FanqieProtocol::ShelfBook books[2];
  int count = 0;
  ASSERT_TRUE(FanqieProtocol::parseBookDetails(detail, strlen(detail), books, 2, count));
  ASSERT_EQ(count, 1);
  EXPECT_STREQ(books[0].title, "Story");
  EXPECT_STREQ(books[0].author, "Ada");
  EXPECT_EQ(books[0].chapterCount, 10u);

  const char* directory =
      "{\"data\":{\"chapterListWithVolume\":[[{\"itemId\":\"100\",\"title\":\"One\"},{\"itemId\":\"101\",\"title\":\"Two\"}]]}}";
  ChapterSink chapters;
  ASSERT_TRUE(FanqieProtocol::parseChapters(directory, strlen(directory), takeChapter, &chapters));
  ASSERT_EQ(chapters.records.size(), 2u);
  EXPECT_STREQ(chapters.records[1].title, "Two");
  EXPECT_EQ(FanqieProtocol::chapterIndexForItem("101", chapters.records.data(), 2), 1);
}

TEST(FanqieProtocol, ChapterKindProgressMappingAndBodies) {
  EXPECT_EQ(FanqieProtocol::classifyChapter("<p>hello chapter text</p>", 25), FanqieProtocol::ChapterKind::Text);
  const char pua[] = "\xee\x80\x80hidden";
  EXPECT_EQ(FanqieProtocol::classifyChapter(pua, sizeof(pua) - 1), FanqieProtocol::ChapterKind::Obfuscated);
  char plain[64];
  size_t plainLen = 0;
  ASSERT_TRUE(FanqieProtocol::stripChapterText("<p>hello</p><p>world</p>", 22, plain, sizeof(plain), plainLen));
  EXPECT_NE(strstr(plain, "hello"), nullptr);
  EXPECT_EQ(strchr(plain, '<'), nullptr);

  EXPECT_EQ(FanqieProtocol::shelfPercent(1, 4, 0.5, false), 38);
  EXPECT_EQ(FanqieProtocol::shelfPercent(0, 10, 2500, true), 25);
  EXPECT_EQ(FanqieProtocol::chooseProgress(true, 1, 9), FanqieProtocol::ProgressChoice::KeepLocal);
  EXPECT_EQ(FanqieProtocol::chooseProgress(false, 1, 9), FanqieProtocol::ProgressChoice::ApplyCloud);
  EXPECT_EQ(FanqieProtocol::chooseProgress(false, 9, 1), FanqieProtocol::ProgressChoice::KeepLocal);

  char body[256];
  ASSERT_TRUE(FanqieProtocol::progressUpdateBody("11", "22", 3, 0.5, 170, body, sizeof(body)));
  EXPECT_NE(strstr(body, "\"book_id\":\"11\""), nullptr);
  EXPECT_NE(strstr(body, "\"read_progress\":0.5000"), nullptr);
  EXPECT_NE(strstr(body, "\"index\":3"), nullptr);

  FanqieProtocol::ShelfBook books[1] = {};
  strcpy(books[0].bookId, "11");
  strcpy(books[0].itemId, "22");
  ASSERT_TRUE(FanqieProtocol::multidetailBody(books, 1, body, sizeof(body)));
  EXPECT_NE(strstr(body, "\"book_id\":\"11\""), nullptr);
}

TEST(FanqieProtocol, UrlsRejectUnsafeIds) {
  char url[512];
  EXPECT_TRUE(FanqieProtocol::qrStartUrl(url, sizeof(url)));
  EXPECT_NE(strstr(url, "/passport/web/get_qrcode/"), nullptr);
  EXPECT_TRUE(FanqieProtocol::qrPollUrl("abc", url, sizeof(url)));
  EXPECT_FALSE(FanqieProtocol::qrPollUrl("a&b", url, sizeof(url)));
  EXPECT_TRUE(FanqieProtocol::directoryUrl("123", url, sizeof(url)));
  EXPECT_FALSE(FanqieProtocol::webChapterUrl("12 3", "4", url, sizeof(url)));
  EXPECT_TRUE(FanqieProtocol::appChapterQuery("7123456789012345678", "42", url, sizeof(url)));
  EXPECT_NE(strstr(url, "device_type=Honor10"), nullptr);
  EXPECT_NE(strstr(url, "novel_text_type=0"), nullptr);
  EXPECT_TRUE(FanqieProtocol::registerKeyQuery("7123456789012345678", url, sizeof(url)));
  EXPECT_NE(strstr(url, "iid=0"), nullptr);
}

TEST(FanqieProtocol, CookieJarUpdatesWithoutKeepingExpiredValues) {
  FanqieProtocol::CookieJar jar;
  ASSERT_TRUE(jar.absorbSetCookie("sessionid=abc; Path=/; Domain=.fanqienovel.com"));
  ASSERT_TRUE(jar.absorbSetCookie("passport_csrf_token=csrf; Path=/"));
  EXPECT_TRUE(jar.has("sessionid"));
  ASSERT_TRUE(jar.absorbSetCookie("sessionid=; Max-Age=0"));
  EXPECT_FALSE(jar.has("sessionid"));
  EXPECT_TRUE(jar.has("passport_csrf_token"));
  EXPECT_TRUE(jar.absorbSetCookie("sid=other; Domain=example.com"));
  EXPECT_FALSE(jar.has("sid"));
}
