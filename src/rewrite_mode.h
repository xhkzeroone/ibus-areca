#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "im/im.h"

#include "bamboo_engine_adapter.h"
#include "input_scheduler.h"
#include "mode_handler.h"
#include "sentence_capitalizer.h"
#include "surrounding_text_restore.h"

namespace areca {

struct RewriteInputState final : public im::InputContextProperty {
  explicit RewriteInputState(std::string inputMethod, bool spellCheck,
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
  SentenceCapitalizationState sentenceCapitalization;
  bool backspaceRecoveryAwaitingRelease = false;
  std::set<std::pair<int, uint32_t>> forwardedBackspacePresses;
  // Frontend hoặc lifecycle reset đã thay đổi text/vị trí quanh con trỏ. Cờ
  // one-shot này cho phép phím text kế tiếp thử nạp lại trạng thái đúng một
  // lần.
  bool surroundingRestoreArmed = false;
  // Đoạn text Areca biết chắc đang nằm ngay trước con trỏ, gom từ các lần
  // commit liên tiếp không có phím nào ứng dụng tự xử lý xen giữa. Rỗng nghĩa
  // là không biết.
  std::string knownTextBeforeCursor;
  // Tăng khi ký tự đầu tiên của một từ mới tới sau boundary. Backend dùng số
  // này để chỉ retry surrounding probe ở từ kế tiếp, không retry mỗi ký tự.
  uint32_t wordSerial = 0;
  bool wordBoundaryPending = false;
  std::string resolvedProgram;
  // Một số frontend X11 gửi reset ngay sau Backspace đã được forward. Giữ
  // snapshot Bamboo vừa đồng bộ để phím kế tiếp còn sửa được từ trước đó.
  uint64_t resetProtectedUntilUsec = 0;
};

class RewriteModeHandler final : public InputModeHandler {
public:
  using StateFactory = im::FactoryFor<RewriteInputState>;
  using BoolProvider = std::function<bool()>;
  using ContextBoolProvider = std::function<bool(im::InputContext &)>;
  using MacroBypassMatcher = std::function<bool(const im::Key &)>;
  using BackendVerdictProtector =
      std::function<void(im::InputContext &, const char *)>;
  using BackspaceRecoveryProvider = std::function<bool()>;
  RewriteModeHandler(im::EventLoop &eventLoop, StateFactory &stateFactory,
                     InputScheduler &scheduler,
                     BoolProvider autoCapitalizeProvider,
                     BoolProvider debugProvider,
                     BackendVerdictProtector backendVerdictProtector,
                     BackspaceRecoveryProvider backspaceRecoveryProvider,
                     ContextBoolProvider forwardFirstCharacterProvider,
                     ContextBoolProvider restoreSurroundingTextProvider,
                     MacroBypassMatcher macroBypassMatcher);
  ~RewriteModeHandler();

  RewriteInputState *stateFor(im::InputContext &inputContext) const;
  void activate(im::InputContext &inputContext) override;
  void deactivate(im::InputContext &inputContext) override;
  void handleKeyEvent(im::KeyEvent &event) override;
  void requestProtectedReset(im::InputContext &inputContext) override;
  void resetContext(im::InputContext &inputContext) override;
  // Frontend báo text hoặc con trỏ vừa đổi.
  void onSurroundingTextUpdated(im::InputContext &inputContext);
  // Scheduler báo text trong ứng dụng vừa đổi do Areca.
  void noteCommitted(im::InputContext &inputContext, uint32_t deleteCount,
                     const std::string &text);

private:
  bool syncEngineBackspace(RewriteInputState &state);
  void forwardSyncedBackspace(im::KeyEvent &event, RewriteInputState &state);
  bool shouldRecoverBackspace(im::InputContext &inputContext,
                              RewriteInputState &state) const;
  void deferBackspaceRecoveryUntilRelease(im::KeyEvent &event,
                                          RewriteInputState &state);
  void runDeferredBackspaceRecovery(im::InputContext &inputContext,
                                    im::KeyEvent &event,
                                    RewriteInputState &state);
  void tryRestoreFromSurroundingText(im::InputContext &inputContext,
                                     RewriteInputState &state);

  im::EventLoop &eventLoop_;
  StateFactory &stateFactory_;
  InputScheduler &scheduler_;
  BoolProvider autoCapitalizeProvider_;
  BoolProvider debugProvider_;
  BackendVerdictProtector backendVerdictProtector_;
  BackspaceRecoveryProvider backspaceRecoveryProvider_;
  ContextBoolProvider forwardFirstCharacterProvider_;
  ContextBoolProvider restoreSurroundingTextProvider_;
  MacroBypassMatcher macroBypassMatcher_;
};

} // namespace areca
