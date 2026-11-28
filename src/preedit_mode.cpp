#include "preedit_mode.h"

#include <exception>
#include <string_view>
#include <utility>

#include "im/im.h"

#include "preedit_logic.h"

namespace areca {
namespace {

constexpr uint64_t kPostCommitResetProtectionUsec = 20 * 1000;

im::KeySym normalizePreeditKeypadSym(im::KeySym sym) {
  if (sym >= im::Key_KP_0 && sym <= im::Key_KP_9) {
    return static_cast<im::KeySym>(im::Key_0 + (sym - im::Key_KP_0));
  }
  switch (sym) {
  case im::Key_KP_Add:
    return im::Key_plus;
  case im::Key_KP_Subtract:
    return im::Key_minus;
  case im::Key_KP_Divide:
    return im::Key_slash;
  case im::Key_KP_Multiply:
    return im::Key_asterisk;
  case im::Key_KP_Decimal:
    return im::Key_period;
  case im::Key_KP_Enter:
    return im::Key_Return;
  case im::Key_KP_Equal:
    return im::Key_equal;
  case im::Key_KP_Space:
    return im::Key_space;
  default:
    return sym;
  }
}

bool hasPreeditShortcutModifier(const im::Key &key) {
  const auto states = key.states();
  return states.test(im::KeyState::Ctrl) || states.test(im::KeyState::Alt) ||
         states.test(im::KeyState::Super) || states.test(im::KeyState::Meta) ||
         states.test(im::KeyState::Hyper) ||
         states.test(im::KeyState::Super2) || states.test(im::KeyState::Hyper2);
}

} // namespace

PreeditInputState::PreeditInputState(std::string inputMethod, bool spellCheck,
                                     bool realtimeSpellcheck, bool modernStyle,
                                     std::string outputCharset,
                                     bool macroEnabled, bool capitalizeMacro,
                                     uint64_t macroRevision,
                                     std::vector<MacroDefinition> macros,
                                     bool revertMacroOnBackspace)
    : inputMethod(std::move(inputMethod)), spellCheck(spellCheck),
      realtimeSpellcheck(realtimeSpellcheck), modernStyle(modernStyle),
      outputCharset(std::move(outputCharset)), macroEnabled(macroEnabled),
      capitalizeMacro(capitalizeMacro),
      revertMacroOnBackspace(revertMacroOnBackspace),
      macroRevision(macroRevision),
      engine(std::make_unique<BambooEngineAdapter>(
          this->inputMethod, this->spellCheck, this->realtimeSpellcheck,
          this->modernStyle, this->outputCharset, this->macroEnabled,
          this->capitalizeMacro, std::move(macros),
          this->revertMacroOnBackspace)) {}

PreeditModeHandler::PreeditModeHandler(
    StateFactory &stateFactory, DebugProvider debugProvider,
    AutoCapitalizeProvider autoCapitalizeProvider,
    BackspaceRecoveryProvider backspaceRecoveryProvider,
    MacroBypassMatcher macroBypassMatcher,
    FinalizedWordEraser finalizedWordEraser)
    : stateFactory_(stateFactory), debugProvider_(std::move(debugProvider)),
      autoCapitalizeProvider_(std::move(autoCapitalizeProvider)),
      backspaceRecoveryProvider_(std::move(backspaceRecoveryProvider)),
      macroBypassMatcher_(std::move(macroBypassMatcher)),
      finalizedWordEraser_(std::move(finalizedWordEraser)) {}

PreeditInputState *
PreeditModeHandler::stateFor(im::InputContext &inputContext) const {
  return inputContext.propertyFor(&stateFactory_);
}

void PreeditModeHandler::activate(im::InputContext &inputContext) {
  // Bắt đầu: Giữ finalized-word recovery qua lifecycle giả của Edge X11
  if (auto *state = stateFor(inputContext);
      state &&
      state->finalizedWordRecovery == FinalizedWordRecoveryState::Erasing) {
    if (debugProvider_()) {
      ARECA_INFO() << "areca: preedit activate reset rejected"
                   << " reason=finalized-word-recovery";
    }
    return;
  }
  // Kết thúc: Giữ finalized-word recovery qua lifecycle giả của Edge X11
  resetContext(inputContext);
}

void PreeditModeHandler::deactivate(im::InputContext &inputContext) {
  if (auto *state = stateFor(inputContext)) {
    // Bắt đầu: Giữ finalized-word recovery qua lifecycle giả của Edge X11
    if (state->finalizedWordRecovery == FinalizedWordRecoveryState::Erasing) {
      if (debugProvider_()) {
        ARECA_INFO() << "areca: preedit deactivate rejected"
                     << " reason=finalized-word-recovery";
      }
      return;
    }
    // Kết thúc: Giữ finalized-word recovery qua lifecycle giả của Edge X11
    commitComposition(inputContext, *state);
    state->sentenceCapitalization.reset();
  }
}

void PreeditModeHandler::handleKeyEvent(im::KeyEvent &event) {
  auto *inputContext = event.inputContext();
  if (!inputContext) {
    return;
  }
  const auto rawKey = event.rawKey();
  auto *state = stateFor(*inputContext);
  if (!state || !state->engine) {
    event.forward();
    return;
  }
  const auto key = event.key();
  const bool isBackspace =
      key.check(im::Key_BackSpace) || rawKey.check(im::Key_BackSpace);
  const bool isRepeat = key.states().test(im::KeyState::Repeat) ||
                        rawKey.states().test(im::KeyState::Repeat);
  if (event.isRelease()) {
    if (isBackspace &&
        state->finalizedWordRecovery ==
            FinalizedWordRecoveryState::AwaitingBackspaceRelease) {
      if (isRepeat) {
        event.filterAndAccept();
      } else {
        state->finalizedWordRecovery = FinalizedWordRecoveryState::Idle;
        event.filterAndAccept();
        if (!startFinalizedWordRecovery(*inputContext, *state)) {
          state->engine->reset();
        }
      }
    }
    return;
  }
  if (rawKey.isModifier()) {
    return;
  }
  // Backspace do backend bơm vào phải tiếp tục đi tới ứng dụng, nhưng không
  // được làm thay đổi state Bamboo trong lúc từ cũ đang được xóa.
  if (state->finalizedWordRecovery == FinalizedWordRecoveryState::Erasing) {
    event.forward();
    return;
  }

  if (inputContext->capabilityFlags().test(im::CapabilityFlag::Password)) {
    resetContext(*inputContext);
    event.forward();
    return;
  }

  const auto normalizedKey = key.normalize();
  const auto rawSym = rawKey.sym();
  const auto textSym = normalizePreeditKeypadSym(normalizedKey.sym());
  const bool suppressMacro = !state->engine->currentText().empty() &&
                             macroBypassMatcher_(normalizedKey);
  const bool isEnter = textSym == im::Key_Return || rawSym == im::Key_Return ||
                       rawSym == im::Key_KP_Enter;

  // Modified and unsupported keys are not rebuilt. Commit the current Bamboo
  // preedit, then let IBus pass the original event on with its exact
  // modifiers.
  if (!suppressMacro && hasPreeditShortcutModifier(normalizedKey)) {
    commitComposition(*inputContext, *state);
    state->sentenceCapitalization.reset();
    event.forward();
    return;
  }

  if (isBackspace) {
    // IBus không gửi cờ Repeat; engine IBus tự đặt cờ này cho press đến khi
    // phím chưa nhả, kể cả lúc đang chờ khôi phục từ.
    state->sentenceCapitalization.reset();
    if (isRepeat) {
      if (debugProvider_()) {
        ARECA_INFO() << "areca: preedit backspace route=forward"
                     << " reason=repeat-reset-composition"
                     << " repeat=" << isRepeat;
      }
      // Giữ nguyên phần chữ người dùng đang thấy, nhưng bỏ toàn bộ trạng thái
      // Bamboo để chuỗi Backspace repeat tiếp tục xóa trực tiếp trong app.
      if (state->composing.empty()) {
        clearComposition(*inputContext, *state);
      } else {
        commitComposition(*inputContext, *state);
      }
      state->finalizedWordRecovery = FinalizedWordRecoveryState::Idle;
      event.forward();
      return;
    }
    if (state->engine && state->engine->canUndoMacro()) {
      try {
        const auto result = state->engine->processBackspace();
        auto &surrounding = inputContext->surroundingText();
        if (surrounding.isValid() && result.deleteCount > 0) {
          inputContext->deleteSurroundingText(
              -static_cast<int>(result.deleteCount), result.deleteCount);
          if (!result.commitText.empty()) {
            inputContext->commitString(result.commitText);
          }
          event.filterAndAccept();
          return;
        }
      } catch (const std::exception &error) {
        ARECA_ERROR() << "areca: preedit macro undo failed: " << error.what();
      }
    }
    if (state->composing.empty()) {
      if (state->finalizedWordRecovery ==
          FinalizedWordRecoveryState::AwaitingBackspaceRelease) {
        if (isRepeat) {
          // Nhấn giữ Backspace có nghĩa là người dùng muốn xóa liên tục, không
          // mở lại từ trong preedit. Hủy snapshot và để repeat đi xuống app.
          state->finalizedWordRecovery = FinalizedWordRecoveryState::Idle;
          state->engine->reset();
          event.forward();
        } else {
          event.filterAndAccept();
        }
        return;
      }
      try {
        // Adapter đếm các boundary đã commit. Backspace qua boundary trung gian
        // chỉ được forward; khi chạm tới snapshot từ, handler kéo từ lên
        // preedit.
        state->engine->backspace();
        if (state->engine->currentText().empty()) {
          event.forward();
        } else if (im::utf8::validate(state->engine->currentText())) {
          state->finalizedWordRecovery =
              FinalizedWordRecoveryState::AwaitingBackspaceRelease;
          event.filterAndAccept();
        } else {
          state->engine->reset();
          event.forward();
        }
      } catch (const std::exception &error) {
        ARECA_ERROR() << "areca: preedit finalized-word Backspace failed: "
                      << error.what();
        clearComposition(*inputContext, *state);
        event.forward();
      }
      return;
    }
    try {
      if (backspaceRecoveryProvider_()) {
        // Recovery tái chạy chuỗi phím Latin còn lại qua Bamboo sau khi xóa;
        // đây là nhánh biến `nhanhsh` + Backspace thành `nhánh`.
        state->composing = state->engine->processBackspace().newText;
      } else {
        state->engine->backspace();
        state->composing = state->engine->currentText();
      }
      updatePreedit(*inputContext, *state);
      event.filterAndAccept();
    } catch (const std::exception &error) {
      ARECA_ERROR() << "areca: preedit Backspace failed: " << error.what();
      clearComposition(*inputContext, *state);
      event.forward();
    }
    return;
  }

  const bool commitAndForward =
      key.isCursorMove() || textSym == im::Key_Delete ||
      rawSym == im::Key_Delete || rawSym == im::Key_Tab ||
      rawSym == im::Key_KP_Tab || rawSym == im::Key_ISO_Left_Tab ||
      rawSym == im::Key_Escape || isEnter;
  if (commitAndForward) {
    commitComposition(*inputContext, *state);
    state->sentenceCapitalization.reset();
    event.forward();
    return;
  }

  auto effectiveTextSym = textSym;
  if (autoCapitalizeProvider_()) {
    effectiveTextSym = capitalizeAfterSentenceBoundary(
        state->sentenceCapitalization, effectiveTextSym);
  } else {
    state->sentenceCapitalization.reset();
  }
  const uint32_t codepoint = im::Key::keySymToUnicode(effectiveTextSym);
  const auto utf8Text = im::Key::keySymToUTF8(effectiveTextSym);
  if (!codepoint || utf8Text.empty()) {
    commitComposition(*inputContext, *state);
    state->sentenceCapitalization.reset();
    event.forward();
    return;
  }

  try {
    if (suppressMacro && debugProvider_()) {
      ARECA_INFO() << "areca: preedit macro expansion bypassed";
    }
    const auto result =
        suppressMacro ? state->engine->processWithoutMacro(codepoint, utf8Text)
                      : state->engine->process(codepoint, utf8Text);
    if (!result.newText.empty()) {
      state->composing = result.newText;
      updatePreedit(*inputContext, *state);
      event.filterAndAccept();
      if (debugProvider_()) {
        ARECA_INFO() << "areca: preedit update text=" << state->composing;
      }
      return;
    }

    const auto committed = buildPreeditCommit(result);
    const bool nativeBoundary =
        result.currentText.empty() && committed == utf8Text;
    if (nativeBoundary) {
      state->composing.clear();
      updatePreedit(*inputContext, *state);
      event.forward();
      return;
    }

    if (!committed.empty()) {
      state->resetProtectedUntilUsec =
          im::now(CLOCK_MONOTONIC) + kPostCommitResetProtectionUsec;
      inputContext->commitString(committed);
    }
    // Không reset ở boundary: adapter giữ snapshot của từ và số boundary đã
    // commit để Backspace có thể đi ngược nhiều Space giống mode Rewrite.
    state->composing.clear();
    updatePreedit(*inputContext, *state);
    event.filterAndAccept();
  } catch (const std::exception &error) {
    ARECA_ERROR() << "areca: preedit Bamboo processing failed: "
                  << error.what();
    clearComposition(*inputContext, *state);
    event.forward();
  }
}

bool PreeditModeHandler::startFinalizedWordRecovery(
    im::InputContext &inputContext, PreeditInputState &state) {
  const auto &word = state.engine->currentText();
  if (word.empty() || !im::utf8::validate(word)) {
    return false;
  }
  const auto wordLength = static_cast<uint32_t>(im::utf8::length(word));
  if (!wordLength) {
    return false;
  }

  const uint32_t deleteCount = wordLength + 1;
  if (!finalizedWordEraser_) {
    return false;
  }
  state.finalizedWordRecovery = FinalizedWordRecoveryState::Erasing;
  const auto watchedContext = inputContext.watch();
  const std::string recoveredWord = word;
  const bool started = finalizedWordEraser_(
      inputContext, deleteCount,
      [this, watchedContext, recoveredWord](bool succeeded) {
        auto *context = watchedContext.get();
        if (!context) {
          if (debugProvider_()) {
            ARECA_INFO() << "areca: preedit finalized-word callback ignored"
                         << " reason=context-lost";
          }
          return;
        }
        auto *currentState = stateFor(*context);
        if (!currentState || currentState->finalizedWordRecovery !=
                                 FinalizedWordRecoveryState::Erasing) {
          if (debugProvider_()) {
            ARECA_INFO() << "areca: preedit finalized-word callback ignored"
                         << " reason=state-changed"
                         << " has_state=" << (currentState != nullptr)
                         << " recovery_state="
                         << (currentState
                                 ? static_cast<int>(
                                       currentState->finalizedWordRecovery)
                                 : -1);
          }
          return;
        }
        currentState->finalizedWordRecovery = FinalizedWordRecoveryState::Idle;
        if (!succeeded) {
          currentState->engine->reset();
          currentState->composing.clear();
          updatePreedit(*context, *currentState);
          return;
        }
        currentState->composing = recoveredWord;
        updatePreedit(*context, *currentState);
        if (debugProvider_()) {
          ARECA_INFO() << "areca: preedit restored finalized word"
                       << " text=" << recoveredWord << " erase=backspace";
        }
      });
  if (!started) {
    state.finalizedWordRecovery = FinalizedWordRecoveryState::Idle;
  }
  return started;
}

void PreeditModeHandler::updatePreedit(im::InputContext &inputContext,
                                       const PreeditInputState &state) {
  auto &panel = inputContext.inputPanel();
  panel.reset();
  if (!state.composing.empty() && im::utf8::validate(state.composing)) {
    im::Text text;
    text.append(state.composing);
    text.setCursor(static_cast<int>(text.textLength()));
    if (inputContext.capabilityFlags().test(im::CapabilityFlag::Preedit)) {
      panel.setClientPreedit(text);
    } else {
      panel.setPreedit(text);
    }
  }
  inputContext.updatePreedit();
  inputContext.updateUserInterface(im::UserInterfaceComponent::InputPanel,
                                   true);
}

void PreeditModeHandler::clearComposition(im::InputContext &inputContext,
                                          PreeditInputState &state) {
  state.engine->reset();
  state.composing.clear();
  state.sentenceCapitalization.reset();
  updatePreedit(inputContext, state);
}

void PreeditModeHandler::commitComposition(im::InputContext &inputContext,
                                           PreeditInputState &state) {
  if (!state.composing.empty()) {
    inputContext.commitString(state.composing);
  }
  state.engine->reset();
  state.composing.clear();
  updatePreedit(inputContext, state);
}

void PreeditModeHandler::requestProtectedReset(im::InputContext &inputContext) {
  auto *state = stateFor(inputContext);
  // Bắt đầu: Giữ finalized-word recovery qua lifecycle giả của Edge X11
  if (state &&
      state->finalizedWordRecovery == FinalizedWordRecoveryState::Erasing) {
    if (debugProvider_()) {
      ARECA_INFO() << "areca: preedit reset rejected"
                   << " reason=finalized-word-recovery";
    }
    return;
  }
  // Kết thúc: Giữ finalized-word recovery qua lifecycle giả của Edge X11
  if (state && im::now(CLOCK_MONOTONIC) <= state->resetProtectedUntilUsec) {
    if (debugProvider_()) {
      ARECA_INFO() << "areca: preedit reset rejected"
                   << " reason=post-commit-window";
    }
    return;
  }
  resetContext(inputContext);
}

void PreeditModeHandler::resetContext(im::InputContext &inputContext) {
  if (auto *state = stateFor(inputContext)) {
    state->finalizedWordRecovery = FinalizedWordRecoveryState::Idle;
    state->resetProtectedUntilUsec = 0;
    clearComposition(inputContext, *state);
  }
}

bool PreeditModeHandler::resetAfterClick(im::InputContext &inputContext) {
  auto *state = stateFor(inputContext);
  if (!state || !state->engine || !state->composing.empty() ||
      state->finalizedWordRecovery != FinalizedWordRecoveryState::Idle) {
    return false;
  }

  // Preedit trống vẫn có thể giữ snapshot của từ vừa commit trong Bamboo. Caret
  // đã sang chỗ khác nên bỏ snapshot đó.
  state->engine->reset();
  state->sentenceCapitalization.reset();
  state->resetProtectedUntilUsec = 0;
  return true;
}

} // namespace areca
