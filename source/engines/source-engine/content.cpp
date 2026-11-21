#include "engines/source-engine/content.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace source_engine {

namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

struct search_path {
    std::string kind;
    std::string value;
};

std::vector<search_path> search_paths(const std::string& text) {
    std::vector<search_path> paths;
    auto start = lowercase(text).find("searchpaths");
    if (start == std::string::npos) return paths;
    auto open = text.find('{', start);
    if (open == std::string::npos) return paths;
    std::istringstream lines(text.substr(open + 1));
    for (std::string line; std::getline(lines, line);) {
        if (auto comment = line.find("//"); comment != std::string::npos) line.erase(comment);
        std::istringstream words(line);
        search_path path;
        words >> path.kind >> path.value;
        if (path.kind == "}") break;
        if (!path.kind.empty() && !path.value.empty()) paths.push_back(path);
    }
    return paths;
}

bool is_game_path(const std::string& kind) {
    std::istringstream parts(kind);
    for (std::string part; std::getline(parts, part, '+');)
        if (part == "game") return true;
    return false;
}

}

std::optional<content> content::mount(const std::filesystem::path& root, const std::string& game_folder, std::string& error) {
    std::ifstream in(root / game_folder / "gameinfo.txt");
    if (!in) {
        error = "No gameinfo.txt in " + (root / game_folder).string();
        return std::nullopt;
    }
    std::stringstream text;
    text << in.rdbuf();

    content result;
    for (auto path : search_paths(text.str())) {
        if (!is_game_path(path.kind) || path.value.find("custom/") != std::string::npos) continue;
        const std::string marker = "|all_source_engine_paths|";
        if (path.value.starts_with(marker)) path.value.erase(0, marker.size());
        if (path.value.find('|') != std::string::npos) continue;
        if (path.value.find("sound") != std::string::npos) continue;

        const std::string owner = path.value.substr(0, path.value.find('/'));
        if (path.value.ends_with(".vpk")) {
            auto directory_file = root / (path.value.substr(0, path.value.size() - 4) + "_dir.vpk");
            if (std::filesystem::exists(directory_file) && !result.add_archive(directory_file, owner, error)) return std::nullopt;
        } else {
            result.add_folder(root / path.value, owner);
        }
    }
    if (result.sources.empty()) {
        error = "gameinfo.txt in " + game_folder + " names no content that is installed";
        return std::nullopt;
    }
    return result;
}

std::optional<content> content::mount_folders(const std::filesystem::path& root, const std::vector<std::string>& folders,
                                              std::string& error) {
    content result;
    for (const auto& folder : folders) {
        if (!std::filesystem::is_directory(root / folder)) continue;
        if (auto directory_file = root / folder / "pak01_dir.vpk"; std::filesystem::exists(directory_file))
            if (!result.add_archive(directory_file, folder, error)) return std::nullopt;
        result.add_folder(root / folder, folder);
    }
    if (result.sources.empty()) {
        error = "None of the game's folders are in " + root.string();
        return std::nullopt;
    }
    return result;
}

bool content::add_archive(const std::filesystem::path& directory_file, const std::string& owner, std::string& error) {
    auto opened = vpk::archive::open(directory_file, error);
    if (!opened) return false;
    const auto number = std::uint32_t(sources.size());
    const auto& entries = opened->entries();
    for (std::uint32_t i = 0; i < entries.size(); ++i) index.try_emplace(entries[i].path, location{number, i});
    sources.push_back({owner, {}, std::move(opened), {}});
    return true;
}

void content::add_folder(const std::filesystem::path& folder, const std::string& owner) {
    source item{owner, folder, std::nullopt, {}};
    for (const char* top : {"materials", "maps"}) {
        std::error_code code;
        if (!std::filesystem::is_directory(folder / top, code)) continue;
        for (auto it = std::filesystem::recursive_directory_iterator(folder / top, code);
             it != std::filesystem::recursive_directory_iterator(); it.increment(code)) {
            if (code) break;
            if (it->is_regular_file()) item.loose.push_back(std::filesystem::relative(it->path(), folder));
        }
    }
    const auto number = std::uint32_t(sources.size());
    for (std::uint32_t i = 0; i < item.loose.size(); ++i) index.try_emplace(lowercase(item.loose[i].generic_string()), location{number, i});
    sources.push_back(std::move(item));
}

bool content::contains(const std::string& path) const {
    return index.contains(path);
}

std::optional<std::vector<std::uint8_t>> content::read(const std::string& path, std::uint64_t offset, std::uint64_t length) const {
    auto found = index.find(path);
    if (found == index.end()) return std::nullopt;
    const source& from = sources[found->second.source];
    if (from.archive) {
        const auto& item = from.archive->entries()[found->second.index];
        auto bytes = from.archive->read(item, offset, length);
        if (bytes.empty() && item.size() > offset) return std::nullopt;
        return bytes;
    }
    std::ifstream in(from.folder / from.loose[found->second.index], std::ios::binary | std::ios::ate);
    if (!in) return std::nullopt;
    const std::uint64_t size = std::uint64_t(in.tellg());
    if (offset >= size) return std::vector<std::uint8_t>{};
    length = std::min(length, size - offset);
    std::vector<std::uint8_t> bytes(length);
    in.seekg(std::streamoff(offset));
    in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(length));
    if (!in) return std::nullopt;
    return bytes;
}

std::string content::owner(const std::string& path) const {
    auto found = index.find(path);
    return found == index.end() ? std::string() : sources[found->second.source].owner;
}

std::vector<std::string> content::files(std::string_view prefix, std::string_view suffix) const {
    std::vector<std::string> paths;
    for (const auto& [path, where] : index)
        if (path.starts_with(prefix) && path.ends_with(suffix)) paths.push_back(path);
    std::sort(paths.begin(), paths.end());
    return paths;
}

}
