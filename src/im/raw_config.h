#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace areca::im {

// Cây key/value theo đường dẫn "A/B/C", cùng cấu trúc với file .conf của
// fcitx5: key cấp cao nằm đầu file, nhánh con thành section [A/B].
class RawConfig {
public:
  const std::string &value() const { return value_; }
  void setValue(std::string value) { value_ = std::move(value); }
  const std::string &comment() const { return comment_; }
  void setComment(std::string comment) { comment_ = std::move(comment); }
  // Giá trị mặc định thì ghi dạng comment như fcitx5 để file chỉ lộ chỗ đã đổi.
  bool commentedOut() const { return commentedOut_; }
  void setCommentedOut(bool value) { commentedOut_ = value; }

  RawConfig *get(std::string_view path, bool create = false);
  const RawConfig *get(std::string_view path) const;
  RawConfig &operator[](std::string_view path) { return *get(path, true); }
  void setValueByPath(std::string_view path, std::string value) {
    get(path, true)->setValue(std::move(value));
  }
  const std::vector<std::pair<std::string, std::unique_ptr<RawConfig>>> &
  subItems() const {
    return children_;
  }
  bool hasSubItems() const { return !children_.empty(); }
  void clear() {
    value_.clear();
    comment_.clear();
    children_.clear();
  }

private:
  RawConfig *child(std::string_view name, bool create);
  const RawConfig *child(std::string_view name) const;

  std::string value_;
  std::string comment_;
  bool commentedOut_ = false;
  std::vector<std::pair<std::string, std::unique_ptr<RawConfig>>> children_;
};

void readIniText(RawConfig &config, std::string_view text);
std::string writeIniText(const RawConfig &config);

// Đọc/ghi theo đường dẫn tuyệt đối. Ghi qua file tạm rồi đổi tên để lỗi giữa
// chừng không làm hỏng file cũ.
bool readIniFile(RawConfig &config, const std::string &path);
bool safeSaveIniFile(const RawConfig &config, const std::string &path);

} // namespace areca::im
