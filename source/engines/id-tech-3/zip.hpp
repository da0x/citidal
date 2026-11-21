#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace id_tech_3::zip {

struct entry {
    std::string path;
    std::uint16_t method = 0;
    std::uint32_t compressed_size = 0;
    std::uint32_t size = 0;
    std::uint32_t header_offset = 0;
};

// A pk3 is a zip file. Entries are stored or deflated.
class archive {
public:
    static std::optional<archive> open(const std::filesystem::path& file, std::string& error);

    const std::vector<entry>& entries() const { return items; }
    const std::filesystem::path& file() const { return location; }
    std::optional<std::vector<std::uint8_t>> read(const entry& item) const;

private:
    std::filesystem::path location;
    std::vector<entry> items;
};

// Writes a zip of stored entries, which every Quake III engine reads.
bool write(const std::filesystem::path& file,
           const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files, std::string& error);

}
