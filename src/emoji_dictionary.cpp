#include "emoji_dictionary.h"

#include <algorithm>
#include <utility>

#include <glib.h>

#include "im/im.h"

namespace areca {

std::string foldEmojiKeyword(std::string_view keyword) {
  gchar *folded =
      g_utf8_strdown(keyword.data(), static_cast<gssize>(keyword.size()));
  if (!folded) {
    return std::string(keyword);
  }
  std::string result(folded);
  g_free(folded);
  return result;
}

EmojiDictionary::EmojiDictionary(EmojiAnnotationLoader loader,
                                 std::vector<std::string> languages,
                                 DebugProvider debugProvider)
    : loader_(std::move(loader)), languages_(std::move(languages)),
      debugProvider_(std::move(debugProvider)) {}

EmojiDictionary::~EmojiDictionary() {
  if (worker_.joinable()) {
    worker_.join();
  }
}

void EmojiDictionary::startLoading() {
  if (started_ || !loader_) {
    return;
  }
  started_ = true;
  worker_ = std::thread([this]() { load(); });
}

void EmojiDictionary::load() {
  auto index = std::make_shared<Index>();
  index->reserve(languages_.size());
  for (const auto &language : languages_) {
    KeywordIndex keywords;
    std::size_t emojiCount = 0;
    for (auto &entry : loader_(language)) {
      if (entry.emoji.empty()) {
        continue;
      }
      ++emojiCount;
      for (const auto &keyword : entry.keywords) {
        if (keyword.empty()) {
          continue;
        }
        auto &emojis = keywords[foldEmojiKeyword(keyword)];
        if (std::find(emojis.begin(), emojis.end(), entry.emoji) ==
            emojis.end()) {
          emojis.push_back(entry.emoji);
        }
      }
    }
    if (debugProvider_ && debugProvider_()) {
      ARECA_INFO() << "areca: emoji dictionary loaded lang=" << language
                   << " emoji=" << emojiCount
                   << " keywords=" << keywords.size();
    }
    index->push_back(std::move(keywords));
  }
  std::lock_guard<std::mutex> lock(mutex_);
  index_ = std::move(index);
}

std::vector<EmojiMatch> EmojiDictionary::lookup(std::string_view keyword,
                                                std::size_t limit) const {
  std::shared_ptr<const Index> index;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    index = index_;
  }
  std::vector<EmojiMatch> result;
  if (!index || keyword.empty() || limit == 0) {
    return result;
  }
  const std::string folded = foldEmojiKeyword(keyword);
  for (const auto &keywords : *index) {
    // lower_bound đưa từ khoá khớp đúng lên đầu, sau đó là các từ khoá dài
    // hơn bắt đầu bằng nó theo thứ tự chữ cái.
    for (auto it = keywords.lower_bound(folded);
         it != keywords.end() && it->first.starts_with(folded); ++it) {
      for (const auto &emoji : it->second) {
        appendEmojiMatch(result, emoji, it->first, limit);
        if (result.size() >= limit) {
          return result;
        }
      }
    }
    if (!result.empty()) {
      break;
    }
  }
  return result;
}

} // namespace areca
