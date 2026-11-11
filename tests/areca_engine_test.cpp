#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <glib.h>
#include <glib/gstdio.h>

#include "areca_engine.h"
#include "im/im.h"

namespace {

using areca::im::InputContext;

// Ô nhập giả giữ text như ứng dụng và áp dụng Backspace được forward.
class FakeContext final : public InputContext {
public:
  explicit FakeContext(std::string program)
      : InputContext(std::move(program)) {}

  const char *frontend() const override { return "ibus"; }

  std::string text;
  std::vector<std::string> events;

protected:
  void commitStringImpl(const std::string &value) override {
    events.push_back("commit:" + value);
    text += value;
  }
  void deleteSurroundingTextImpl(int offset, unsigned int size) override {
    events.push_back("delete:" + std::to_string(offset) + ":" +
                     std::to_string(size));
  }
  void forwardKeyImpl(const areca::im::ForwardKeyEvent &event) override {
    if (event.isRelease() || event.rawKey().sym() != areca::im::Key_BackSpace) {
      return;
    }
    events.push_back("backspace");
    const auto length = areca::im::utf8::length(text);
    if (length > 0) {
      text.erase(static_cast<size_t>(
          areca::im::utf8::ncharByteLength(text.begin(), length - 1)));
    }
  }
  void updatePreeditImpl() override {}
};

class FakeHost final : public areca::EngineHost {
public:
  explicit FakeHost(std::string configDirectory)
      : configDirectory_(std::move(configDirectory)) {}

  areca::im::EventLoop &eventLoop() override { return loop_; }
  void
  foreachInputContext(const std::function<bool(InputContext *)> &fn) override {
    for (auto *context : contexts) {
      if (!fn(context)) {
        break;
      }
    }
  }
  void showInputMethodInformation(InputContext *,
                                  const std::string &text) override {
    lastInformation = text;
  }
  void updateStatusArea(InputContext *) override {}
  void sendNotification(const std::string &summary, const std::string &,
                        int) override {
    notifications.push_back(summary);
  }
  std::string configDirectory() const override { return configDirectory_; }

  std::vector<InputContext *> contexts;
  std::vector<std::string> notifications;
  std::string lastInformation;

private:
  areca::im::EventLoop loop_;
  std::string configDirectory_;
};

void writeFile(const std::string &path, const std::string &contents) {
  gchar *dir = g_path_get_dirname(path.c_str());
  g_mkdir_with_parents(dir, 0700);
  g_free(dir);
  g_file_set_contents(path.c_str(), contents.c_str(), -1, nullptr);
}

void runFor(uint32_t ms) {
  const int64_t deadline = g_get_monotonic_time() + ms * 1000;
  while (g_get_monotonic_time() < deadline) {
    g_main_context_iteration(nullptr, FALSE);
    g_usleep(200);
  }
}

void typeText(areca::ArecaEngine &engine, FakeContext &context,
              const std::string &keys) {
  for (char key : keys) {
    areca::im::KeyEvent press(&context,
                              areca::im::Key(static_cast<areca::im::KeySym>(
                                  static_cast<unsigned char>(key))));
    engine.keyEvent(press);
    if (!press.filtered()) {
      // Phím không bị chặn thì ứng dụng tự chèn ký tự gốc.
      context.text += key;
    }
    areca::im::KeyEvent release(&context,
                                areca::im::Key(static_cast<areca::im::KeySym>(
                                    static_cast<unsigned char>(key))),
                                true);
    engine.keyEvent(release);
    runFor(2);
  }
}

std::string makeConfigDir() {
  gchar *dir = g_dir_make_tmp("areca-engine-test-XXXXXX", nullptr);
  std::string result(dir);
  g_free(dir);
  // Không dùng thiết bị thật hay tiến trình phụ trong test.
  writeFile(result + "/conf/areca-advanced.conf",
            "UseUinputInsteadOfForwardKey=False\n"
            "EnableMouseTracking=False\n");
  return result;
}

void testTypesTelexSentence() {
  FakeHost host(makeConfigDir());
  FakeContext context("gnome-text-editor");
  host.contexts.push_back(&context);
  areca::ArecaEngine engine(host);
  engine.activate(context);

  typeText(engine, context, "gox ngon laf dduwojc");
  runFor(2000);
  assert(context.text == "gõ ngon là được");
  engine.deactivate(context);
}

void testConfigRoundTrip() {
  const auto dir = makeConfigDir();
  writeFile(dir + "/conf/areca.conf",
            "BambooInputMethod=VNI\nSpellcheckMode=\"Khôi phục từ sau khi gõ "
            "xong\"\n[SelectBackendKey]\n0=grave\n");
  {
    FakeHost host(dir);
    areca::ArecaEngine engine(host);
    engine.save();
  }
  gchar *contents = nullptr;
  assert(g_file_get_contents((dir + "/conf/areca.conf").c_str(), &contents,
                             nullptr, nullptr));
  const std::string saved(contents);
  g_free(contents);
  // Giá trị đã đổi được ghi ra, giá trị mặc định nằm trong comment.
  assert(saved.find("\nBambooInputMethod=VNI\n") != std::string::npos);
  assert(saved.find("SpellcheckMode=\"Khôi phục từ sau khi gõ xong\"") !=
         std::string::npos);
  assert(saved.find("# ModernStyle=True") != std::string::npos);

  FakeHost host(dir);
  FakeContext context("kwrite");
  host.contexts.push_back(&context);
  areca::ArecaEngine engine(host);
  engine.activate(context);
  typeText(engine, context, "vie65t");
  runFor(2000);
  assert(context.text == "việt");
}

} // namespace

int main() {
  testTypesTelexSentence();
  testConfigRoundTrip();
  std::cout << "areca_engine_test passed\n";
  return 0;
}
