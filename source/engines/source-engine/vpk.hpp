#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace source_engine::vpk {

struct entry {
    std::string path;
    std::uint16_t archive = 0;
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    std::uint64_t preload_offset = 0;
    std::uint16_t preload_length = 0;

    std::uint64_t size() const { return std::uint64_t(preload_length) + length; }
};

class archive {
public:
    static std::optional<archive> open(const std::filesystem::path& directory_file, std::string& error);

    const std::vector<entry>& entries() const { return items; }
    const std::filesystem::path& file() const { return directory_file; }

    std::vector<std::uint8_t> read(const entry& item, std::uint64_t offset = 0,
                                   std::uint64_t length = std::numeric_limits<std::uint64_t>::max()) const;

private:
    std::filesystem::path directory_file;
    std::string chunk_prefix;
    std::uint64_t data_start = 0;
    std::vector<entry> items;
};

// Writes a single-file version 1 pack, the format Portal 2 reads, with every file's data
// after the directory.
bool write(const std::filesystem::path& file,
           const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files, std::string& error);

}
