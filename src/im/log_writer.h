#pragma once

namespace areca::im {

// Gắn writer log cho process engine. ibus-daemon chạy nền nối stderr của engine
// vào /dev/null, nên khi stderr không phải terminal hay journal thì ghi thẳng
// vào journal để log debug vẫn đọc được bằng journalctl.
void installLogWriter();

} // namespace areca::im
