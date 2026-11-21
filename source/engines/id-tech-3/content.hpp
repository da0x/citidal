#pragma once

#include "engines/id-tech-3/zip.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace id_tech_3 {

// Every file in a game folder's pk3s. The pk3s load in order of their names and a later
// one wins, which is how a pack named to sort last replaces the game's own art.
class content {
public:
    static std::optional<content> mount(const std::filesystem::path& folder, const std::string& skip, std::string& error);

    bool contains(const std::string& path) const { return index.contains(path); }
    std::optional<std::vector<std::uint8_t>> read(const std::string& path) const;
    std::string owner(const std::string& path) const;
    std::vector<std::string> files(std::string_view prefix, std::string_view suffix) const;

private:
    struct location {
        std::uint32_t archive = 0;
        std::uint32_t index = 0;
    };
    std::vector<zip::archive> archives;
    std::unordered_map<std::string, location> index;
};

// Shader names mapped to the images their stages and editor image use, each image
// without its extension, because the engine finds a .tga or a .jpg under one name.
std::unordered_map<std::string, std::vector<std::string>> shader_images(const content& files);

}
