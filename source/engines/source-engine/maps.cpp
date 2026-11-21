#include "engines/source-engine/maps.hpp"

#include "engines/source-engine/material.hpp"

#include <algorithm>
#include <cstring>
#include <set>

namespace source_engine {

namespace {

constexpr int entity_lump = 0;
constexpr int texture_name_lump = 43;
constexpr std::size_t header_size = 8 + 64 * 16;

bool is_integer(std::string_view text) {
    if (text.starts_with("-")) text.remove_prefix(1);
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); });
}

// A map's brush materials can be copies made for its cubemaps, named
// maps/<map>/<material>_<x>_<y>_<z>. This recovers the material they copy.
std::string original_material(std::string name, const std::string& map) {
    name = normalized(name);
    const std::string prefix = "maps/" + map + "/";
    if (!name.starts_with(prefix)) return name;
    name.erase(0, prefix.size());
    for (int i = 0; i < 3; ++i) {
        auto cut = name.rfind('_');
        if (cut == std::string::npos || !is_integer(std::string_view(name).substr(cut + 1))) return name;
        name.resize(cut);
    }
    return name;
}

}

map_index map_index::build(const content& files) {
    map_index result;
    for (const auto& path : files.files("maps/", ".bsp")) {
        auto header = files.read(path, 0, header_size);
        if (!header || header->size() < header_size || std::memcmp(header->data(), "VBSP", 4) != 0) {
            ++result.skipped;
            continue;
        }
        auto lump = [&](int number) -> std::vector<std::uint8_t> {
            std::int32_t offset = 0, length = 0;
            std::memcpy(&offset, header->data() + 8 + number * 16, 4);
            std::memcpy(&length, header->data() + 12 + number * 16, 4);
            if (offset <= 0 || length <= 0) return {};
            auto bytes = files.read(path, std::uint64_t(offset), std::uint64_t(length));
            if (!bytes || (bytes->size() >= 4 && std::memcmp(bytes->data(), "LZMA", 4) == 0)) return {};
            return *bytes;
        };

        std::string name = path.substr(5, path.size() - 9);
        std::set<std::string> materials;

        auto names = lump(texture_name_lump);
        for (std::size_t start = 0; start < names.size();) {
            auto end = std::find(names.begin() + std::ptrdiff_t(start), names.end(), 0);
            std::string entry(names.begin() + std::ptrdiff_t(start), end);
            if (!entry.empty()) materials.insert(original_material(entry, name));
            start = std::size_t(end - names.begin()) + 1;
        }

        auto entities = lump(entity_lump);
        auto tokens = keyvalue_tokens(std::string_view(reinterpret_cast<const char*>(entities.data()), entities.size()));
        for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
            auto key = normalized(tokens[i]);
            if (key == "texture" || key == "material") materials.insert(normalized(tokens[i + 1]));
        }

        if (names.empty() && entities.empty()) {
            ++result.skipped;
            continue;
        }
        const auto number = std::uint32_t(result.maps.size());
        result.maps.push_back({name, files.owner(path)});
        for (const auto& material : materials) result.uses[material].push_back(number);
    }
    return result;
}

std::vector<map_file> map_index::using_material(const std::string& material_name) const {
    std::vector<map_file> found;
    auto entry = uses.find(material_name);
    if (entry == uses.end()) return found;
    for (auto number : entry->second) found.push_back(maps[number]);
    return found;
}

}
