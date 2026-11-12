#pragma once

#include <memory>
#include <string>

#include <ibus.h>

#include "ibus/ibus_key_event_clock.h"
#include "im/im.h"

namespace areca {

// Một ô nhập phía IBus. IBus tạo một IBusEngine cho mỗi input context của
// client, nên object này vừa là input context của Areca vừa nói chuyện với
// IBus qua engine đó.
class IbusInputContext final : public im::InputContext {
public:
  explicit IbusInputContext(IBusEngine *engine);
  ~IbusInputContext() override;

  IBusEngine *engine() const { return engine_; }

  // Client IBus báo dạng "<toolkit>:<chương trình>", ví dụ "gtk3-im:gedit".
  void setClient(const std::string &client);
  // Xử lý một phím từ process-key-event. Trả true nếu Areca nhận phím.
  bool processKeyEvent(guint keyval, guint keycode, guint state);

  void showInformation(const std::string &text);
  void refreshStatusArea();
  void activateProperty(const std::string &name, guint state);
  void selectCandidate(guint index);

protected:
  void commitStringImpl(const std::string &text) override;
  void deleteSurroundingTextImpl(int offset, unsigned int size) override;
  void forwardKeyImpl(const im::ForwardKeyEvent &event) override;
  void updatePreeditImpl() override;
  void updateUserInterfaceImpl(im::UserInterfaceComponent component,
                               bool immediate) override;

private:
  void updateLookupTable();

  IBusEngine *engine_;
  IbusKeyEventClock keyEventClock_;
  bool showingInformation_ = false;
  std::unique_ptr<im::EventSourceTime> informationTimer_;
};

} // namespace areca
