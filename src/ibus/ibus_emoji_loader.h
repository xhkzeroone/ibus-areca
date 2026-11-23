#pragma once

#include <string>
#include <vector>

#include "emoji_dictionary.h"

namespace areca {

// Đọc từ điển emoji IBus cài sẵn, ibus/dicts/emoji-<ngôn ngữ>.dict trong thư
// mục dữ liệu hệ thống. Không có file thì trả rỗng. Chạy được trên thread
// riêng.
std::vector<EmojiAnnotation>
loadIbusEmojiAnnotations(const std::string &language);

} // namespace areca
