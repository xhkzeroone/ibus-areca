#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "im/im.h"

#include "bamboo_engine_adapter.h"
#include "mode_handler.h"
#include "sentence_capitalizer.h"

namespace areca {

enum class FinalizedWordRecoveryState {
  Idle,
  AwaitingBackspaceRelease,
  Erasing,
};

struct PreeditInputState final : public im::InputContextProperty {
  explicit PreeditInputState(std::string inputMethod, bool spellCheck,
                             bool realtimeSpellcheck, bool modernStyle,
                             std::string outputCharset, bool macroEnabled,
                             bool capitalizeMacro, uint64_t macroRevision,
                             std::vector<MacroDefinition> macros,
                             bool revertMacroOnBackspace = true);

  std::string inputMethod;
  bool spellCheck;
  bool realtimeSpellcheck;
  bool modernStyle;
  std::string outputCharset;
  bool macroEnabled;
  bool capitalizeMacro;
  bool revertMacroOnBackspace;
  uint64_t macroRevision;
  std::unique_ptr<VietnameseEngine> engine;
  std::string composing;
  SentenceCapitalizationState sentenceCapitalization;
  // Frontend vừa đổi text hoặc vị trí con trỏ. Phím text kế tiếp được phép thử
  // chuyển từ đã commit trước con trỏ trở lại thành preedit đúng một lần.
  bool surroundingRestoreArmed = false;
  // Đoạn text Areca biết chắc đang nằm ngay trước con trỏ, gom từ các lần
  // commit liên tiếp không có phím nào ứng dụng tự xử lý xen giữa. Rỗng nghĩa
  // là không biết.
  std::string knownTextBeforeCursor;
  FinalizedWordRecoveryState finalizedWordRecovery =
      FinalizedWordRecoveryState::Idle;
  uint64_t resetProtectedUntilUsec = 0;
};

class PreeditModeHandler final : public InputModeHandler {
public:
  using StateFactory = im::FactoryFor<PreeditInputState>;
  using DebugProvider = std::function<bool()>;
  using AutoCapitalizeProvider = std::function<bool()>;
  using BackspaceRecoveryProvider = std::function<bool()>;
  using RestoreSurroundingTextProvider = std::function<bool()>;
  using MacroBypassMatcher = std::function<bool(const im::Key &)>;
  using FinalizedWordEraseDone = std::function<void(bool)>;
  using FinalizedWordEraser =
      std::function<bool(im::InputContext &, uint32_t, FinalizedWordEraseDone)>;

  PreeditModeHandler(
      StateFactory &stateFactory, DebugProvider debugProvider,
      AutoCapitalizeProvider autoCapitalizeProvider,
      BackspaceRecoveryProvider backspaceRecoveryProvider,
      RestoreSurroundingTextProvider restoreSurroundingTextProvider,
      MacroBypassMatcher macroBypassMatcher,
      FinalizedWordEraser finalizedWordEraser);
  void activate(im::InputContext &inputContext) override;
  void deactivate(im::InputContext &inputContext) override;
  void handleKeyEvent(im::KeyEvent &event) override;
  void requestProtectedReset(im::InputContext &inputContext) override;
  void resetContext(im::InputContext &inputContext) override;

  // Chuẩn bị đọc lại từ quanh caret sau một click, nhưng không xóa preedit
  // đang hiển thị nếu frontend chưa kết thúc composition cũ.
  bool prepareSurroundingRestoreAfterClick(im::InputContext &inputContext);

  PreeditInputState *stateFor(im::InputContext &inputContext) const;

private:
  void updatePreedit(im::InputContext &inputContext,
                     const PreeditInputState &state);
  void clearComposition(im::InputContext &inputContext,
                        PreeditInputState &state);
  void commitComposition(im::InputContext &inputContext,
                         PreeditInputState &state);
  bool tryRestoreFromSurroundingText(im::InputContext &inputContext,
                                     PreeditInputState &state);
  bool startFinalizedWordRecovery(im::InputContext &inputContext,
                                  PreeditInputState &state);

  StateFactory &stateFactory_;
  DebugProvider debugProvider_;
  AutoCapitalizeProvider autoCapitalizeProvider_;
  BackspaceRecoveryProvider backspaceRecoveryProvider_;
  RestoreSurroundingTextProvider restoreSurroundingTextProvider_;
  MacroBypassMatcher macroBypassMatcher_;
  FinalizedWordEraser finalizedWordEraser_;
};

} // namespace areca
