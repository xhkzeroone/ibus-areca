#pragma once

#include <memory>
#include <string>
#include <vector>

#include <ibus.h>

#include "areca_engine.h"
#include "engine_host.h"
#include "ibus/status_notifier.h"

namespace areca {

class IbusInputContext;

// Host dùng chung cả process: giữ ArecaEngine duy nhất, danh sách input context
// đang sống và các dịch vụ desktop (thông báo, theo dõi file cấu hình).
class IbusHost final : public EngineHost {
public:
  static IbusHost &instance();

  ArecaEngine &engine() { return *engine_; }

  im::EventLoop &eventLoop() override { return eventLoop_; }
  void foreachInputContext(
      const std::function<bool(im::InputContext *)> &fn) override;
  void showInputMethodInformation(im::InputContext *inputContext,
                                  const std::string &text) override;
  void updateStatusArea(im::InputContext *inputContext) override;
  void sendNotification(const std::string &summary, const std::string &body,
                        int timeoutMs) override;
  std::string configDirectory() const override;

  // Engine do IBus báo là engine đang chọn (tín hiệu global-engine-changed hoặc
  // truy vấn lúc khởi động). Chưa nhận được thì dựa vào enable/disable.
  void setGlobalEngine(const std::string &name);
  // Bus dùng chung của process để menu khay chọn engine và khởi động lại IBus.
  void setBus(IBusBus *bus) { bus_ = bus; }
  // Engine instance của một input context được bật hoặc tắt.
  void engineEnabled(IbusInputContext *context);
  void engineDisabled(IbusInputContext *context);

  void addContext(IbusInputContext *context);
  void removeContext(IbusInputContext *context);
  void setFocusedContext(IbusInputContext *context) {
    focusedContext_ = context;
  }
  IbusInputContext *focusedContext() const { return focusedContext_; }

private:
  IbusHost();
  void watchConfigDirectory();
  void scheduleConfigReload();
  void scheduleStatusRefresh();
  void syncTrayIcon();
  bool arecaActive() const;
  std::vector<StatusNotifierItem::MenuItem> buildTrayMenu();

  im::EventLoop eventLoop_;
  std::vector<IbusInputContext *> contexts_;
  IbusInputContext *focusedContext_ = nullptr;
  std::vector<IbusInputContext *> enabledContexts_;
  IBusBus *bus_ = nullptr;
  bool globalEngineKnown_ = false;
  bool globalEngineIsAreca_ = false;
  std::unique_ptr<ArecaEngine> engine_;
  std::unique_ptr<StatusNotifierItem> trayItem_;
  GFileMonitor *configMonitor_ = nullptr;
  std::unique_ptr<im::EventSourceTime> configReloadTimer_;
  std::unique_ptr<im::EventSource> statusRefresh_;
};

} // namespace areca
