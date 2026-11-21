#pragma once

#include "engines/source-engine/vpk.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace source_engine {

// Every file the game can see, looked up the way its gameinfo.txt search paths say:
// the first source that has a path wins.
class content {
public:
    static std::optional<content> mount(const std::filesystem::path& root, const std::string& game_folder, std::string& error);

    bool contains(const std::string& path) const;
    std::optional<std::vector<std::uint8_t>> read(const std::string& path, std::uint64_t offset = 0,
                                                  std::uint64_t length = std::numeric_limits<std::uint64_t>::max()) const;
    std::string owner(const std::string& path) const;
    std::vector<std::string> files(std::string_view prefix, std::string_view suffix) const;
    std::size_t source_count() const { return sources.size(); }

private:
    struct source {
        std::string owner;
        std::filesystem::path folder;
        std::optional<vpk::archive> archive;
        std::vector<std::filesystem::path> loose;
    };
    struct location {
        std::uint32_t source = 0;
        std::uint32_t index = 0;
    };

    std::vector<source> sources;
    std::unordered_map<std::string, location> index;
};

}
