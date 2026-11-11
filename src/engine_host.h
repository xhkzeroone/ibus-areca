#pragma once

#include <functional>
#include <string>

#include "im/im.h"

namespace areca {

// Những gì ArecaEngine cần từ framework bộ gõ. Engine IBus implement lớp này;
// test dùng bản giả.
class EngineHost {
public:
  virtual ~EngineHost() = default;
  virtual im::EventLoop &eventLoop() = 0;
  // Duyệt mọi input context đang sống. Callback trả false để dừng.
  virtual void
  foreachInputContext(const std::function<bool(im::InputContext *)> &fn) = 0;
  // Hiện tên kiểu gõ/chế độ vừa đổi, như OSD khi chuyển bộ gõ.
  virtual void showInputMethodInformation(im::InputContext *inputContext,
                                          const std::string &text) = 0;
  // Action trên menu trạng thái vừa đổi; nullptr nghĩa là mọi context.
  virtual void updateStatusArea(im::InputContext *inputContext) = 0;
  virtual void sendNotification(const std::string &summary,
                                const std::string &body, int timeoutMs) = 0;
  // Thư mục chứa conf/areca*.conf.
  virtual std::string configDirectory() const = 0;
};

} // namespace areca
