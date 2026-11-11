#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "im/im.h"

#include "adaptive_wait_monitor.h"
#include "areca_config.h"
#include "bamboo_engine_adapter.h"
#include "engine_host.h"
#include "event_loop_post.h"
#include "forward_backspace_backend.h"
#include "input_scheduler.h"
#include "input_type_detector.h"
#include "native_device.h"
#include "notification/input_method_announcement.h"
#include "preedit_mode.h"
#include "redirect_mode.h"
#include "rewrite_mode.h"
#include "surrounding_reliability.h"
#include "surrounding_text_backend.h"
#include "surrounding_text_v2_backend.h"
#include "uinput_backspace_backend.h"
#include "uinput_device.h"
#include "uinput_shift_select_backend.h"
#include "xtest_backspace_backend.h"
#include "xtest_shift_select_backend.h"

namespace areca {

struct ReliabilityDecision {
  bool useSurrounding = false;
  bool browserAutocomplete = false;
};

class WindowFocusTracker;
class MouseClickTracker;

// Lõi bộ gõ dùng chung cho mọi input context, giống addon trên fcitx5. Engine
// IBus của từng ô nhập chuyển sự kiện vào đây.
class ArecaEngine final {
public:
  explicit ArecaEngine(EngineHost &host);
  ~ArecaEngine();
  ArecaEngine(const ArecaEngine &) = delete;
  ArecaEngine &operator=(const ArecaEngine &) = delete;

  void keyEvent(im::KeyEvent &event);
  std::string subMode(im::InputContext &inputContext);
  std::string overrideIcon();
  std::string subModeLabel(im::InputContext &inputContext);
  void activate(im::InputContext &inputContext);
  void deactivate(im::InputContext &inputContext);
  void reset(im::InputContext &inputContext);
  // Client vừa gửi surrounding text mới.
  void surroundingTextUpdated(im::InputContext &inputContext);
  // Người dùng vừa chuyển sang bộ gõ Areca.
  void announceSwitchedIn();
  im::UserInterfaceManager &userInterfaceManager() {
    return userInterfaceManager_;
  }

  const im::Configuration *getConfig() const;
  const im::Configuration *getSubConfig(const std::string &path) const;
  void setConfig(const im::RawConfig &config);
  void setSubConfig(const std::string &path, const im::RawConfig &config);
  void reloadConfig();
  void save();
  std::string configPath(const char *relativePath) const;

private:
  InputModeHandler &handlerFor(im::InputContext &inputContext);
  PresentationMode effectivePresentationMode(im::InputContext &inputContext);
  static const char *presentationModeName(PresentationMode mode);
  void switchPresentationMode(im::InputContext &inputContext);
  SchedulerTiming timing(im::InputContext &inputContext);
  bool debugEnabled() const { return config_.debug.value(); }
  RewriteBackendSelection selectRewriteBackend(im::InputContext &inputContext,
                                               const BambooResult &result);
  RewriteBackend *resolvePreferredSurroundingBackend();
  void handleSurroundingProbeSucceeded(im::InputContext &inputContext);
  void handleSurroundingProbeFailed(im::InputContext &inputContext);
  RewriteBackend *
  resolveSurroundingFallbackBackend(im::InputContext &inputContext);
  RewriteBackend *resolvePreferredShiftSelectBackend();
  RewriteBackend *resolvePreferredBackspaceBackend(bool preferHardware = false);
  std::string describeAutoBackend(const std::string &appName);
  std::string describeSurroundingBackend();
  std::string describeShiftSelectBackend();
  std::string describeBackspaceBackend(bool preferHardware = false);
  ReliabilityDecision evaluateReliability(im::InputContext &inputContext,
                                          const std::string &shownText,
                                          const std::string &program);
  std::string resolveProgram(im::InputContext &inputContext,
                             RewriteInputState *state);
  void protectBackendVerdict(im::InputContext &inputContext,
                             const char *reason);
  void clearBackendVerdictForLifecycle(im::InputContext &inputContext,
                                       const char *eventName);
  bool backendVerdictProtected(im::InputContext &inputContext) const;
  // Rewrite chỉ restore từ surrounding khi app đang dùng backend surrounding:
  // app phải xoá bằng phím thì snapshot của nó cũng không đáng tin.
  bool surroundingBackendInUse(im::InputContext &inputContext);
  void noteRewriteBackendChoice(im::InputContext &inputContext,
                                const RewriteBackend *backend);
  bool backspaceRecoveryEnabled() const;
  bool
  eraseFinalizedPreeditWord(im::InputContext &inputContext,
                            uint32_t backspaceCount,
                            PreeditModeHandler::FinalizedWordEraseDone onDone);
  void notifyUinputUnavailable();
  void scheduleDeviceWarmup();
  void applyConfig();
  void readConfigFile(im::Configuration &config, const char *relativePath);
  void saveConfigFile(const im::Configuration &config,
                      const char *relativePath) const;
  std::vector<MacroDefinition> macroDefinitions() const;

  void showBackendSelectionMenu(im::InputContext &inputContext);
  void selectBackendForApp(im::InputContext &inputContext, std::string appName,
                           AppBackendMode mode);
  bool isBackendSelecting(im::InputContext &inputContext) const;
  bool handleBackendSelectionKey(im::InputContext &inputContext,
                                 im::KeyEvent &event);

  void initActions();
  void updateUI(im::InputContext *ic);
  void updateInputMethodAction(im::InputContext *ic);
  void updateCharsetAction(im::InputContext *ic);
  void updatePresentationModeAction(im::InputContext *ic);
  void updateSpellcheckAction(im::InputContext *ic);
  void updateModernStyleAction(im::InputContext *ic);
  void updateMacroAction(im::InputContext *ic);
  void updateAutoCapitalizeAction(im::InputContext *ic);

  EngineHost &host_;
  im::UserInterfaceManager userInterfaceManager_;
  ArecaConfig config_;
  AdvancedConfig advancedConfig_;
  MacroTableConfig macroTable_;
  AppProfilesConfig appProfiles_;
  uint64_t macroRevision_ = 1;
  uint64_t nextPreeditRecoveryTransactionId_ = 1;
  std::unordered_map<std::string, AppBackendMode> appBackendOverridesMap_;
  std::unordered_map<std::string, AppBackendMode> lastAutoBackendByApp_;
  // Backend rewrite gần nhất của mỗi app có phải surrounding không. Không xoá ở
  // reset để restore sau click vẫn biết app có dùng surrounding.
  std::unordered_map<std::string, bool> surroundingBackendByApp_;
  void rebuildAppBackendOverridesMap();
  PresentationMode activePresentationMode_ = PresentationMode::Rewrite;
  im::FactoryFor<RewriteInputState> rewriteStateFactory_;
  im::FactoryFor<PreeditInputState> preeditStateFactory_;
  bool backendVerdictContextKnown_ = false;
  im::ICUUID backendVerdictContextId_{};
  SurroundingReliabilityState backendVerdict_;
  uint64_t backendVerdictProtectedUntil_ = 0;
  // Một trạng thái adaptive dùng chung để khi đổi giữa forward và uinput,
  // thông tin máy đang chậm không bị mất.
  AdaptiveWait adaptiveWait_;
  // Probe toàn cục chỉ cung cấp tín hiệu cho adaptiveWait_.
  AdaptiveWaitMonitor adaptiveWaitMonitor_;
  SurroundingTextBackend surroundingBackend_;
  SurroundingTextV2Backend surroundingV2Backend_;
  ForwardBackspaceBackend forwardBackspaceBackend_;
  UinputDevice uinputDevice_;
  UinputBackspaceBackend uinputBackspaceBackend_;
  UinputShiftSelectBackend uinputShiftSelectBackend_;
  NativeDevice nativeDevice_;
  XTestBackspaceBackend xtestBackspaceBackend_;
  XTestShiftSelectBackend xtestShiftSelectBackend_;
  std::unique_ptr<im::EventSourceTime> deviceWarmupTimer_;
  EventLoopPostTask backendSelectionPost_;
  InputScheduler scheduler_;
  RewriteModeHandler rewriteHandler_;
  PreeditModeHandler preeditHandler_;
  RedirectModeHandler redirectHandler_;
  std::unique_ptr<WindowFocusTracker> focusTracker_;
  InputTypeDetector inputTypeDetector_;
  std::unique_ptr<MouseClickTracker> mouseTracker_;
  std::unique_ptr<InputMethodAnnouncement> inputMethodAnnouncement_;

  std::unique_ptr<im::SimpleAction> inputMethodAction_;
  std::vector<std::unique_ptr<im::SimpleAction>> inputMethodSubActions_;
  std::vector<std::string> inputMethodNames_;
  std::unique_ptr<im::Menu> inputMethodMenu_;

  std::unique_ptr<im::SimpleAction> charsetAction_;
  std::vector<std::unique_ptr<im::SimpleAction>> charsetSubActions_;
  std::vector<std::string> charsetNames_;
  std::unique_ptr<im::Menu> charsetMenu_;

  std::unique_ptr<im::SimpleAction> presentationModeAction_;
  std::vector<std::unique_ptr<im::SimpleAction>> presentationModeSubActions_;
  std::unique_ptr<im::Menu> presentationModeMenu_;

  std::unique_ptr<im::SimpleAction> spellcheckAction_;
  std::vector<std::unique_ptr<im::SimpleAction>> spellcheckSubActions_;
  std::unique_ptr<im::Menu> spellcheckMenu_;

  std::unique_ptr<im::SimpleAction> modernStyleAction_;
  std::unique_ptr<im::SimpleAction> macroAction_;
  std::unique_ptr<im::SimpleAction> autoCapitalizeAction_;
  std::unique_ptr<im::SimpleAction> settingsAction_;

  std::vector<im::ScopedConnection> actionConnections_;
};

} // namespace areca
