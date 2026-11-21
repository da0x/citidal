#pragma once

#include "engines/source-engine/content.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace source_engine {

std::vector<std::string> keyvalue_tokens(std::string_view text);
std::string normalized(std::string value);
std::string base_texture(const content& files, const std::string& material);

}
