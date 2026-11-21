#include "engines/source-engine/source-game.hpp"

#include "engines/source-engine/material.hpp"
#include "platform/install.hpp"
#include "platform/process.hpp"

#include <chrono>
#include <fstream>
#include <iterator>

namespace source_engine {

namespace {

constexpr const char* pack_name = "citadel";

}

// Half-Life 2 and its two episodes share one install, and all three mount hl2/custom/
// ahead of their own content, so one pack reaches every one of them.
const std::vector<description>& known_games() {
    static const std::vector<description> games{
        {"half-life-2", "Half-Life 2", "Half-Life 2",
         {{"hl2", "Half-Life 2", 220}, {"episodic", "Episode One", 380}, {"ep2", "Episode Two", 420}},
         "hl2/custom",
         {{"Posters", {"poster"}},
          {"Signs", {"sign"}},
          {"Billboards", {"billboard"}},
          {"Screens", {"screen", "monitor"}},
          {"Paintings", {"painting", "picture", "portrait", "photo"}},
          {"Menu backgrounds", {"console/background", "console/ep1_background", "console/ep2_background"}}}},
    };
    return games;
}

const edition_info* source_game::edition_by_folder(const std::string& folder) const {
    for (const auto& edition : kind.editions)
        if (folder == edition.folder) return &edition;
    return nullptr;
}

std::string source_game::edition_name(const std::string& folder) const {
    auto* edition = edition_by_folder(folder);
    return edition ? edition->name : folder;
}

std::unique_ptr<source_game> source_game::open(const description& kind, const std::filesystem::path& root, std::string& error) {
    auto game = std::unique_ptr<source_game>(new source_game());
    game->kind = kind;
    game->root = root;

    std::string widest;
    for (const auto& edition : kind.editions) {
        if (!std::filesystem::exists(root / edition.folder / "gameinfo.txt")) continue;
        game->available_editions.push_back({edition.folder, edition.name});
        widest = edition.folder;
    }
    if (widest.empty()) {
        error = "No " + kind.name + " game folders in " + root.string();
        return nullptr;
    }

    game->files = content::mount(root, widest, error);
    if (!game->files) return nullptr;

    for (const auto& path : game->files->files("materials/", ".vmt")) {
        std::string name = path.substr(10, path.size() - 14);
        game->all_surfaces.push_back({path, name, game->edition_name(game->files->owner(path))});
    }

    const content* source = &*game->files;
    game->index_job = std::async(std::launch::async, [source] { return map_index::build(*source); });
    return game;
}

std::optional<source_game::texture> source_game::load_texture(const std::string& material, int at_least) const {
    auto path = base_texture(*files, material);
    if (path.empty()) return std::nullopt;
    auto header = files->read(path, 0, vtf::header_read_size);
    if (!header) return std::nullopt;
    auto info = vtf::inspect(*header);
    if (!info) return std::nullopt;
    const int level = vtf::level_for(*info, at_least);
    auto [offset, length] = vtf::locate(*info, level);
    auto slice = files->read(path, offset, length);
    if (!slice || slice->size() < length) return std::nullopt;
    auto picture = vtf::decode_level(*info, level, *slice);
    if (!picture) return std::nullopt;
    return texture{path, *info, std::move(*picture)};
}

std::optional<engine::art> source_game::original(const std::string& surface, int at_least) const {
    auto found = load_texture(surface, at_least);
    if (!found) return std::nullopt;
    engine::art result;
    result.width = found->info.width;
    result.height = found->info.height;
    result.transparent = (found->info.flags & (vtf::flag::one_bit_alpha | vtf::flag::eight_bit_alpha)) != 0;
    result.detail = std::to_string(result.width) + " × " + std::to_string(result.height) + "  " +
                    vtf::format_name(found->info.format) + "  " + found->path;
    result.picture = std::move(found->picture);
    return result;
}

bool source_game::levels_ready() const {
    if (index) return true;
    if (!index_job.valid() || index_job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
    index = index_job.get();
    return true;
}

std::vector<engine::level> source_game::levels_using(const std::string& surface) const {
    std::vector<engine::level> levels;
    if (!levels_ready()) return levels;
    std::string name = surface.substr(10, surface.size() - 14);
    for (const auto& map : index->using_material(name)) levels.push_back({map.name, map.owner});
    return levels;
}

engine::outcome source_game::build(const pack::pack& pack, const std::filesystem::path& staging) const {
    std::error_code code;
    const auto folder = staging / pack_name;
    std::filesystem::remove_all(folder, code);
    std::filesystem::remove(staging / (std::string(pack_name) + ".vpk"), code);
    std::filesystem::create_directories(folder, code);
    if (code) return {false, "Can't make " + folder.string() + ": " + code.message()};

    int written = 0;
    for (const auto& item : pack.replacements()) {
        auto original = load_texture(item.surface, 0);
        if (!original) return {false, "Can't read the game's texture for " + item.surface};
        std::string error;
        auto photo = image::load(pack.picture_file(item.picture), error);
        if (!photo) return {false, "Can't read the picture for " + item.surface + ": " + error};

        auto bytes = vtf::encode(pack::render(item, *photo, original->picture), original->info.flags);
        auto destination = folder / original->path;
        std::filesystem::create_directories(destination.parent_path(), code);
        std::ofstream out(destination, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!out) return {false, "Can't write " + destination.string()};
        ++written;
    }
    if (written == 0) return {false, "The pack has no pictures yet"};

    auto result = platform::run({platform::source_vpk_tool(root).string(), pack_name}, staging,
                                platform::source_tool_environment(root));
    // The Linux vpk tool crashes on exit even after it has written a good pack, so its
    // exit code says nothing. The pack is read back and compared with what was staged.
    const auto built = staging / (std::string(pack_name) + ".vpk");
    std::string problem;
    auto packed = vpk::archive::open(built, problem);
    if (!packed) return {false, "Valve's vpk tool made no pack: " + result.output};
    std::size_t matched = 0;
    for (const auto& entry : packed->entries()) {
        std::ifstream in(folder / entry.path, std::ios::binary);
        std::vector<std::uint8_t> staged((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (staged != packed->read(entry)) return {false, "The pack's copy of " + entry.path + " doesn't match what was staged"};
        ++matched;
    }
    if (matched != std::size_t(written))
        return {false, "The pack holds " + std::to_string(matched) + " of " + std::to_string(written) + " textures"};
    return {true, "Built " + std::to_string(written) + (written == 1 ? " texture" : " textures") + " with Valve's vpk tool"};
}

std::filesystem::path source_game::installed_pack() const {
    return root / kind.pack_folder / (std::string(pack_name) + ".vpk");
}

engine::outcome source_game::install(const std::filesystem::path& staging) const {
    std::error_code code;
    const auto built = staging / (std::string(pack_name) + ".vpk");
    std::filesystem::create_directories(installed_pack().parent_path(), code);
    std::filesystem::copy_file(built, installed_pack(), std::filesystem::copy_options::overwrite_existing, code);
    if (code) return {false, "Can't copy the pack into the game: " + code.message()};
    return {true, "The pack is in the game, in " + kind.pack_folder};
}

engine::outcome source_game::uninstall() const {
    std::error_code code;
    if (!std::filesystem::remove(installed_pack(), code)) {
        if (code) return {false, code.message()};
        return {true, "The pack wasn't in the game"};
    }
    return {true, "Took the pack out of the game"};
}

engine::outcome source_game::launch(const std::string& edition, const std::string& level) const {
    auto* found = edition_by_folder(edition);
    if (!found) return {false, "Unknown edition " + edition};
    std::vector<std::string> arguments;
    if (!level.empty()) arguments = {"+map", level};
    if (!platform::launch(platform::steam_launch(found->app, arguments)))
        return {false, "Steam didn't start"};
    return {true, std::string("Starting ") + found->name + (level.empty() ? "" : " on " + level)};
}

}
