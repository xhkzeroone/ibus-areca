#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "action.h"
#include "flags.h"
#include "key.h"

namespace areca::im {

enum class CapabilityFlag : uint64_t {
  NoFlag = 0,
  Preedit = 1ULL << 1,
  FormattedPreedit = 1ULL << 4,
  Password = 1ULL << 3,
  SurroundingText = 1ULL << 6,
  Url = 1ULL << 9,
  Terminal = 1ULL << 10,
  KeyEventOrderFix = 1ULL << 37,
};
using CapabilityFlags = Flags<CapabilityFlag>;

using ICUUID = std::array<uint8_t, 16>;

// Text quanh con trỏ mà client báo. Cursor và anchor tính theo ký tự.
class SurroundingText {
public:
  bool isValid() const { return valid_; }
  const std::string &text() const { return text_; }
  unsigned int cursor() const { return cursor_; }
  unsigned int anchor() const { return anchor_; }
  std::string selectedText() const;

  void setText(const std::string &text, unsigned int cursor,
               unsigned int anchor);
  void setCursor(unsigned int cursor, unsigned int anchor);
  // Sửa bản sao cục bộ sau khi gửi lệnh xoá, như client sẽ làm.
  void deleteText(int offset, unsigned int size);
  void invalidate();

private:
  bool valid_ = false;
  std::string text_;
  unsigned int cursor_ = 0;
  unsigned int anchor_ = 0;
};

class Text {
public:
  Text() = default;
  explicit Text(std::string text) { append(std::move(text)); }
  void append(std::string text) { text_ += std::move(text); }
  void setCursor(int cursor) { cursor_ = cursor; }
  int cursor() const { return cursor_; }
  size_t textLength() const;
  const std::string &toString() const { return text_; }
  bool empty() const { return text_.empty(); }
  void clear() {
    text_.clear();
    cursor_ = -1;
  }

private:
  std::string text_;
  int cursor_ = -1;
};

class InputContext;

class CandidateWord {
public:
  explicit CandidateWord(Text text) : text_(std::move(text)) {}
  virtual ~CandidateWord() = default;
  const Text &text() const { return text_; }
  virtual void select(InputContext *inputContext) const = 0;

private:
  Text text_;
};

enum class CandidateLayoutHint { NotSet, Vertical, Horizontal };

// Danh sách ứng viên một trang, có con trỏ chọn để duyệt bằng phím mũi tên.
class CommonCandidateList {
public:
  void append(std::unique_ptr<CandidateWord> word) {
    words_.push_back(std::move(word));
    if (cursorIndex_ < 0) {
      cursorIndex_ = 0;
    }
  }
  int size() const { return static_cast<int>(words_.size()); }
  const CandidateWord &candidate(int index) const { return *words_.at(index); }
  int cursorIndex() const { return cursorIndex_; }
  void setGlobalCursorIndex(int index) { cursorIndex_ = index; }
  void prevCandidate();
  void nextCandidate();
  void setLabels(std::vector<std::string> labels) {
    labels_ = std::move(labels);
  }
  const std::vector<std::string> &labels() const { return labels_; }
  void setPageSize(int pageSize) { pageSize_ = pageSize; }
  int pageSize() const { return pageSize_; }
  void setLayoutHint(CandidateLayoutHint hint) { layoutHint_ = hint; }
  CandidateLayoutHint layoutHint() const { return layoutHint_; }

private:
  std::vector<std::unique_ptr<CandidateWord>> words_;
  std::vector<std::string> labels_;
  int cursorIndex_ = -1;
  int pageSize_ = 10;
  CandidateLayoutHint layoutHint_ = CandidateLayoutHint::NotSet;
};

class InputPanel {
public:
  void reset();
  const Text &preedit() const { return preedit_; }
  void setPreedit(Text text) { preedit_ = std::move(text); }
  const Text &clientPreedit() const { return clientPreedit_; }
  void setClientPreedit(Text text) { clientPreedit_ = std::move(text); }
  const Text &auxUp() const { return auxUp_; }
  void setAuxUp(Text text) { auxUp_ = std::move(text); }
  const std::shared_ptr<CommonCandidateList> &candidateList() const {
    return candidateList_;
  }
  void setCandidateList(std::unique_ptr<CommonCandidateList> list) {
    candidateList_ = std::move(list);
  }

private:
  Text preedit_;
  Text clientPreedit_;
  Text auxUp_;
  std::shared_ptr<CommonCandidateList> candidateList_;
};

enum class UserInterfaceComponent { InputPanel, StatusArea };

// State riêng của từng input context, tạo khi được hỏi lần đầu.
class InputContextProperty {
public:
  virtual ~InputContextProperty() = default;
};

class FactoryBase {
public:
  virtual ~FactoryBase() = default;
  virtual InputContextProperty *create(InputContext &inputContext) = 0;
};

template <typename T> class FactoryFor final : public FactoryBase {
public:
  using Creator = std::function<T *(InputContext &)>;
  explicit FactoryFor(Creator creator) : creator_(std::move(creator)) {}
  InputContextProperty *create(InputContext &inputContext) override {
    return creator_(inputContext);
  }

private:
  Creator creator_;
};

// Tham chiếu yếu tới input context; get() trả nullptr khi context đã bị huỷ.
template <typename T> class TrackableObjectReference {
public:
  TrackableObjectReference() = default;
  TrackableObjectReference(std::weak_ptr<bool> alive, T *object)
      : alive_(std::move(alive)), object_(object) {}
  T *get() const {
    const auto alive = alive_.lock();
    return alive && *alive ? object_ : nullptr;
  }
  bool isValid() const { return get() != nullptr; }
  void unwatch() {
    alive_.reset();
    object_ = nullptr;
  }

private:
  std::weak_ptr<bool> alive_;
  T *object_ = nullptr;
};

class ForwardKeyEvent {
public:
  ForwardKeyEvent(Key key, bool isRelease) : key_(key), isRelease_(isRelease) {}
  const Key &rawKey() const { return key_; }
  const Key &key() const { return key_; }
  bool isRelease() const { return isRelease_; }

private:
  Key key_;
  bool isRelease_;
};

// Chỉ để test chép từ bản fcitx5 tạo context theo cùng cú pháp.
class FactoryBase;
class InputContextManager {
public:
  void registerProperty(const char *, FactoryBase *) {}
};

class InputContext {
public:
  explicit InputContext(std::string program = {});
  explicit InputContext(InputContextManager &, std::string program = {})
      : InputContext(std::move(program)) {}
  virtual ~InputContext();
  InputContext(const InputContext &) = delete;
  InputContext &operator=(const InputContext &) = delete;

  const std::string &program() const { return program_; }
  void setProgram(std::string program) { program_ = std::move(program); }
  const ICUUID &uuid() const { return uuid_; }
  CapabilityFlags capabilityFlags() const { return capabilityFlags_; }
  void setCapabilityFlags(CapabilityFlags flags) { capabilityFlags_ = flags; }
  bool hasFocus() const { return hasFocus_; }
  void setHasFocus(bool focus) { hasFocus_ = focus; }
  void focusIn() { hasFocus_ = true; }
  void focusOut() { hasFocus_ = false; }

  SurroundingText &surroundingText() { return surroundingText_; }
  const SurroundingText &surroundingText() const { return surroundingText_; }
  InputPanel &inputPanel() { return inputPanel_; }
  StatusArea &statusArea() { return statusArea_; }

  void commitString(const std::string &text) { commitStringImpl(text); }
  void deleteSurroundingText(int offset, unsigned int size);
  void forwardKey(const Key &key, bool isRelease = false) {
    forwardKeyImpl(ForwardKeyEvent{key, isRelease});
  }
  void updatePreedit() { updatePreeditImpl(); }
  void updateUserInterface(UserInterfaceComponent component,
                           bool immediate = false) {
    updateUserInterfaceImpl(component, immediate);
  }

  TrackableObjectReference<InputContext> watch() {
    return TrackableObjectReference<InputContext>(alive_, this);
  }

  template <typename T> T *propertyFor(FactoryFor<T> *factory) {
    return static_cast<T *>(propertyForBase(factory));
  }

protected:
  // Lớp con gọi trong destructor; giữ để code test cũ biên dịch được.
  void destroy() {}
  virtual void commitStringImpl(const std::string &text) = 0;
  virtual void deleteSurroundingTextImpl(int offset, unsigned int size) = 0;
  virtual void forwardKeyImpl(const ForwardKeyEvent &event) = 0;
  virtual void updatePreeditImpl() = 0;
  virtual void updateUserInterfaceImpl(UserInterfaceComponent, bool) {}

private:
  InputContextProperty *propertyForBase(FactoryBase *factory);

  std::string program_;
  ICUUID uuid_{};
  CapabilityFlags capabilityFlags_;
  bool hasFocus_ = false;
  SurroundingText surroundingText_;
  InputPanel inputPanel_;
  StatusArea statusArea_;
  std::unordered_map<FactoryBase *, std::unique_ptr<InputContextProperty>>
      properties_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

class KeyEvent {
public:
  KeyEvent(InputContext *inputContext, Key rawKey, bool isRelease = false,
           int time = 0)
      : inputContext_(inputContext), rawKey_(rawKey), key_(rawKey.normalize()),
        isRelease_(isRelease), time_(time) {}

  InputContext *inputContext() const { return inputContext_; }
  const Key &key() const { return key_; }
  const Key &rawKey() const { return rawKey_; }
  bool isRelease() const { return isRelease_; }
  int time() const { return time_; }

  void filter() { filtered_ = true; }
  void accept() { accepted_ = true; }
  void filterAndAccept() {
    filtered_ = true;
    accepted_ = true;
  }
  bool filtered() const { return filtered_; }
  bool accepted() const { return accepted_; }
  // Không làm gì: phím không bị filter thì client tự xử lý phím gốc.
  bool forward() const { return !filtered_; }

private:
  InputContext *inputContext_;
  Key rawKey_;
  Key key_;
  bool isRelease_;
  int time_;
  bool filtered_ = false;
  bool accepted_ = false;
};

} // namespace areca::im
