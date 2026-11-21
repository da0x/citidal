#pragma once

#include "engines/source-engine/content.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace source_engine {

std::vector<std::string> keyvalue_tokens(std::string_view text);
std::string normalized(std::string value);
std::string base_texture(const content& files, const std::string& material);
// The material's text with its $decalscale divided by `times`, so that a decal drawn
// from a texture `times` as wide stays the size it was.
std::optional<std::string> with_decal_scale(const content& files, const std::string& material, int times);

}
