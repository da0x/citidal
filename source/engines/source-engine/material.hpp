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
// A change to a material: its $decalscale divided by `smaller`, so a decal drawn from a
// texture that many times as wide stays the size it was, and its $basetexture pointed at
// a texture of its own when it isn't empty.
struct material_change {
    int smaller = 1;
    std::string base_texture;
};

std::optional<std::string> changed_material(const content& files, const std::string& material, const material_change& change);

}
