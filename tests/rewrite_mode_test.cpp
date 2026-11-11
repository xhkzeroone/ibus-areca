#include <cassert>
#include <iostream>
#include <string>

#include "bamboo_engine_adapter.h"
#include "forward_backspace_backend.h"
#include "input_scheduler.h"
#include "keys.h"
#include "rewrite_mode.h"
#include "test_support.h"

namespace {

using areca::BambooEngineAdapter;
using areca::BambooResult;
using areca::ForwardBackspaceBackend;
using areca::InputScheduler;
using areca::KeyDisposition;
using areca::KeyInput;
using areca::RewriteBackend;
using areca::RewriteMode;
using areca::test::FakeClient;
using areca::test::runFor;
using areca::test::runUntil;
namespace keys = areca::keys;

struct Fixture {
  Fixture()
      : backend([]() { return false; }),
        scheduler(
            engine, client, []() { return areca::SchedulerTiming{}; },
            []() { return false; },
            [this](const BambooResult &) -> RewriteBackend * {
              return &backend;
            }),
        mode(engine, scheduler, []() { return false; }) {}

  KeyDisposition press(char key, uint32_t state = 0) {
    return mode.handleKey(
        {static_cast<unsigned char>(key), state, static_cast<uint32_t>(key)});
  }
  KeyDisposition pressKeyval(uint32_t keyval, uint32_t state = 0) {
    return mode.handleKey({keyval, state, 0});
  }
  void settle() {
    assert(runUntil([&]() {
      return scheduler.queuedKeyCount() == 0 && !scheduler.rewritePending();
    }));
    runFor(70);
  }

  BambooEngineAdapter engine;
  FakeClient client;
  ForwardBackspaceBackend backend;
  InputScheduler scheduler;
  RewriteMode mode;
};

void testTextKeysAreAccepted() {
  Fixture fixture;
  for (char key : std::string("vieetj")) {
    assert(fixture.press(key) == KeyDisposition::Accept);
  }
  fixture.settle();
  assert(fixture.client.text == "việt");
}

void testReleaseAndModifiersPassThrough() {
  Fixture fixture;
  assert(fixture.press('a', keys::kReleaseMask) == KeyDisposition::Forward);
  assert(fixture.pressKeyval(keys::kShiftL) == KeyDisposition::Forward);
  assert(fixture.client.operations.empty());
}

void testShortcutResetsWhenIdle() {
  Fixture fixture;
  fixture.press('a');
  fixture.settle();
  assert(fixture.engine.currentText() == "a");
  assert(fixture.press('c', keys::kControlMask) == KeyDisposition::Forward);
  assert(fixture.engine.currentText().empty());
}

void testCursorMoveInsideRejectWindowKeepsWord() {
  Fixture fixture;
  fixture.press('a');
  // Ngay sau commit: phím mũi tên không được xoá từ đang gõ.
  assert(fixture.pressKeyval(keys::kHome + 1) == KeyDisposition::Forward);
  assert(fixture.engine.currentText() == "a");
  fixture.settle();
  assert(fixture.pressKeyval(keys::kHome + 1) == KeyDisposition::Forward);
  assert(fixture.engine.currentText().empty());
}

void testBackspaceKeepsBambooInSync() {
  Fixture fixture;
  for (char key : std::string("vie")) {
    fixture.press(key);
  }
  fixture.settle();
  assert(fixture.pressKeyval(keys::kBackSpace) == KeyDisposition::Forward);
  // Ứng dụng tự xoá "e"; Bamboo lùi theo nên "e" rồi "e" ghép thành "ê".
  fixture.client.forwardBackspace();
  assert(fixture.engine.currentText() == "vi");
  for (char key : std::string("ee")) {
    fixture.press(key);
  }
  fixture.settle();
  assert(fixture.client.text == "viê");
}

void testPasswordFieldPassesThrough() {
  Fixture fixture;
  fixture.mode.setPasswordField(true);
  assert(fixture.press('a') == KeyDisposition::Forward);
  assert(fixture.client.operations.empty());
}

void testEnterPassesThroughAndEndsWord() {
  Fixture fixture;
  fixture.press('a');
  fixture.settle();
  assert(fixture.pressKeyval(keys::kReturn) == KeyDisposition::Forward);
  fixture.press('w');
  fixture.settle();
  // Từ cũ đã kết thúc nên "w" không sửa "a" thành "ă".
  assert(fixture.client.text == "aư");
}

} // namespace

int main() {
  testTextKeysAreAccepted();
  testReleaseAndModifiersPassThrough();
  testShortcutResetsWhenIdle();
  testCursorMoveInsideRejectWindowKeepsWord();
  testBackspaceKeepsBambooInSync();
  testPasswordFieldPassesThrough();
  testEnterPassesThroughAndEndsWord();
  std::cout << "rewrite_mode_test passed\n";
  return 0;
}
