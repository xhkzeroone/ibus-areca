#include "raw_config.h"

#include <glib.h>
#include <glib/gstdio.h>

namespace areca::im {
namespace {

std::string_view trim(std::string_view text) {
  constexpr std::string_view kSpace = " \t\r";
  const auto begin = text.find_first_not_of(kSpace);
  if (begin == std::string_view::npos) {
    return {};
  }
  const auto end = text.find_last_not_of(kSpace);
  return text.substr(begin, end - begin + 1);
}

std::string unquote(std::string_view value) {
  if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
    return std::string(value);
  }
  value = value.substr(1, value.size() - 2);
  std::string out;
  out.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '\\' && i + 1 < value.size()) {
      const char next = value[++i];
      out.push_back(next == 'n' ? '\n' : next);
    } else {
      out.push_back(value[i]);
    }
  }
  return out;
}

std::string quote(const std::string &value) {
  const bool needsQuote =
      value.find_first_of(" \t\"\\\n#;") != std::string::npos ||
      (!value.empty() && (value.front() == ' ' || value.back() == ' '));
  if (!needsQuote) {
    return value;
  }
  std::string out = "\"";
  for (char c : value) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
      out.push_back(c);
    } else if (c == '\n') {
      out += "\\n";
    } else {
      out.push_back(c);
    }
  }
  out.push_back('"');
  return out;
}

void writeComment(std::string &out, const std::string &comment) {
  if (!comment.empty()) {
    out += "# " + comment + "\n";
  }
}

void writeSection(std::string &out, const RawConfig &node,
                  const std::string &path) {
  bool hasLeaf = false;
  for (const auto &[name, child] : node.subItems()) {
    if (!child->hasSubItems()) {
      hasLeaf = true;
      break;
    }
  }
  if (hasLeaf && !path.empty()) {
    out += "[" + path + "]\n";
  }
  for (const auto &[name, child] : node.subItems()) {
    if (child->hasSubItems()) {
      continue;
    }
    writeComment(out, child->comment());
    if (child->commentedOut()) {
      out += "# ";
    }
    out += name + "=" + quote(child->value()) + "\n";
  }
  if (hasLeaf) {
    out += "\n";
  }
  for (const auto &[name, child] : node.subItems()) {
    if (child->hasSubItems()) {
      writeSection(out, *child, path.empty() ? name : path + "/" + name);
    }
  }
}

} // namespace

RawConfig *RawConfig::child(std::string_view name, bool create) {
  for (auto &[childName, child] : children_) {
    if (childName == name) {
      return child.get();
    }
  }
  if (!create) {
    return nullptr;
  }
  children_.emplace_back(std::string(name), std::make_unique<RawConfig>());
  return children_.back().second.get();
}

const RawConfig *RawConfig::child(std::string_view name) const {
  for (const auto &[childName, child] : children_) {
    if (childName == name) {
      return child.get();
    }
  }
  return nullptr;
}

RawConfig *RawConfig::get(std::string_view path, bool create) {
  RawConfig *node = this;
  while (node && !path.empty()) {
    const auto slash = path.find('/');
    node = node->child(path.substr(0, slash), create);
    path = slash == std::string_view::npos ? std::string_view{}
                                           : path.substr(slash + 1);
  }
  return node;
}

const RawConfig *RawConfig::get(std::string_view path) const {
  const RawConfig *node = this;
  while (node && !path.empty()) {
    const auto slash = path.find('/');
    node = node->child(path.substr(0, slash));
    path = slash == std::string_view::npos ? std::string_view{}
                                           : path.substr(slash + 1);
  }
  return node;
}

void readIniText(RawConfig &config, std::string_view text) {
  std::string section;
  while (!text.empty()) {
    const auto newline = text.find('\n');
    const auto line = trim(text.substr(0, newline));
    text = newline == std::string_view::npos ? std::string_view{}
                                             : text.substr(newline + 1);
    if (line.empty() || line.front() == '#' || line.front() == ';') {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      section = std::string(trim(line.substr(1, line.size() - 2)));
      continue;
    }
    const auto equal = line.find('=');
    if (equal == std::string_view::npos) {
      continue;
    }
    const auto key = trim(line.substr(0, equal));
    if (key.empty()) {
      continue;
    }
    const auto value = unquote(trim(line.substr(equal + 1)));
    const auto path =
        section.empty() ? std::string(key) : section + "/" + std::string(key);
    config.setValueByPath(path, value);
  }
}

std::string writeIniText(const RawConfig &config) {
  std::string out;
  writeSection(out, config, "");
  return out;
}

bool readIniFile(RawConfig &config, const std::string &path) {
  gchar *contents = nullptr;
  gsize length = 0;
  if (!g_file_get_contents(path.c_str(), &contents, &length, nullptr)) {
    return false;
  }
  readIniText(config, std::string_view(contents, length));
  g_free(contents);
  return true;
}

bool safeSaveIniFile(const RawConfig &config, const std::string &path) {
  gchar *dir = g_path_get_dirname(path.c_str());
  g_mkdir_with_parents(dir, 0700);
  g_free(dir);
  const auto text = writeIniText(config);
  return g_file_set_contents(path.c_str(), text.data(),
                             static_cast<gssize>(text.size()), nullptr);
}

} // namespace areca::im
