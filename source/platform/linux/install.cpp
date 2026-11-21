#include "platform/install.hpp"

#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace platform {

namespace {

std::filesystem::path home() {
    const char* value = std::getenv("HOME");
    return value ? std::filesystem::path(value) : std::filesystem::path("/");
}

std::string read_text(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

std::vector<std::string> quoted_values(const std::string& text, const std::string& key) {
    std::vector<std::string> values;
    const std::string needle = "\"" + key + "\"";
    for (auto at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
        auto open = text.find('"', at + needle.size());
        if (open == std::string::npos) break;
        auto close = text.find('"', open + 1);
        if (close == std::string::npos) break;
        values.push_back(text.substr(open + 1, close - open - 1));
    }
    return values;
}

std::filesystem::path user_folder(const std::string& key, const std::string& fallback) {
    std::ifstream in(home() / ".config/user-dirs.dirs");
    for (std::string line; std::getline(in, line);) {
        if (!line.starts_with(key + "=")) continue;
        auto open = line.find('"');
        auto close = line.rfind('"');
        if (open == std::string::npos || close <= open) break;
        std::string value = line.substr(open + 1, close - open - 1);
        if (value.starts_with("$HOME")) return home() / value.substr(value.size() > 5 ? 6 : 5);
        return value;
    }
    return home() / fallback;
}

}

std::vector<std::filesystem::path> steam_libraries() {
    const std::vector<std::filesystem::path> roots{
        home() / ".local/share/Steam",
        home() / ".steam/steam",
        home() / ".var/app/com.valvesoftware.Steam/.local/share/Steam",
    };
    std::vector<std::filesystem::path> libraries;
    std::set<std::filesystem::path> seen;
    auto add = [&](const std::filesystem::path& library) {
        std::error_code error;
        auto real = std::filesystem::canonical(library, error);
        if (error || !std::filesystem::is_directory(real / "steamapps")) return;
        if (seen.insert(real).second) libraries.push_back(real);
    };
    for (const auto& root : roots) {
        add(root);
        for (const auto& path : quoted_values(read_text(root / "steamapps/libraryfolders.vdf"), "path"))
            add(path);
    }
    return libraries;
}

std::filesystem::path documents_folder() {
    return user_folder("XDG_DOCUMENTS_DIR", "Documents");
}

std::filesystem::path pictures_folder() {
    return user_folder("XDG_PICTURES_DIR", "Pictures");
}

std::filesystem::path cache_folder() {
    const char* value = std::getenv("XDG_CACHE_HOME");
    std::filesystem::path base = value && *value ? std::filesystem::path(value) : home() / ".cache";
    return base / "citadel";
}

std::filesystem::path interface_font(bool bold) {
    const std::vector<std::filesystem::path> candidates = bold
        ? std::vector<std::filesystem::path>{
              "/usr/share/fonts/noto/NotoSans-SemiBold.ttf",
              "/usr/share/fonts/noto/NotoSans-Bold.ttf",
              "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"}
        : std::vector<std::filesystem::path>{
              "/usr/share/fonts/noto/NotoSans-Regular.ttf",
              "/usr/share/fonts/TTF/DejaVuSans.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
    for (const auto& file : candidates)
        if (std::filesystem::exists(file)) return file;
    return {};
}

std::filesystem::path source_vpk_tool(const std::filesystem::path& game_root) {
    return game_root / "bin/vpk_linux32";
}

environment source_tool_environment(const std::filesystem::path& game_root) {
    return {{"LD_LIBRARY_PATH", (game_root / "bin").string()}};
}

std::vector<std::string> steam_launch(int app, const std::vector<std::string>& arguments) {
    std::vector<std::string> command{"steam", "-applaunch", std::to_string(app)};
    command.insert(command.end(), arguments.begin(), arguments.end());
    return command;
}

}
