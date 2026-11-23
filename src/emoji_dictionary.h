#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "emoji_suggestion.h"

namespace areca {

struct EmojiAnnotation {
  std::string emoji;
  std::vector<std::string> keywords;
};

// Đọc từ khoá emoji của một ngôn ngữ từ dữ liệu có sẵn trên máy. Chạy trên
// thread nạp nên không được đụng state của engine hay input context.
using EmojiAnnotationLoader =
    std::function<std::vector<EmojiAnnotation>(const std::string &language)>;

// Tra emoji theo từ khoá. Dữ liệu nạp trên thread riêng vì file từ điển cỡ
// vài MB; chưa nạp xong thì tra ra rỗng thay vì chặn event loop.
class EmojiDictionary final {
public:
  using DebugProvider = std::function<bool()>;

  EmojiDictionary(EmojiAnnotationLoader loader,
                  std::vector<std::string> languages,
                  DebugProvider debugProvider);
  ~EmojiDictionary();
  EmojiDictionary(const EmojiDictionary &) = delete;
  EmojiDictionary &operator=(const EmojiDictionary &) = delete;

  // Gọi nhiều lần cũng chỉ nạp một lần.
  void startLoading();
  // Khớp đúng từ khoá lên trước rồi tới các từ khoá bắt đầu bằng nó, theo thứ
  // tự ngôn ngữ. Ngôn ngữ sau chỉ dùng khi ngôn ngữ trước không có kết quả,
  // để ":tim" không lẫn "timer" của tiếng Anh. Mỗi emoji kèm từ khoá đã khớp.
  std::vector<EmojiMatch> lookup(std::string_view keyword,
                                 std::size_t limit) const;

private:
  friend struct EmojiDictionaryTestAccess;
  using KeywordIndex = std::map<std::string, std::vector<std::string>>;
  using Index = std::vector<KeywordIndex>;

  void load();

  EmojiAnnotationLoader loader_;
  std::vector<std::string> languages_;
  DebugProvider debugProvider_;
  mutable std::mutex mutex_;
  std::shared_ptr<const Index> index_;
  std::thread worker_;
  bool started_ = false;
};

// Hạ chữ hoa xuống chữ thường cả với chữ có dấu, để "Cười" khớp "cười".
std::string foldEmojiKeyword(std::string_view keyword);

} // namespace areca
