#include "input_context.h"

#include <random>

#include "utf8.h"

namespace areca::im {

std::string SurroundingText::selectedText() const {
  if (!valid_ || cursor_ == anchor_) {
    return {};
  }
  const auto begin = std::min(cursor_, anchor_);
  const auto end = std::max(cursor_, anchor_);
  const auto beginByte = utf8::detail::byteLength(text_, begin);
  const auto endByte = utf8::detail::byteLength(text_, end);
  if (beginByte == utf8::detail::kInvalidLength ||
      endByte == utf8::detail::kInvalidLength) {
    return {};
  }
  return text_.substr(beginByte, endByte - beginByte);
}

void SurroundingText::setText(const std::string &text, unsigned int cursor,
                              unsigned int anchor) {
  const auto length = utf8::detail::length(text);
  if (length == utf8::detail::kInvalidLength || cursor > length ||
      anchor > length) {
    invalidate();
    return;
  }
  valid_ = true;
  text_ = text;
  cursor_ = cursor;
  anchor_ = anchor;
}

void SurroundingText::setCursor(unsigned int cursor, unsigned int anchor) {
  const auto length = utf8::detail::length(text_);
  if (length == utf8::detail::kInvalidLength || cursor > length ||
      anchor > length) {
    invalidate();
    return;
  }
  cursor_ = cursor;
  anchor_ = anchor;
}

void SurroundingText::deleteText(int offset, unsigned int size) {
  if (!valid_) {
    return;
  }
  // Có vùng chọn thì client xoá vùng chọn trước, con trỏ về đầu vùng chọn.
  if (cursor_ != anchor_) {
    const auto begin = std::min(cursor_, anchor_);
    const auto end = std::max(cursor_, anchor_);
    const auto beginByte = utf8::detail::byteLength(text_, begin);
    const auto endByte = utf8::detail::byteLength(text_, end);
    text_.erase(beginByte, endByte - beginByte);
    cursor_ = anchor_ = begin;
  }
  const auto length = static_cast<int64_t>(utf8::detail::length(text_));
  int64_t start = static_cast<int64_t>(cursor_) + offset;
  int64_t end = start + size;
  if (start < 0) {
    start = 0;
  }
  if (end > length) {
    end = length;
  }
  if (start >= end) {
    return;
  }
  const auto startByte =
      utf8::detail::byteLength(text_, static_cast<std::size_t>(start));
  const auto endByte =
      utf8::detail::byteLength(text_, static_cast<std::size_t>(end));
  text_.erase(startByte, endByte - startByte);
  if (static_cast<int64_t>(cursor_) > start) {
    cursor_ = static_cast<unsigned int>(start);
  }
  anchor_ = cursor_;
}

void SurroundingText::invalidate() {
  valid_ = false;
  text_.clear();
  cursor_ = 0;
  anchor_ = 0;
}

size_t Text::textLength() const { return utf8::length(text_); }

void CommonCandidateList::prevCandidate() {
  if (words_.empty()) {
    return;
  }
  cursorIndex_ = cursorIndex_ <= 0 ? size() - 1 : cursorIndex_ - 1;
}

void CommonCandidateList::nextCandidate() {
  if (words_.empty()) {
    return;
  }
  cursorIndex_ = cursorIndex_ + 1 >= size() ? 0 : cursorIndex_ + 1;
}

void InputPanel::reset() {
  preedit_.clear();
  clientPreedit_.clear();
  auxUp_.clear();
  candidateList_.reset();
}

InputContext::InputContext(std::string program) : program_(std::move(program)) {
  std::random_device random;
  for (auto &byte : uuid_) {
    byte = static_cast<uint8_t>(random());
  }
}

InputContext::~InputContext() { *alive_ = false; }

void InputContext::deleteSurroundingText(int offset, unsigned int size) {
  deleteSurroundingTextImpl(offset, size);
}

InputContextProperty *InputContext::propertyForBase(FactoryBase *factory) {
  auto it = properties_.find(factory);
  if (it == properties_.end()) {
    std::unique_ptr<InputContextProperty> property(factory->create(*this));
    it = properties_.emplace(factory, std::move(property)).first;
  }
  return it->second.get();
}

} // namespace areca::im
