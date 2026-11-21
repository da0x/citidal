#pragma once

#include "engines/source-engine/content.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace source_engine {

struct map_file {
    std::string name;
    std::string owner;
};

// Which maps use each material, read from each map's brush texture table and the
// decals and overlays its entities place, and which materials its faces are painted with.
class map_index {
public:
    static map_index build(const content& files);

    std::vector<map_file> using_material(const std::string& material_name) const;
    // Whether any map paints the material on its walls and floors, where the engine draws
    // a texture bigger when it has more pixels.
    bool on_walls(const std::string& material_name) const { return walls.contains(material_name); }
    std::size_t map_count() const { return maps.size(); }
    std::size_t skipped_count() const { return skipped; }

private:
    std::vector<map_file> maps;
    std::unordered_map<std::string, std::vector<std::uint32_t>> uses;
    std::unordered_set<std::string> walls;
    std::size_t skipped = 0;
};

}
