#include <cassert>
#include <string>
#include <vector>

#include "im/im.h"

#include "preedit_mode.h"

namespace {

class TestInputContext final : public areca::im::InputContext {
public:
  explicit TestInputContext(areca::im::InputContextManager &manager)
      : areca::im::InputContext(manager, "org.gnome.TextEditor") {}
  ~TestInputContext() override { destroy(); }

  void commitStringImpl(const std::string &text) override {
    commits.push_back(text);
  }
  void deleteSurroundingTextImpl(int offset, unsigned int count) override {
    deletes.push_back({offset, count});
  }
  void forwardKeyImpl(const areca::im::ForwardKeyEvent &event) override {
    if (event.rawKey().check(areca::im::Key_BackSpace)) {
      forwardedBackspaces.push_back(event.isRelease());
    }
  }
  void updatePreeditImpl() override {}

  struct DeleteCall {
    int offset;
    unsigned int count;
  };
  std::vector<std::string> commits;
  std::vector<DeleteCall> deletes;
  std::vector<bool> forwardedBackspaces;

private:
};

} // namespace

int main() {
  areca::im::InputContextManager manager;
  areca::PreeditModeHandler::StateFactory factory(
      [](areca::im::InputContext &) {
        return new areca::PreeditInputState("Telex 2", true, true, true,
                                            "Unicode", true, false, 1,
                                            {{"bt", "Be There"}});
      });
  manager.registerProperty("preedit-surrounding-restore-test", &factory);

  bool enabled = true;
  bool backspaceRecovery = true;
  uint32_t requestedBackspaces = 0;
  areca::PreeditModeHandler::FinalizedWordEraseDone eraseDone;
  areca::PreeditModeHandler handler(
      factory, [] { return false; }, [] { return false; },
      [&] { return backspaceRecovery; }, [&] { return enabled; },
      [](const areca::im::Key &key) {
        return key.normalize().sym() == areca::im::Key_space &&
               key.states().test(areca::im::KeyState::Shift);
      },
      [&](areca::im::InputContext &, uint32_t count,
          areca::PreeditModeHandler::FinalizedWordEraseDone done) {
        requestedBackspaces = count;
        eraseDone = std::move(done);
        return true;
      });

  TestInputContext context(manager);
  context.setCapabilityFlags(areca::im::CapabilityFlag::SurroundingText);
  context.surroundingText().setText("tưởng", 5, 5);
  handler.activate(context);

  // Khi bật, từ cũ được xóa đúng một lần khỏi ứng dụng rồi chuyển vào preedit;
  // phím mới tiếp tục sửa trên state Bamboo vừa phục hồi.
  areca::im::KeyEvent toneKey(&context, areca::im::Key(areca::im::Key_s));
  handler.handleKeyEvent(toneKey);
  assert(toneKey.accepted());
  assert(context.deletes.size() == 1);
  assert(context.deletes[0].offset == -5);
  assert(context.deletes[0].count == 5);
  assert(context.propertyFor(&factory)->composing == "tướng");

  areca::im::KeyEvent boundary(&context, areca::im::Key(areca::im::Key_space));
  handler.handleKeyEvent(boundary);
  assert(boundary.accepted());
  assert(context.commits == std::vector<std::string>{"tướng "});
  assert(context.propertyFor(&factory)->composing.empty());

  // Space đã commit ngay. Backspace dùng backend để xóa cả từ và boundary,
  // sau đó đưa riêng từ trở lại preedit để sửa tiếp.
  areca::im::KeyEvent undoBoundary(&context,
                                   areca::im::Key(areca::im::Key_BackSpace));
  handler.handleKeyEvent(undoBoundary);
  assert(undoBoundary.accepted());
  assert(context.propertyFor(&factory)->finalizedWordRecovery ==
         areca::FinalizedWordRecoveryState::AwaitingBackspaceRelease);
  assert(requestedBackspaces == 0);
  areca::im::KeyEvent undoBoundaryRelease(
      &context, areca::im::Key(areca::im::Key_BackSpace), true);
  handler.handleKeyEvent(undoBoundaryRelease);
  assert(undoBoundaryRelease.accepted());
  assert(context.deletes.size() == 1);
  assert(requestedBackspaces == 6);
  assert(eraseDone);
  eraseDone(true);
  assert(context.propertyFor(&factory)->composing == "tướng");
  areca::im::KeyEvent retone(&context, areca::im::Key(areca::im::Key_f));
  handler.handleKeyEvent(retone);
  assert(retone.accepted());
  assert(context.propertyFor(&factory)->composing == "tường");

  // Giữ modifier đã chọn rồi nhấn Space phải finalize nguyên từ viết tắt mà
  // không expand macro; Space vẫn được commit như boundary bình thường.
  TestInputContext macroBypassContext(manager);
  handler.activate(macroBypassContext);
  for (const char key : std::string("bt")) {
    areca::im::KeyEvent keyEvent(
        &macroBypassContext, areca::im::Key(areca::im::Key::keySymFromUnicode(
                                 static_cast<unsigned char>(key))));
    handler.handleKeyEvent(keyEvent);
    assert(keyEvent.accepted());
  }
  areca::im::KeyEvent bypassSpace(
      &macroBypassContext,
      areca::im::Key(areca::im::Key_space, areca::im::KeyState::Shift));
  handler.handleKeyEvent(bypassSpace);
  assert(bypassSpace.accepted());
  assert(macroBypassContext.commits == std::vector<std::string>{"bt "});

  // Click đặt caret giữa `tiến`, ngay sau `ế`: prefix `tiế` chưa hoàn chỉnh
  // được thu hẹp tới `ế`. `ti` và `n` vẫn nằm trong app nên f tạo `tiền`.
  TestInputContext middleContext(manager);
  middleContext.setCapabilityFlags(areca::im::CapabilityFlag::SurroundingText);
  middleContext.surroundingText().setText("tiến", 3, 3);
  handler.activate(middleContext);
  assert(handler.prepareSurroundingRestoreAfterClick(middleContext));
  areca::im::KeyEvent middleTone(&middleContext,
                                 areca::im::Key(areca::im::Key_f));
  handler.handleKeyEvent(middleTone);
  assert(middleTone.accepted());
  assert(middleContext.deletes.size() == 1);
  assert(middleContext.deletes[0].offset == -1);
  assert(middleContext.deletes[0].count == 1);
  assert(middleContext.propertyFor(&factory)->composing == "ề");

  // Không được reset ngầm một preedit còn hiển thị chỉ vì tracker thấy click.
  assert(!handler.prepareSurroundingRestoreAfterClick(middleContext));
  assert(middleContext.propertyFor(&factory)->composing == "ề");

  // Snapshot trễ (log anvildroid-gui): Areca vừa commit "gõ " nhưng app gửi
  // lại "gõ". Restore lần này phải bị bỏ qua, không xoá gì.
  TestInputContext laggingContext(manager);
  laggingContext.setCapabilityFlags(areca::im::CapabilityFlag::SurroundingText);
  laggingContext.surroundingText().setText("", 0, 0);
  handler.activate(laggingContext);
  for (const char key : std::string("gox ")) {
    areca::im::KeyEvent keyEvent(
        &laggingContext, areca::im::Key(areca::im::Key::keySymFromUnicode(
                             static_cast<unsigned char>(key))));
    handler.handleKeyEvent(keyEvent);
    assert(keyEvent.accepted());
  }
  assert(laggingContext.commits == std::vector<std::string>{"gõ "});
  auto *laggingState = laggingContext.propertyFor(&factory);
  assert(laggingState->knownTextBeforeCursor == "gõ ");
  laggingContext.surroundingText().setText("gõ", 2, 2);
  laggingState->surroundingRestoreArmed = true;
  areca::im::KeyEvent laggingKey(&laggingContext,
                                 areca::im::Key(areca::im::Key_n));
  handler.handleKeyEvent(laggingKey);
  assert(laggingKey.accepted());
  assert(laggingContext.deletes.empty());
  assert(laggingState->composing == "n");

  // Bỏ qua chỉ áp dụng cho lần đó: sau khi đổi ô, snapshot đúng thì restore
  // vẫn chạy bình thường.
  handler.deactivate(laggingContext);
  laggingContext.surroundingText().setText("tưởng", 5, 5);
  handler.activate(laggingContext);
  assert(laggingState->knownTextBeforeCursor.empty());
  areca::im::KeyEvent nextRestoreKey(&laggingContext,
                                     areca::im::Key(areca::im::Key_s));
  handler.handleKeyEvent(nextRestoreKey);
  assert(nextRestoreKey.accepted());
  assert(laggingContext.deletes.size() == 1);
  assert(laggingState->composing == "tướng");

  // Chromium báo dấu cách cuối thành NBSP: không được coi là snapshot trễ.
  TestInputContext nbspContext(manager);
  nbspContext.setCapabilityFlags(areca::im::CapabilityFlag::SurroundingText);
  nbspContext.surroundingText().setText("", 0, 0);
  handler.activate(nbspContext);
  for (const char key : std::string("gox")) {
    areca::im::KeyEvent keyEvent(
        &nbspContext, areca::im::Key(areca::im::Key::keySymFromUnicode(
                          static_cast<unsigned char>(key))));
    handler.handleKeyEvent(keyEvent);
  }
  areca::im::KeyEvent nbspSpace(&nbspContext,
                                areca::im::Key(areca::im::Key_space));
  handler.handleKeyEvent(nbspSpace);
  auto *nbspState = nbspContext.propertyFor(&factory);
  assert(nbspState->knownTextBeforeCursor == "gõ ");
  // Con trỏ lùi vào cuối "gõ" bằng phím di chuyển ứng dụng không báo: snapshot
  // vẫn kết thúc bằng NBSP thay cho dấu cách nên restore không bị từ chối.
  nbspContext.surroundingText().setText("gõ\u00a0", 3, 3);
  nbspState->surroundingRestoreArmed = true;
  areca::im::KeyEvent nbspKey(&nbspContext, areca::im::Key(areca::im::Key_n));
  handler.handleKeyEvent(nbspKey);
  assert(nbspKey.accepted());
  assert(nbspContext.deletes.empty());
  assert(nbspState->composing == "n");

  // Khi tắt, cùng snapshot chỉ tạo preedit từ phím mới và tuyệt đối không xóa
  // phần text đã commit trước con trỏ.
  enabled = false;
  TestInputContext disabledContext(manager);
  disabledContext.setCapabilityFlags(
      areca::im::CapabilityFlag::SurroundingText);
  disabledContext.surroundingText().setText("tưởng", 5, 5);
  handler.activate(disabledContext);
  areca::im::KeyEvent plainKey(&disabledContext,
                               areca::im::Key(areca::im::Key_a));
  handler.handleKeyEvent(plainKey);
  assert(plainKey.accepted());
  assert(disabledContext.deletes.empty());
  assert(disabledContext.propertyFor(&factory)->composing == "a");

  // Regression macro `k → không`: snapshot thiếu dấu chấm có thể tới trễ sau
  // khi macro đã commit. Space là boundary nên không được hút "không" vào
  // Bamboo; nếu restore nhầm sẽ xóa từ và tạo kết quả lặp kiểu "không.hông ".
  enabled = true;
  TestInputContext macroBoundaryContext(manager);
  macroBoundaryContext.setCapabilityFlags(
      areca::im::CapabilityFlag::SurroundingText);
  macroBoundaryContext.surroundingText().setText("không", 5, 5);
  handler.activate(macroBoundaryContext);
  areca::im::KeyEvent macroSpace(&macroBoundaryContext,
                                 areca::im::Key(areca::im::Key_space));
  handler.handleKeyEvent(macroSpace);
  assert(!macroSpace.accepted());
  assert(macroBoundaryContext.deletes.empty());
  assert(macroBoundaryContext.propertyFor(&factory)->composing.empty());
  assert(!macroBoundaryContext.propertyFor(&factory)->surroundingRestoreArmed);

  // App không có SurroundingText dùng backend Backspace để xóa chính xác từ
  // vừa commit cùng boundary rồi đưa từ trở lại preedit.
  TestInputContext noSurroundingContext(manager);
  handler.activate(noSurroundingContext);
  for (const char key : std::string("as")) {
    areca::im::KeyEvent keyEvent(
        &noSurroundingContext, areca::im::Key(areca::im::Key::keySymFromUnicode(
                                   static_cast<unsigned char>(key))));
    handler.handleKeyEvent(keyEvent);
    assert(keyEvent.accepted());
  }
  areca::im::KeyEvent firstSpace(&noSurroundingContext,
                                 areca::im::Key(areca::im::Key_space));
  handler.handleKeyEvent(firstSpace);
  assert(firstSpace.accepted());
  assert(noSurroundingContext.commits == std::vector<std::string>{"á "});
  assert(noSurroundingContext.propertyFor(&factory)->composing.empty());
  // Frontend thường gửi reset ngay sau commit. Reset này không được xóa
  // snapshot finalized cần cho Backspace quay lại sửa từ vừa gõ.
  handler.requestProtectedReset(noSurroundingContext);
  assert(noSurroundingContext.propertyFor(&factory)->resetProtectedUntilUsec !=
         0);

  for (int index = 0; index < 2; ++index) {
    areca::im::KeyEvent extraSpace(&noSurroundingContext,
                                   areca::im::Key(areca::im::Key_space));
    handler.handleKeyEvent(extraSpace);
    assert(!extraSpace.accepted());
  }
  for (int index = 0; index < 2; ++index) {
    areca::im::KeyEvent crossSpace(&noSurroundingContext,
                                   areca::im::Key(areca::im::Key_BackSpace));
    handler.handleKeyEvent(crossSpace);
    assert(!crossSpace.accepted());
    assert(noSurroundingContext.propertyFor(&factory)->composing.empty());
  }
  areca::im::KeyEvent restoreWord(&noSurroundingContext,
                                  areca::im::Key(areca::im::Key_BackSpace));
  handler.handleKeyEvent(restoreWord);
  assert(restoreWord.accepted());
  assert(noSurroundingContext.propertyFor(&factory)->finalizedWordRecovery ==
         areca::FinalizedWordRecoveryState::AwaitingBackspaceRelease);
  areca::im::KeyEvent restoreWordRelease(
      &noSurroundingContext, areca::im::Key(areca::im::Key_BackSpace), true);
  handler.handleKeyEvent(restoreWordRelease);
  assert(restoreWordRelease.accepted());
  assert(requestedBackspaces == 2);
  assert(eraseDone);
  assert(noSurroundingContext.propertyFor(&factory)->finalizedWordRecovery ==
         areca::FinalizedWordRecoveryState::Erasing);
  areca::im::KeyEvent injectedBackspace(
      &noSurroundingContext, areca::im::Key(areca::im::Key_BackSpace));
  handler.handleKeyEvent(injectedBackspace);
  assert(!injectedBackspace.accepted());
  assert(noSurroundingContext.propertyFor(&factory)->finalizedWordRecovery ==
         areca::FinalizedWordRecoveryState::Erasing);
  // Edge X11 sends reset after every injected Backspace. The erase transaction
  // must keep its state until the backend callback restores the word.
  noSurroundingContext.propertyFor(&factory)->resetProtectedUntilUsec = 0;
  handler.requestProtectedReset(noSurroundingContext);
  assert(noSurroundingContext.propertyFor(&factory)->finalizedWordRecovery ==
         areca::FinalizedWordRecoveryState::Erasing);
  handler.deactivate(noSurroundingContext);
  assert(noSurroundingContext.propertyFor(&factory)->finalizedWordRecovery ==
         areca::FinalizedWordRecoveryState::Erasing);
  handler.activate(noSurroundingContext);
  assert(noSurroundingContext.propertyFor(&factory)->finalizedWordRecovery ==
         areca::FinalizedWordRecoveryState::Erasing);
  eraseDone(true);
  assert(noSurroundingContext.forwardedBackspaces.empty());
  assert(noSurroundingContext.propertyFor(&factory)->composing == "á");

  // Có SurroundingText thì snapshot vẫn sống qua nhiều Space đã commit/forward.
  // Backspace lùi hết boundary mới xóa chính xác `từ + Space` và mở lại
  // preedit.
  TestInputContext multipleSpacesContext(manager);
  multipleSpacesContext.setCapabilityFlags(
      areca::im::CapabilityFlag::SurroundingText);
  multipleSpacesContext.surroundingText().setText("", 0, 0);
  handler.activate(multipleSpacesContext);
  for (const char key : std::string("as")) {
    areca::im::KeyEvent keyEvent(
        &multipleSpacesContext,
        areca::im::Key(areca::im::Key::keySymFromUnicode(
            static_cast<unsigned char>(key))));
    handler.handleKeyEvent(keyEvent);
  }
  areca::im::KeyEvent committedSpace(&multipleSpacesContext,
                                     areca::im::Key(areca::im::Key_space));
  handler.handleKeyEvent(committedSpace);
  assert(committedSpace.accepted());
  for (int index = 0; index < 2; ++index) {
    areca::im::KeyEvent extraSpace(&multipleSpacesContext,
                                   areca::im::Key(areca::im::Key_space));
    handler.handleKeyEvent(extraSpace);
    assert(!extraSpace.accepted());
  }
  for (int index = 0; index < 2; ++index) {
    areca::im::KeyEvent crossSpace(&multipleSpacesContext,
                                   areca::im::Key(areca::im::Key_BackSpace));
    handler.handleKeyEvent(crossSpace);
    assert(!crossSpace.accepted());
  }
  areca::im::KeyEvent restoreAfterSpaces(
      &multipleSpacesContext, areca::im::Key(areca::im::Key_BackSpace));
  handler.handleKeyEvent(restoreAfterSpaces);
  assert(restoreAfterSpaces.accepted());
  areca::im::KeyEvent restoreAfterSpacesRelease(
      &multipleSpacesContext, areca::im::Key(areca::im::Key_BackSpace), true);
  handler.handleKeyEvent(restoreAfterSpacesRelease);
  assert(restoreAfterSpacesRelease.accepted());
  assert(multipleSpacesContext.deletes.empty());
  assert(requestedBackspaces == 2);
  assert(eraseDone);
  eraseDone(true);
  assert(multipleSpacesContext.propertyFor(&factory)->composing == "á");

  // Nhấn giữ Backspace hủy việc mở lại từ và cho repeat đi thẳng xuống app để
  // người dùng tiếp tục xóa nhanh như trong Rewrite mode.
  TestInputContext heldBackspaceContext(manager);
  handler.activate(heldBackspaceContext);
  for (const char key : std::string("as ")) {
    areca::im::KeyEvent keyEvent(
        &heldBackspaceContext, areca::im::Key(areca::im::Key::keySymFromUnicode(
                                   static_cast<unsigned char>(key))));
    handler.handleKeyEvent(keyEvent);
  }
  areca::im::KeyEvent heldPress(&heldBackspaceContext,
                                areca::im::Key(areca::im::Key_BackSpace));
  handler.handleKeyEvent(heldPress);
  assert(heldPress.accepted());
  areca::im::KeyEvent heldRepeat(
      &heldBackspaceContext,
      areca::im::Key(areca::im::Key_BackSpace, areca::im::KeyState::Repeat));
  handler.handleKeyEvent(heldRepeat);
  assert(!heldRepeat.accepted());
  assert(heldBackspaceContext.propertyFor(&factory)->finalizedWordRecovery ==
         areca::FinalizedWordRecoveryState::Idle);
  assert(heldBackspaceContext.propertyFor(&factory)
             ->engine->currentText()
             .empty());

  // Phím kế tiếp bắt đầu composition mới; từ trước đó đã commit ngay từ Space,
  // không có trạng thái pending làm chậm trải nghiệm gõ.
  TestInputContext immediateCommitContext(manager);
  handler.activate(immediateCommitContext);
  for (const char key : std::string("as b")) {
    areca::im::KeyEvent keyEvent(
        &immediateCommitContext,
        areca::im::Key(areca::im::Key::keySymFromUnicode(
            static_cast<unsigned char>(key))));
    handler.handleKeyEvent(keyEvent);
    assert(keyEvent.accepted());
  }
  assert(immediateCommitContext.commits == std::vector<std::string>{"á "});
  assert(immediateCommitContext.propertyFor(&factory)->composing == "b");

  // BackspaceRecovery trong Preedit dùng processBackspace thay vì xóa thô, nên
  // chuỗi Latin do spell-check trả về có thể được dựng lại thành âm tiết đúng.
  TestInputContext recoveryContext(manager);
  handler.activate(recoveryContext);
  for (const char key : std::string("nhanhsh")) {
    areca::im::KeyEvent keyEvent(
        &recoveryContext, areca::im::Key(areca::im::Key::keySymFromUnicode(
                              static_cast<unsigned char>(key))));
    handler.handleKeyEvent(keyEvent);
    assert(keyEvent.accepted());
  }
  assert(recoveryContext.propertyFor(&factory)->composing == "nhanhsh");
  areca::im::KeyEvent recover(&recoveryContext,
                              areca::im::Key(areca::im::Key_BackSpace));
  handler.handleKeyEvent(recover);
  assert(recover.accepted());
  assert(recoveryContext.propertyFor(&factory)->composing == "nhánh");

  // Khi Backspace chuyển thành repeat, commit phần preedit đang thấy rồi xóa
  // state Bamboo; repeat hiện tại và các repeat sau đi thẳng xuống ứng dụng.
  TestInputContext recoveryRepeatContext(manager);
  handler.activate(recoveryRepeatContext);
  for (const char key : std::string("nhanhsh")) {
    areca::im::KeyEvent keyEvent(
        &recoveryRepeatContext,
        areca::im::Key(areca::im::Key::keySymFromUnicode(
            static_cast<unsigned char>(key))));
    handler.handleKeyEvent(keyEvent);
  }
  areca::im::KeyEvent recoveryRepeat(
      &recoveryRepeatContext,
      areca::im::Key(areca::im::Key_BackSpace, areca::im::KeyState::Repeat));
  handler.handleKeyEvent(recoveryRepeat);
  assert(!recoveryRepeat.accepted());
  assert(recoveryRepeatContext.commits == std::vector<std::string>{"nhanhsh"});
  assert(recoveryRepeatContext.propertyFor(&factory)->composing.empty());
  assert(recoveryRepeatContext.propertyFor(&factory)
             ->engine->currentText()
             .empty());

  // IBus không gắn Repeat; IbusKeyEventClock gắn cờ này cho press khi phím chưa
  // nhả. Nhấn giữ hủy khôi phục từ đã commit.
  {
    TestInputContext ibusContext(manager);
    handler.activate(ibusContext);
    auto *ibusState = ibusContext.propertyFor(&factory);
    for (const char key : std::string("as ")) {
      areca::im::KeyEvent typed(
          &ibusContext, areca::im::Key(areca::im::Key::keySymFromUnicode(key)));
      handler.handleKeyEvent(typed);
    }
    const auto erasuresBefore = requestedBackspaces;
    areca::im::KeyEvent press(&ibusContext,
                              areca::im::Key(areca::im::Key_BackSpace));
    handler.handleKeyEvent(press);
    assert(press.accepted());
    for (int i = 0; i < 3; ++i) {
      areca::im::KeyEvent repeat(&ibusContext,
                                 areca::im::Key(areca::im::Key_BackSpace,
                                                areca::im::KeyState::Repeat));
      handler.handleKeyEvent(repeat);
      assert(!repeat.accepted());
      assert(ibusState->composing.empty());
      assert(ibusState->engine->currentText().empty());
      assert(ibusState->finalizedWordRecovery ==
             areca::FinalizedWordRecoveryState::Idle);
    }
    areca::im::KeyEvent release(&ibusContext,
                                areca::im::Key(areca::im::Key_BackSpace), true);
    handler.handleKeyEvent(release);
    assert(!release.accepted());
    assert(requestedBackspaces == erasuresBefore);
    assert(ibusContext.commits == std::vector<std::string>{"á "});

    // Release phải kết thúc lần nhấn giữ để lần gõ sau recovery bình thường.
    for (const char key : std::string("nhanhsh")) {
      areca::im::KeyEvent typed(
          &ibusContext, areca::im::Key(areca::im::Key::keySymFromUnicode(key)));
      handler.handleKeyEvent(typed);
    }
    areca::im::KeyEvent tap(&ibusContext,
                            areca::im::Key(areca::im::Key_BackSpace));
    handler.handleKeyEvent(tap);
    assert(tap.accepted());
    assert(ibusState->composing == "nhánh");
    // Press tiếp theo vẫn sửa trong preedit, không ép commit ra ứng dụng.
    areca::im::KeyEvent secondTap(&ibusContext,
                                  areca::im::Key(areca::im::Key_BackSpace));
    handler.handleKeyEvent(secondTap);
    assert(secondTap.accepted());
    assert(!ibusState->composing.empty());
    assert(ibusContext.commits == std::vector<std::string>{"á "});
  }

  backspaceRecovery = false;
  TestInputContext plainBackspaceContext(manager);
  handler.activate(plainBackspaceContext);
  for (const char key : std::string("nhanhsh")) {
    areca::im::KeyEvent keyEvent(
        &plainBackspaceContext,
        areca::im::Key(areca::im::Key::keySymFromUnicode(
            static_cast<unsigned char>(key))));
    handler.handleKeyEvent(keyEvent);
  }
  areca::im::KeyEvent plainBackspace(&plainBackspaceContext,
                                     areca::im::Key(areca::im::Key_BackSpace));
  handler.handleKeyEvent(plainBackspace);
  assert(plainBackspace.accepted());
  assert(plainBackspaceContext.propertyFor(&factory)->composing == "nhanhs");
}
