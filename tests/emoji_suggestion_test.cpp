#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "im/im.h"

#include "emoji_dictionary.h"
#include "emoji_suggestion.h"
#include "input_scheduler.h"

namespace areca {

struct EmojiDictionaryTestAccess {
  static void waitLoaded(EmojiDictionary &dictionary) {
    if (dictionary.worker_.joinable()) {
      dictionary.worker_.join();
    }
  }
};

} // namespace areca

namespace {

void testQueryParsing() {
  auto query = areca::emojiQueryBeforeCursor("hôm nay vui :cười");
  assert(query);
  assert(query->keyword == "cười");
  assert(query->replacedText == ":cười");
  assert(query->replacedLength == 5);

  // Đầu đoạn đã biết cũng được coi là đầu từ.
  query = areca::emojiQueryBeforeCursor(":Tim");
  assert(query && query->keyword == "tim" && query->replacedLength == 4);

  // Từ khoá nhiều chữ.
  query = areca::emojiQueryBeforeCursor(" :mặt cười");
  assert(query && query->keyword == "mặt cười");

  // Giờ, URL, a:b và câu có dấu hai chấm không phải từ khoá.
  assert(!areca::emojiQueryBeforeCursor("10:30"));
  assert(!areca::emojiQueryBeforeCursor("http://abc"));
  assert(!areca::emojiQueryBeforeCursor("a:b"));
  assert(!areca::emojiQueryBeforeCursor("Ghi chú: hôm nay"));
  assert(!areca::emojiQueryBeforeCursor("x :"));
  // Dấu cách cuối nghĩa là đã gõ xong, dấu câu kết thúc từ khoá.
  assert(!areca::emojiQueryBeforeCursor(" :tim "));
  assert(!areca::emojiQueryBeforeCursor(" :tim."));
  assert(!areca::emojiQueryBeforeCursor(" :a  b"));
  assert(!areca::emojiQueryBeforeCursor(" :một hai ba bốn"));
  assert(!areca::emojiQueryBeforeCursor(" :abcdefghijklmnopqrstuvwxyz"));
  assert(!areca::emojiQueryBeforeCursor("không có"));

  // Shortcode kiểu chat có "+-_".
  query = areca::emojiQueryBeforeCursor(" :+1");
  assert(query && query->keyword == "+1" && query->replacedLength == 3);
  query = areca::emojiQueryBeforeCursor(":Thumbs_Up");
  assert(query && query->keyword == "thumbs_up");
  assert(!areca::emojiQueryBeforeCursor(" :-)"));
}

void testShortcodes() {
  // Khớp đúng lên trước, không lặp emoji, có giới hạn.
  auto found = areca::emojiShortcodeMatches("like", 9);
  assert(!found.empty() && found.front().emoji == "👍" &&
         found.front().name == "like");
  found = areca::emojiShortcodeMatches("+1", 9);
  assert(found.size() == 1 && found.front().emoji == "👍" &&
         found.front().name == "+1");
  // "hihi" và "hehe" bắt đầu bằng "h" nhưng khớp tiền tố "ha" chỉ có "haha".
  found = areca::emojiShortcodeMatches("ha", 9);
  assert(!found.empty() && found.front().emoji == "😂" &&
         found.front().name == "haha");
  found = areca::emojiShortcodeMatches("h", 2);
  assert(found.size() == 2);
  assert(areca::emojiShortcodeMatches("khongco", 9).empty());
  // Emoji đã có thì không thêm lần nữa dù tên khác.
  std::vector<areca::EmojiMatch> merged;
  areca::appendEmojiMatch(merged, "👍", "like", 9);
  areca::appendEmojiMatch(merged, "👍", "thumbs up", 9);
  areca::appendEmojiMatch(merged, "😂", "haha", 1);
  assert(merged.size() == 1 && merged.front().name == "like");
  // Shortcode trong bảng không trùng nhau.
  for (std::size_t i = 0; i < areca::kEmojiShortcodes.size(); ++i) {
    for (std::size_t j = i + 1; j < areca::kEmojiShortcodes.size(); ++j) {
      assert(areca::kEmojiShortcodes[i].code !=
             areca::kEmojiShortcodes[j].code);
    }
  }
}

std::vector<std::string>
emojisOf(const std::vector<areca::EmojiMatch> &matches) {
  std::vector<std::string> emojis;
  for (const auto &match : matches) {
    emojis.push_back(match.emoji);
  }
  return emojis;
}

void testDictionaryLookup() {
  std::vector<std::string> loadedLanguages;
  areca::EmojiDictionary dictionary(
      [&loadedLanguages](const std::string &language) {
        loadedLanguages.push_back(language);
        std::vector<areca::EmojiAnnotation> entries;
        if (language == "vi") {
          entries.push_back({"😀", {"cười", "Mặt cười"}});
          entries.push_back({"😂", {"cười ra nước mắt", "cười"}});
          entries.push_back({"❤️", {"tim"}});
        } else {
          entries.push_back({"⏲️", {"timer"}});
          entries.push_back({"😄", {"smile"}});
        }
        return entries;
      },
      {"vi", "en"}, [] { return false; });

  // Chưa nạp thì tra ra rỗng chứ không chặn.
  assert(dictionary.lookup("cười", 9).empty());
  dictionary.startLoading();
  dictionary.startLoading();
  areca::EmojiDictionaryTestAccess::waitLoaded(dictionary);
  assert((loadedLanguages == std::vector<std::string>{"vi", "en"}));

  // Khớp đúng trước, rồi tới từ khoá bắt đầu bằng nó, không lặp emoji.
  assert((emojisOf(dictionary.lookup("cười", 9)) ==
          std::vector<std::string>{"😀", "😂"}));
  assert((emojisOf(dictionary.lookup("cười", 1)) ==
          std::vector<std::string>{"😀"}));
  // Từ khoá có chữ hoa vẫn khớp, cả chữ có dấu.
  assert((emojisOf(dictionary.lookup("mặt cười", 9)) ==
          std::vector<std::string>{"😀"}));
  assert((emojisOf(dictionary.lookup("MẶT", 9)) ==
          std::vector<std::string>{"😀"}));
  // Tiếng Việt có kết quả thì không trộn tiếng Anh.
  assert(
      (emojisOf(dictionary.lookup("tim", 9)) == std::vector<std::string>{"❤️"}));
  // Tiếng Việt không có thì lùi về tiếng Anh.
  assert((emojisOf(dictionary.lookup("smi", 9)) ==
          std::vector<std::string>{"😄"}));
  assert(dictionary.lookup("không có", 9).empty());
  // Mỗi emoji kèm từ khoá đã khớp: khớp đúng là chính từ khoá, khớp tiền tố là
  // từ khoá đầy đủ trong từ điển.
  const auto named = dictionary.lookup("cười", 9);
  assert(named[0].name == "cười" && named[1].name == "cười");
  assert(dictionary.lookup("smi", 9)[0].name == "smile");
  assert(dictionary.lookup("MẶT", 9)[0].name == "mặt cười");

  // Không có nguồn dữ liệu thì không tạo thread và không gợi ý.
  areca::EmojiDictionary empty({}, {"vi"}, [] { return false; });
  empty.startLoading();
  assert(empty.lookup("cười", 9).empty());
}

class TestInputContext final : public areca::im::InputContext {
public:
  explicit TestInputContext(areca::im::InputContextManager &manager)
      : areca::im::InputContext(manager, "test-app") {}
  ~TestInputContext() override { destroy(); }

  void commitStringImpl(const std::string &text) override {
    commits.push_back(text);
  }
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const areca::im::ForwardKeyEvent &) override {}
  void updatePreeditImpl() override {}

  std::vector<std::string> commits;
};

class TestEngine final : public areca::VietnameseEngine {
public:
  bool canProcessKey(uint32_t) const override { return true; }
  areca::BambooResult process(uint32_t, const std::string &text) override {
    current_ += text;
    areca::BambooResult result;
    result.commitText = text;
    result.deleteCount = nextDeleteCount;
    nextDeleteCount = 0;
    return result;
  }
  areca::BambooResult processBackspace() override { return {}; }
  void backspace() override {}
  void reset() override {
    current_.clear();
    ++resetCount;
  }
  const std::string &currentText() const override { return current_; }

  int resetCount = 0;
  uint32_t nextDeleteCount = 0;

private:
  std::string current_;
};

class RecordingBackend final : public areca::RewriteBackend {
public:
  const char *name() const override { return "recording"; }
  areca::ApplyStatus apply(areca::im::InputContext &inputContext,
                           const areca::RewritePlan &plan,
                           areca::RewriteDone onDone) override {
    backspaces.push_back(plan.backspaceCount);
    if (holdNext) {
      holdNext = false;
      transactionId = plan.transactionId;
      pendingText = plan.commitText;
      pendingDone = std::move(onDone);
      return areca::ApplyStatus::Pending;
    }
    inputContext.commitString(plan.commitText);
    return areca::ApplyStatus::Completed;
  }

  std::vector<uint32_t> backspaces;
  bool holdNext = false;
  uint64_t transactionId = 0;
  std::string pendingText;
  areca::RewriteDone pendingDone;
};

void runLoopFor(areca::im::EventLoop &loop, uint64_t usec) {
  auto timer =
      loop.addTimeEvent(CLOCK_MONOTONIC, areca::im::now(CLOCK_MONOTONIC) + usec,
                        0, [&loop](areca::im::EventSourceTime *, uint64_t) {
                          loop.exit();
                          return false;
                        });
  timer->setOneShot();
  loop.exec();
}

void testSchedulerReplacement() {
  areca::im::EventLoop loop;
  areca::im::InputContextManager manager;
  TestInputContext context(manager);
  TestEngine engine;
  RecordingBackend backend;
  std::string replacedSeen;
  std::string known;
  areca::InputScheduler scheduler(
      loop, [&](areca::im::InputContext &) { return &engine; },
      [](areca::im::InputContext &) {
        areca::SchedulerTiming timing;
        timing.postCommitDelayMs = 0;
        return timing;
      },
      [] { return false; },
      [&](areca::im::InputContext &, const areca::BambooResult &result) {
        replacedSeen = result.currentText;
        return areca::RewriteBackendSelection{&backend};
      });
  std::vector<std::pair<uint32_t, std::string>> observed;
  scheduler.setCommitObserver([&](areca::im::InputContext &,
                                  uint32_t deleteCount,
                                  const std::string &text) {
    observed.emplace_back(deleteCount, text);
  });

  engine.process('c', "c");
  // Đoạn bị thay được đọc lúc tới lượt, tính theo ký tự chứ không theo byte,
  // và xoá thêm marker "·" commit ngay trước đó.
  scheduler.enqueueReplacement(context, "😀", [](areca::im::InputContext &) {
    return std::string(":cười");
  });
  assert((backend.backspaces == std::vector<uint32_t>{6}));
  assert((context.commits == std::vector<std::string>{"\xC2\xB7", "😀"}));
  assert(replacedSeen == ":cười");
  assert(engine.resetCount == 1);
  assert(engine.currentText().empty());
  assert(observed.size() == 1 && observed[0].first == 5 &&
         observed[0].second == "😀");

  runLoopFor(loop, 5000);

  // Không còn ":từ" khi tới lượt thì không xoá hay chèn gì.
  scheduler.enqueueReplacement(
      context, "😀", [](areca::im::InputContext &) { return std::string(); });
  assert(backend.backspaces.size() == 1);
  assert(context.commits.size() == 2);

  runLoopFor(loop, 5000);

  // Phím xếp trước vẫn đang chờ backend: resolver chỉ chạy khi phím đó xong,
  // nên đọc được ":tim" chứ không phải ":ti" lúc chọn.
  known = ":ti";
  int resolverCalls = 0;
  engine.nextDeleteCount = 1;
  backend.holdNext = true;
  scheduler.enqueue(context, 'm', "m");
  assert(backend.pendingDone);
  scheduler.enqueueReplacement(context, "❤️", [&](areca::im::InputContext &) {
    ++resolverCalls;
    return known;
  });
  assert(resolverCalls == 0);
  known = ":tim";
  auto done = std::move(backend.pendingDone);
  done(backend.transactionId, areca::RewriteOutcome::Succeeded);
  runLoopFor(loop, 20000);
  assert(resolverCalls == 1);
  assert(backend.backspaces.back() == 5);
  assert(context.commits.back() == "❤️");
}

} // namespace

int main() {
  testQueryParsing();
  testShortcodes();
  testDictionaryLookup();
  testSchedulerReplacement();
  std::cout << "Emoji suggestion test passed successfully\n";
  return 0;
}
