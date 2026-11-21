#pragma once

#include "engines/source-engine/content.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace source_engine {

struct map_file {
    std::string name;
    std::string owner;
};

// Which maps use each material, read from each map's brush texture table and the
// decals and overlays its entities place.
class map_index {
public:
    static map_index build(const content& files);

    std::vector<map_file> using_material(const std::string& material_name) const;
    std::size_t map_count() const { return maps.size(); }
    std::size_t skipped_count() const { return skipped; }

private:
    std::vector<map_file> maps;
    std::unordered_map<std::string, std::vector<std::uint32_t>> uses;
    std::size_t skipped = 0;
};

}
