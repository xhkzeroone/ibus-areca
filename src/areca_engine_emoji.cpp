#include "areca_engine.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "im/im.h"

#include "emoji_suggestion.h"

namespace areca {

namespace {

// Danh sách dài hơn chỉ làm người dùng phải đọc nhiều.
constexpr std::size_t kMaxEmojiCandidates = 9;

class EmojiCandidateWord final : public im::CandidateWord {
public:
  EmojiCandidateWord(EmojiMatch match,
                     std::function<void(const std::string &)> onSelect)
      : im::CandidateWord(im::Text(match.emoji + "  :" + match.name + ":")),
        emoji_(std::move(match.emoji)), onSelect_(std::move(onSelect)) {}

  void select(im::InputContext *) const override {
    if (onSelect_) {
      onSelect_(emoji_);
    }
  }

private:
  std::string emoji_;
  std::function<void(const std::string &)> onSelect_;
};

} // namespace

bool ArecaEngine::isEmojiSuggesting(im::InputContext &inputContext) const {
  auto *candList = inputContext.inputPanel().candidateList().get();
  auto *commonList = dynamic_cast<im::CommonCandidateList *>(candList);
  return commonList && commonList->size() > 0 &&
         dynamic_cast<const EmojiCandidateWord *>(&commonList->candidate(0)) !=
             nullptr;
}

void ArecaEngine::hideEmojiSuggestions(im::InputContext &inputContext) {
  if (!isEmojiSuggesting(inputContext)) {
    return;
  }
  inputContext.inputPanel().reset();
  inputContext.updateUserInterface(im::UserInterfaceComponent::InputPanel,
                                   true);
}

void ArecaEngine::refreshEmojiSuggestions(im::InputContext &inputContext) {
  if (!config_.emojiSuggestion.value() || isBackendSelecting(inputContext) ||
      effectivePresentationMode(inputContext) != PresentationMode::Rewrite) {
    hideEmojiSuggestions(inputContext);
    return;
  }
  auto *state = inputContext.propertyFor(&rewriteStateFactory_);
  const auto query = state
                         ? emojiQueryBeforeCursor(state->knownTextBeforeCursor)
                         : std::nullopt;
  if (!query) {
    hideEmojiSuggestions(inputContext);
    return;
  }
  // Shortcode kiểu chat lên trước vì từ điển CLDR không có "like", "haha"; máy
  // không có từ điển vẫn dùng được shortcode. Từ điển còn đang nạp ở thread
  // riêng thì tra ra rỗng; phím sau sẽ thử lại.
  auto emojis = emojiShortcodeMatches(query->keyword, kMaxEmojiCandidates);
  if (emojis.size() < kMaxEmojiCandidates) {
    for (auto &match :
         emojiDictionary_.lookup(query->keyword, kMaxEmojiCandidates)) {
      appendEmojiMatch(emojis, match.emoji, match.name, kMaxEmojiCandidates);
    }
  }
  if (emojis.empty()) {
    hideEmojiSuggestions(inputContext);
    return;
  }
  if (debugEnabled()) {
    ARECA_INFO() << "areca: emoji suggest keyword=" << query->keyword
                 << " count=" << emojis.size();
  }

  auto candList = std::make_unique<im::CommonCandidateList>();
  candList->setLayoutHint(im::CandidateLayoutHint::Vertical);
  candList->setPageSize(static_cast<int>(emojis.size()));
  // Chọn bằng mũi tên và Enter như bộ chọn emoji của các ứng dụng chat, nên
  // không đánh số; phím số vẫn gõ chữ số bình thường.
  candList->setLabels(std::vector<std::string>(emojis.size()));
  auto inputContextWatch = inputContext.watch();
  for (auto &value : emojis) {
    candList->append(std::make_unique<EmojiCandidateWord>(
        std::move(value), [this, inputContextWatch](const std::string &chosen) {
          // Chọn bằng chuột tới từ IBus: đưa sang pha post để không sửa panel
          // ngay trong lúc đang duyệt danh sách.
          emojiSelectionPost_.schedule([this, inputContextWatch, chosen]() {
            if (auto *ic = inputContextWatch.get()) {
              selectEmoji(*ic, chosen);
            }
          });
        }));
  }
  candList->setGlobalCursorIndex(0);

  auto &panel = inputContext.inputPanel();
  panel.reset();
  panel.setCandidateList(std::move(candList));
  inputContext.updateUserInterface(im::UserInterfaceComponent::InputPanel,
                                   true);
}

void ArecaEngine::selectEmoji(im::InputContext &inputContext,
                              std::string emoji) {
  hideEmojiSuggestions(inputContext);
  if (debugEnabled()) {
    ARECA_INFO() << "areca: emoji select emoji=" << emoji;
  }
  // Đoạn ":từ" được đọc lại lúc tới lượt vì các phím đang xếp hàng có thể vẫn
  // còn đổi nó.
  scheduler_.enqueueReplacement(
      inputContext, std::move(emoji), [this](im::InputContext &ic) {
        auto *state = ic.propertyFor(&rewriteStateFactory_);
        if (!state) {
          return std::string();
        }
        const auto query = emojiQueryBeforeCursor(state->knownTextBeforeCursor);
        return query ? query->replacedText : std::string();
      });
}

bool ArecaEngine::handleEmojiSuggestionKey(im::InputContext &inputContext,
                                           im::KeyEvent &event) {
  const auto key = event.key().normalize();
  const auto sym = key.sym();
  const bool plain = !key.states().testAny(
      im::KeyStates{im::KeyState::Ctrl, im::KeyState::Alt, im::KeyState::Super,
                    im::KeyState::Shift});
  const bool navigation = sym == im::Key_Up || sym == im::Key_Down ||
                          sym == im::Key_Return || sym == im::Key_KP_Enter ||
                          sym == im::Key_Tab || sym == im::Key_Escape;
  if (!plain || !navigation) {
    return false;
  }
  // Danh sách đang giữ các phím này: phím nhả cũng không được lọt vào ứng dụng,
  // nếu không Enter có thể gửi tin nhắn ngay sau khi chọn emoji.
  if (event.isRelease()) {
    return true;
  }
  auto *commonList = dynamic_cast<im::CommonCandidateList *>(
      inputContext.inputPanel().candidateList().get());
  if (!commonList) {
    return false;
  }
  switch (sym) {
  case im::Key_Escape:
    hideEmojiSuggestions(inputContext);
    break;
  case im::Key_Up:
    commonList->prevCandidate();
    inputContext.updateUserInterface(im::UserInterfaceComponent::InputPanel,
                                     true);
    break;
  case im::Key_Down:
    commonList->nextCandidate();
    inputContext.updateUserInterface(im::UserInterfaceComponent::InputPanel,
                                     true);
    break;
  default: {
    const int index = commonList->cursorIndex();
    if (index >= 0 && index < commonList->size()) {
      commonList->candidate(index).select(&inputContext);
    }
    break;
  }
  }
  return true;
}

} // namespace areca
