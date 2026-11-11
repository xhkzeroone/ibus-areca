#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace areca::im {
class InputContext;
}

namespace areca {

void updateSurroundingCacheAfterDelete(im::InputContext &inputContext,
                                       int offset, uint32_t count);
void updateSurroundingCacheAfterCommit(im::InputContext &inputContext,
                                       const std::string &committedText);
void updateSurroundingCacheAfterDeleteV2(im::InputContext &inputContext,
                                         int offset, uint32_t count);
void updateSurroundingCacheAfterCommitV2(im::InputContext &inputContext,
                                         const std::string &committedText);

std::size_t commonPrefixBytesUTF8Boundary(const std::string &s1,
                                          const std::string &s2);
uint32_t utf8CharCount(const std::string &s);

} // namespace areca
