#pragma once

#include "platform/platform.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace platform {

using environment = std::vector<std::pair<std::string, std::string>>;

std::vector<std::filesystem::path> steam_libraries();
std::filesystem::path documents_folder();
std::filesystem::path pictures_folder();
std::filesystem::path cache_folder();
std::filesystem::path interface_font(bool bold);

std::filesystem::path source_vpk_tool(const std::filesystem::path& game_root);
environment source_tool_environment(const std::filesystem::path& game_root);
std::vector<std::string> steam_launch(int app, const std::vector<std::string>& arguments);

}
