#include "areca_engine.h"

#include <algorithm>
#include <array>

#include "im/im.h"
#include "program_compatibility.h"
#include "surrounding_probe_match.h"

#include "mouse_click_tracker.h"
#include "window_focus_tracker.h"

namespace areca {

namespace {

constexpr uint64_t kBackendVerdictProtectionUsec = 1ULL * 1000 * 1000;

using SurroundingRule = bool (*)(const im::SurroundingText &);

constexpr std::array<SurroundingRule, 3> kInvalidSurroundingRules = {
    [](const auto &s) { return s.cursor() <= 0; },
    [](const auto &s) { return s.anchor() <= 0; },
    [](const auto &s) { return s.text().empty(); }};

bool isInvalidSurrounding(const im::SurroundingText &surrounding) {
  return std::any_of(kInvalidSurroundingRules.begin(),
                     kInvalidSurroundingRules.end(),
                     [&](auto rule) { return rule(surrounding); });
}

bool isGnomeShellProxyProgram(std::string_view program) {
  return program == "gnome-shell" || program == "mutter" ||
         program == "org.gnome.Shell" || program == "org.gnome.Mutter";
}

} // namespace

void ArecaEngine::protectBackendVerdict(im::InputContext &inputContext,
                                        const char *reason) {
  if (!backendVerdictContextKnown_ ||
      backendVerdictContextId_ != inputContext.uuid() ||
      !backendVerdict_.known) {
    return;
  }
  backendVerdictProtectedUntil_ =
      im::now(CLOCK_MONOTONIC) + kBackendVerdictProtectionUsec;
  if (debugEnabled()) {
    ARECA_INFO() << "areca: backend verdict protection armed"
                 << " reason=" << reason << " duration_ms=1000"
                 << " program=" << inputContext.program();
  }
}

bool ArecaEngine::surroundingBackendInUse(im::InputContext &inputContext) {
  const auto program = resolveProgram(
      inputContext, inputContext.propertyFor(&rewriteStateFactory_));
  const auto it = surroundingBackendByApp_.find(program);
  // Chưa từng chọn backend cho app này thì chưa có lý do để tắt restore.
  return it == surroundingBackendByApp_.end() || it->second;
}

void ArecaEngine::noteRewriteBackendChoice(im::InputContext &inputContext,
                                           const RewriteBackend *backend) {
  const auto program = resolveProgram(
      inputContext, inputContext.propertyFor(&rewriteStateFactory_));
  if (program.empty()) {
    return;
  }
  surroundingBackendByApp_[program] = backend == &surroundingBackend_;
}

bool ArecaEngine::backendVerdictProtected(
    im::InputContext &inputContext) const {
  return backendVerdictContextKnown_ && backendVerdict_.known &&
         backendVerdictContextId_ == inputContext.uuid() &&
         backendVerdictProtectedUntil_ > im::now(CLOCK_MONOTONIC);
}

void ArecaEngine::clearBackendVerdictForLifecycle(
    im::InputContext &inputContext, const char *eventName) {
  if (!backendVerdictContextKnown_ ||
      backendVerdictContextId_ != inputContext.uuid()) {
    return;
  }
  if (std::string_view(eventName) != "reset" &&
      backendVerdictProtected(inputContext)) {
    if (debugEnabled()) {
      ARECA_INFO() << "areca: backend verdict cache preserved"
                   << " event=" << eventName
                   << " program=" << inputContext.program();
    }
    return;
  }
  backendVerdict_.reset();
  backendVerdictProtectedUntil_ = 0;
  if (std::string_view(eventName) == "reset") {
    const std::string prog = resolveProgram(
        inputContext, inputContext.propertyFor(&rewriteStateFactory_));
    if (!prog.empty()) {
      lastAutoBackendByApp_.erase(prog);
    }
  }
  if (debugEnabled()) {
    ARECA_INFO() << "areca: backend verdict cache cleared"
                 << " event=" << eventName
                 << " program=" << inputContext.program();
  }
}

std::string ArecaEngine::resolveProgram(im::InputContext &inputContext,
                                        RewriteInputState *state) {
  const std::string reportedProgram = inputContext.program();
  const bool needsTrackedProgram =
      reportedProgram.empty() || isGnomeShellProxyProgram(reportedProgram);
  if (!needsTrackedProgram) {
    return reportedProgram;
  }

  if (focusTracker_ && focusTracker_->isValid()) {
    const std::string trackedProgram = focusTracker_->focusProgram();
    if (!trackedProgram.empty() && !isGnomeShellProxyProgram(trackedProgram)) {
      if (state) {
        state->resolvedProgram = trackedProgram;
      }
      if (debugEnabled()) {
        ARECA_INFO()
            << "areca: resolved and cached proxy program via focus tracker"
            << " reported=" << reportedProgram << " tracked=" << trackedProgram;
      }
      return trackedProgram;
    }
  }

  if (state && !state->resolvedProgram.empty()) {
    return state->resolvedProgram;
  }
  return reportedProgram;
}

RewriteBackend *ArecaEngine::resolvePreferredSurroundingBackend() {
  return &surroundingBackend_;
}

void ArecaEngine::handleSurroundingProbeSucceeded(
    im::InputContext &inputContext) {
  backendVerdict_.probePending = false;
  backendVerdict_.retryOnNextWord = false;
  if (debugEnabled()) {
    ARECA_INFO() << "areca: surrounding probe succeeded program="
                 << inputContext.program();
  }
}

void ArecaEngine::handleSurroundingProbeFailed(im::InputContext &inputContext) {
  auto *state = inputContext.propertyFor(&rewriteStateFactory_);
  const uint32_t wordSerial = state ? state->wordSerial : 0;
  backendVerdict_.probeFailed(wordSerial);
  // Transaction này đã hoàn tất bằng backend dự phòng.
  noteRewriteBackendChoice(inputContext, nullptr);
  if (debugEnabled()) {
    ARECA_INFO() << "areca: surrounding probe failed, marked unreliable"
                 << " attempt=" << backendVerdict_.probeAttempts
                 << " retry_next_word=" << backendVerdict_.retryOnNextWord
                 << " word=" << wordSerial
                 << " program=" << inputContext.program();
  }
}

RewriteBackend *
ArecaEngine::resolveSurroundingFallbackBackend(im::InputContext &inputContext) {
  const std::string prog = resolveProgram(
      inputContext, inputContext.propertyFor(&rewriteStateFactory_));
  const bool isTerminal = inputTypeDetector_.isTerminal(prog);
  const bool canForward =
      inputTypeDetector_.canForwardBackspaces(inputContext, prog);
  return resolvePreferredBackspaceBackend(isTerminal || !canForward);
}

RewriteBackend *ArecaEngine::resolvePreferredShiftSelectBackend() {
  if (advancedConfig_.useNativeInsteadOfUinput.value() &&
      nativeShiftSelectBackend_.isAvailable()) {
    return &nativeShiftSelectBackend_;
  }
  if (uinputShiftSelectBackend_.isAvailable()) {
    return &uinputShiftSelectBackend_;
  }
  if (nativeShiftSelectBackend_.isAvailable()) {
    return &nativeShiftSelectBackend_;
  }
  return nullptr;
}

RewriteBackend *
ArecaEngine::resolvePreferredBackspaceBackend(bool preferHardware) {
  if (preferHardware || advancedConfig_.useUinputInsteadOfForwardKey.value()) {
    if (advancedConfig_.useNativeInsteadOfUinput.value() &&
        nativeBackspaceBackend_.isAvailable()) {
      return &nativeBackspaceBackend_;
    }
    if (uinputBackspaceBackend_.isAvailable()) {
      return &uinputBackspaceBackend_;
    }
    if (nativeBackspaceBackend_.isAvailable()) {
      return &nativeBackspaceBackend_;
    }
  }
  return &forwardBackspaceBackend_;
}

std::string ArecaEngine::describeAutoBackend(const std::string &appName) {
  auto it = lastAutoBackendByApp_.find(appName);
  if (it != lastAutoBackendByApp_.end()) {
    switch (it->second) {
    case AppBackendMode::SurroundingText:
      return "Tự động (Surrounding)";
    case AppBackendMode::Backspace:
      return "Tự động (Backspace)";
    case AppBackendMode::Preedit:
    case AppBackendMode::Redirect:
      break;
    default:
      break;
    }
  }
  return "Tự động";
}

// Mô tả đi theo đúng thứ tự của bộ chọn nhưng hỏi Native bằng isSupported():
// isAvailable() của Native mở phiên libei, chỉ routing thật mới được gọi.
std::string ArecaEngine::describeShiftSelectBackend() {
  const bool native = nativeDevice_.isSupported();
  if (advancedConfig_.useNativeInsteadOfUinput.value() && native) {
    return "Native";
  }
  if (uinputShiftSelectBackend_.isAvailable()) {
    return "Uinput";
  }
  if (native) {
    return "Native";
  }
  return advancedConfig_.useNativeInsteadOfUinput.value() ? "Native" : "Uinput";
}

std::string ArecaEngine::describeBackspaceBackend(bool preferHardware) {
  if (preferHardware || advancedConfig_.useUinputInsteadOfForwardKey.value()) {
    const bool native = nativeDevice_.isSupported();
    if (advancedConfig_.useNativeInsteadOfUinput.value() && native) {
      return "Native";
    }
    if (uinputBackspaceBackend_.isAvailable()) {
      return "Uinput";
    }
    if (native) {
      return "Native";
    }
  }
  return "ForwardKey";
}

RewriteBackendSelection
ArecaEngine::selectRewriteBackend(im::InputContext &inputContext,
                                  const BambooResult &result) {
  auto *state = inputContext.propertyFor(&rewriteStateFactory_);
  if (!state) {
    return {resolvePreferredBackspaceBackend(false)};
  }

  const std::string program = resolveProgram(inputContext, state);

  const bool isTerminal = inputTypeDetector_.isTerminal(program);
  const bool canForward =
      inputTypeDetector_.canForwardBackspaces(inputContext, program);
  const bool preferHardware = isTerminal || !canForward;

  if (!backendVerdictContextKnown_ ||
      backendVerdictContextId_ != inputContext.uuid()) {
    backendVerdictContextKnown_ = true;
    backendVerdictContextId_ = inputContext.uuid();
    backendVerdict_.reset();
    backendVerdictProtectedUntil_ = 0;
    if (debugEnabled()) {
      ARECA_INFO() << "areca: backend verdict cache switched context"
                   << " program=" << inputContext.program();
    }
  }

  if (isTerminal) {
    RewriteBackend *backend = resolvePreferredBackspaceBackend(true);
    if (!program.empty()) {
      lastAutoBackendByApp_[program] = AppBackendMode::Backspace;
    }
    if (debugEnabled()) {
      ARECA_INFO() << "areca: terminal selected backend=" << backend->name()
                   << " program=" << program;
    }
    return {backend};
  }

  // Gợi ý tự hoàn thành và vùng chọn của trình duyệt đã bị marker "·" mà
  // scheduler commit trước khi xoá thay mất, nên ở đây không còn nhánh riêng
  // cho thanh địa chỉ hay ô URL.
  const auto decision =
      evaluateReliability(inputContext, result.currentText, program);

  if (!program.empty()) {
    auto it = appBackendOverridesMap_.find(program);
    if (it != appBackendOverridesMap_.end() &&
        it->second != AppBackendMode::Auto) {
      switch (it->second) {
      case AppBackendMode::SurroundingText:
        if (decision.useSurrounding) {
          auto *surBackend = resolvePreferredSurroundingBackend();
          if (backendVerdict_.probePending) {
            backendVerdict_.probeStarted();
          }
          const uint32_t probeTimeoutMs =
              backendVerdict_.probePending
                  ? backendVerdict_.activeProbeTimeoutMs()
                  : 0;
          if (debugEnabled()) {
            ARECA_INFO() << "areca: app-override selected surrounding backend="
                         << surBackend->name() << " program=" << program
                         << " probe_timeout_ms=" << probeTimeoutMs;
          }
          return {surBackend, backendVerdict_.probePending, probeTimeoutMs};
        }
        return {resolvePreferredBackspaceBackend(preferHardware)};
      case AppBackendMode::ShiftSelect:
        if (!isTerminal) {
          if (auto *shiftBackend = resolvePreferredShiftSelectBackend()) {
            if (debugEnabled()) {
              ARECA_INFO()
                  << "areca: app-override selected shift-select backend="
                  << shiftBackend->name() << " program=" << program;
            }
            return {shiftBackend};
          }
        }
        return {resolvePreferredBackspaceBackend(preferHardware)};
      case AppBackendMode::Backspace: {
        auto *bsBackend = resolvePreferredBackspaceBackend(preferHardware);
        if (debugEnabled()) {
          ARECA_INFO() << "areca: app-override selected backspace backend="
                       << bsBackend->name() << " program=" << program;
        }
        return {bsBackend};
      }
      case AppBackendMode::Preedit:
      case AppBackendMode::Redirect:
        // Mode routing happens before key handling, so these entries must not
        // normally reach the rewrite scheduler. Fall through to auto as a
        // safe fallback if an already queued transaction outlives a switch.
        break;
      case AppBackendMode::Auto:
        break;
      }
    }
  }

  if (decision.useSurrounding) {
    auto *surBackend = resolvePreferredSurroundingBackend();
    if (backendVerdict_.probePending) {
      backendVerdict_.probeStarted();
    }
    const uint32_t probeTimeoutMs = backendVerdict_.probePending
                                        ? backendVerdict_.activeProbeTimeoutMs()
                                        : 0;
    if (!program.empty()) {
      lastAutoBackendByApp_[program] = AppBackendMode::SurroundingText;
    }
    if (debugEnabled()) {
      ARECA_INFO() << "areca: auto selected surrounding backend="
                   << surBackend->name() << " program=" << program
                   << " probe=" << backendVerdict_.probePending
                   << " probe_timeout_ms=" << probeTimeoutMs;
    }
    return {surBackend, backendVerdict_.probePending, probeTimeoutMs};
  }

  if (!program.empty()) {
    lastAutoBackendByApp_[program] = AppBackendMode::Backspace;
  }
  return {resolvePreferredBackspaceBackend(preferHardware)};
}

ReliabilityDecision
ArecaEngine::evaluateReliability(im::InputContext &inputContext,
                                 const std::string &shownText,
                                 const std::string &program) {
  const auto &surrounding = inputContext.surroundingText();
  const std::string &programName =
      !program.empty() ? program : inputContext.program();

  // Bắt đầu: Probe lại đúng một lần khi bước sang từ kế tiếp
  auto *state = inputContext.propertyFor(&rewriteStateFactory_);
  if (state && backendVerdict_.retryForWord(state->wordSerial) &&
      debugEnabled()) {
    ARECA_INFO() << "areca: surrounding probe retry armed"
                 << " attempt=" << (backendVerdict_.probeAttempts + 1)
                 << " word=" << state->wordSerial << " program=" << programName;
  }
  // Kết thúc: Probe lại đúng một lần khi bước sang từ kế tiếp

  if (!backendVerdict_.known) {
    backendVerdict_.reliable = !isInvalidSurrounding(surrounding);
    backendVerdict_.probePending = backendVerdict_.reliable;
    if (debugEnabled()) {
      ARECA_INFO() << "areca: reliability first-probe text="
                   << surrounding.text() << " cursor=" << surrounding.cursor()
                   << " anchor=" << surrounding.anchor()
                   << " reliable=" << backendVerdict_.reliable
                   << " probe_pending=" << backendVerdict_.probePending;
    }

    backendVerdict_.forceForwardBackspace = false;
    if (isProgrammingProgram(programName) || isTerminalProgram(programName) ||
        isOfficeProgram(programName)) {
      backendVerdict_.forceForwardBackspace = true;
      if (debugEnabled()) {
        ARECA_INFO() << "areca: reliability first-probe force_forward=1"
                     << " reason=programming-terminal-or-office-program"
                     << " program=" << programName;
      }
    } else if (backendVerdict_.reliable && isVSCodeFamilyProgram(programName)) {
      const uint64_t capabilityMask =
          inputContext.capabilityFlags().toInteger();
      backendVerdict_.forceForwardBackspace = (capabilityMask == 0x72);
      if (backendVerdict_.forceForwardBackspace && debugEnabled()) {
        ARECA_INFO() << "areca: reliability first-probe force_forward=1"
                     << " reason=program-compatibility-capability-mask-0x72"
                     << " program=" << programName;
      }
    }
    backendVerdict_.known = true;
  }

  // Lúc thăm dò, snapshot phải chứa từ đang hiện trong ô. Frontend trễ một nhịp
  // (báo "g" khi ô đã là "go") khiến probe tính sai kết quả mong đợi rồi nhận
  // nhầm update cũ là xác nhận xoá. Khi đó coi probe thất bại: dùng
  // Backspace/Shift+Left và thử lại ở từ kế tiếp.
  const bool staleDuringProbe =
      backendVerdict_.probePending && backendVerdict_.reliable &&
      surrounding.cursor() == surrounding.anchor() &&
      !surroundingSnapshotEndsWith(surrounding.isValid(), surrounding.text(),
                                   surrounding.cursor(), surrounding.anchor(),
                                   shownText);
  if (staleDuringProbe) {
    // Tính là một lần probe để chỉ retry đúng một lần như probe timeout.
    backendVerdict_.probeStarted();
    backendVerdict_.probeFailed(state ? state->wordSerial : 0);
    if (debugEnabled()) {
      ARECA_INFO() << "areca: surrounding snapshot stale during probe text="
                   << surrounding.text() << " cursor=" << surrounding.cursor()
                   << " anchor=" << surrounding.anchor()
                   << " word=" << shownText << " marked_unreliable=1"
                   << " attempt=" << backendVerdict_.probeAttempts
                   << " retry_next_word=" << backendVerdict_.retryOnNextWord
                   << " program=" << programName;
    }
  }

  if (debugEnabled()) {
    ARECA_INFO() << "areca: reliability cached known=" << backendVerdict_.known
                 << " reliable=" << backendVerdict_.reliable
                 << " force_forward=" << backendVerdict_.forceForwardBackspace
                 << " program=" << programName;
  }

  ReliabilityDecision decision;
  decision.useSurrounding = backendVerdict_.known && backendVerdict_.reliable &&
                            !backendVerdict_.forceForwardBackspace;
  return decision;
}

bool ArecaEngine::backspaceRecoveryEnabled() const {
  return config_.backspaceRecovery.value();
}

} // namespace areca
