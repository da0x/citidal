#include "engines/source-engine/source-game.hpp"

#include "engines/source-engine/material.hpp"
#include "platform/install.hpp"
#include "platform/process.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <map>

namespace source_engine {

namespace {

constexpr const char* pack_name = "citadel";
constexpr int largest_texture = 4096;

std::string material_name(const std::string& material) {
    return material.substr(10, material.size() - 14);
}

}

// Half-Life 2 and its two episodes share one install, and all three mount hl2/custom/
// ahead of their own content, so one pack reaches every one of them. Portal is built the
// same way. Portal 2 lists no content in its gameinfo.txt: the engine mounts update/,
// then each portal2_dlcN with the highest number first, then portal2/, each one's
// pak01_dir.vpk. A pack in a new portal2_dlc3 sits above everything but update/. Portal 2
// ships no vpk tool on Linux and reads version 1 packs, so citadel writes that one itself.
const std::vector<description>& known_games() {
    static const std::vector<description> games{
        {"half-life-2", "Half-Life 2", "Half-Life 2",
         {{"hl2", "Half-Life 2", 220}, {"episodic", "Episode One", 380}, {"ep2", "Episode Two", 420}},
         {},
         "hl2/custom/citadel.vpk",
         false,
         {{"Posters", {"poster"}},
          {"Signs", {"sign"}},
          {"Billboards", {"billboard"}},
          {"Screens", {"screen", "monitor"}, {"widescreen"}},
          {"Paintings", {"painting", "picture", "portrait", "photo"}},
          {"Menu backgrounds", {"console/background", "console/ep1_background", "console/ep2_background"}}}},
        {"portal", "Portal", "Portal",
         {{"portal", "Portal", 400}},
         {},
         "portal/custom/citadel.vpk",
         false,
         {{"Signs", {"sign"}},
          {"Posters", {"poster"}},
          {"Screens", {"screen", "monitor"}, {"widescreen"}},
          {"Menu backgrounds", {"console/background"}}}},
        {"portal-2", "Portal 2", "Portal 2",
         {{"portal2", "Portal 2", 620}, {"portal2_dlc1", "Co-op", 0}, {"portal2_dlc2", "Peer Review", 0}, {"update", "Update", 0}},
         {"update", "portal2_dlc2", "portal2_dlc1", "portal2"},
         "portal2_dlc3/pak01_dir.vpk",
         true,
         {{"Signs", {"sign"}},
          {"Posters", {"poster"}},
          {"Screens", {"screen", "monitor"}, {"widescreen"}},
          {"Menu backgrounds", {"console/portal2_product"}}}},
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
        if (edition.app) game->available_editions.push_back({edition.folder, edition.name});
        widest = edition.folder;
    }
    if (widest.empty()) {
        error = "No " + kind.name + " game folders in " + root.string();
        return nullptr;
    }

    game->files = kind.folders.empty() ? content::mount(root, widest, error) : content::mount_folders(root, kind.folders, error);
    if (!game->files) return nullptr;

    for (const auto& path : game->files->files("materials/", ".vmt")) {
        std::string name = path.substr(10, path.size() - 14);
        game->all_surfaces.push_back({path, name, game->edition_name(game->files->owner(path))});
    }

    const content* source = &*game->files;
    std::vector<std::string> materials;
    for (const auto& surface : game->all_surfaces) materials.push_back(surface.id);
    game->survey_job = std::async(std::launch::async, [source, materials = std::move(materials)] {
                           survey found{map_index::build(*source), {}};
                           for (const auto& material : materials)
                               if (auto texture = base_texture(*source, material); !texture.empty())
                                   found.drawn_by[texture].push_back(material);
                           return found;
                       }).share();
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

const source_game::survey* source_game::surveyed() const {
    if (!survey_job.valid() || survey_job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;
    return &survey_job.get();
}

bool source_game::levels_ready() const {
    return surveyed() != nullptr;
}

// Models, overlays and screens stretch a texture over the same place whatever its size,
// so a sharper one only looks sharper. The engine sizes decals and wall textures by their
// pixels: decals are given a smaller $decalscale to match, and a texture any map paints
// on its walls keeps the game's size.
engine::resolution source_game::resolution_for(const pack::replacement& item, int picture_width, int picture_height) const {
    auto path = base_texture(*files, item.surface);
    auto header = path.empty() ? std::nullopt : files->read(path, 0, vtf::header_read_size);
    auto info = header ? vtf::inspect(*header) : std::nullopt;
    if (!info) return {};
    const int width = info->width, height = info->height;
    const auto* known = surveyed();
    if (!known) return {width, height, "Looking through the maps to see how big it can go"};
    if (auto users = known->drawn_by.find(path); users != known->drawn_by.end())
        for (const auto& material : users->second)
            if (known->maps.on_walls(material_name(material)))
                return {width, height, "This texture is also painted on map walls, where more pixels would draw it bigger, so it keeps the game's size"};
    const int scale = image::sharpest_scale(picture_width, picture_height, width, height, item.placement, largest_texture);
    if (scale > 1) return {width * scale, height * scale, ""};
    if (width * 2 > largest_texture || height * 2 > largest_texture) return {width, height, "The game's texture is already as big as the game takes"};
    return {width, height, "Your picture has no more detail than the game's size"};
}

std::vector<engine::level> source_game::levels_using(const std::string& surface) const {
    std::vector<engine::level> levels;
    const auto* known = surveyed();
    if (!known) return levels;
    for (const auto& map : known->maps.using_material(material_name(surface))) levels.push_back({map.name, map.owner});
    return levels;
}

engine::outcome source_game::build(const pack::pack& pack, const std::filesystem::path& staging) const {
    std::error_code code;
    const auto folder = staging / pack_name;
    std::filesystem::remove_all(folder, code);
    std::filesystem::remove(staging / (std::string(pack_name) + ".vpk"), code);
    std::filesystem::create_directories(folder, code);
    if (code) return {false, "Can't make " + folder.string() + ": " + code.message()};

    // Several materials can draw one texture. The first surface in the pack to use a
    // texture replaces it in place, along with every material that draws it. A later
    // surface on the same texture gets a copy of its own under materials/citadel/, and its
    // material is pointed at that copy.
    const auto& known = survey_job.get();
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> staged;
    std::unordered_map<std::string, std::string> texture_for;
    std::map<std::string, material_change> changes;
    std::size_t sharper = 0;
    for (const auto& item : pack.replacements()) {
        auto original = load_texture(item.surface, 0);
        if (!original) return {false, "Can't read the game's texture for " + item.surface};
        std::string error;
        auto photo = image::load(pack.picture_file(item.picture), error);
        if (!photo) return {false, "Can't read the picture for " + item.surface + ": " + error};

        auto size = resolution_for(item, photo->width, photo->height);
        if (size.width <= 0) return {false, "Can't read the game's texture for " + item.surface};
        const int scale = size.width / original->info.width;
        if (scale > 1) ++sharper;
        auto bytes = vtf::encode(pack::render(item, *photo, original->picture, size.width, size.height), original->info.flags);

        if (texture_for.try_emplace(original->path, item.surface).second) {
            staged.emplace_back(original->path, std::move(bytes));
            if (auto users = known.drawn_by.find(original->path); scale > 1 && users != known.drawn_by.end())
                for (const auto& material : users->second) changes[material].smaller = scale;
        } else {
            const std::string own = "citadel/" + material_name(item.surface);
            staged.emplace_back("materials/" + own + ".vtf", std::move(bytes));
            changes[item.surface] = {scale, own};
        }
    }
    for (const auto& [material, change] : changes) {
        auto text = changed_material(*files, material, change);
        if (!text) return {false, "Can't read the material " + material};
        staged.emplace_back(material, std::vector<std::uint8_t>(text->begin(), text->end()));
    }
    for (const auto& [path, bytes] : staged) {
        auto destination = folder / path;
        std::filesystem::create_directories(destination.parent_path(), code);
        std::ofstream out(destination, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!out) return {false, "Can't write " + destination.string()};
    }
    if (staged.empty()) return {false, "The pack has no pictures yet"};

    const auto built = staging / (std::string(pack_name) + ".vpk");
    std::string problem;
    std::string tool_output;
    if (kind.writes_own_pack) {
        if (!vpk::write(built, staged, problem)) return {false, problem};
    } else {
        tool_output = platform::run({platform::source_vpk_tool(root).string(), pack_name}, staging,
                                    platform::source_tool_environment(root)).output;
    }

    // The Linux vpk tool crashes on exit even after it has written a good pack, so its
    // exit code says nothing. Either way, the pack is read back and compared with what was
    // staged.
    auto packed = vpk::archive::open(built, problem);
    if (!packed) return {false, kind.writes_own_pack ? "The pack can't be read back: " + problem : "Valve's vpk tool made no pack: " + tool_output};
    std::size_t matched = 0;
    for (const auto& entry : packed->entries()) {
        auto found = std::find_if(staged.begin(), staged.end(), [&](const auto& file) { return file.first == entry.path; });
        if (found == staged.end() || found->second != packed->read(entry))
            return {false, "The pack's copy of " + entry.path + " doesn't match what was staged"};
        ++matched;
    }
    if (matched != staged.size())
        return {false, "The pack holds " + std::to_string(matched) + " of " + std::to_string(staged.size()) + " files"};
    const auto written = pack.replacements().size();
    return {true, "Built " + std::to_string(written) + (written == 1 ? " texture" : " textures") +
                      (sharper ? ", " + std::to_string(sharper) + " sharper than the game's own" : "") +
                      (kind.writes_own_pack ? "" : ", with Valve's vpk tool")};
}

std::filesystem::path source_game::installed_pack() const {
    return root / kind.pack_file;
}

engine::outcome source_game::install(const std::filesystem::path& staging) const {
    std::error_code code;
    const auto built = staging / (std::string(pack_name) + ".vpk");
    std::filesystem::create_directories(installed_pack().parent_path(), code);
    std::filesystem::copy_file(built, installed_pack(), std::filesystem::copy_options::overwrite_existing, code);
    if (code) return {false, "Can't copy the pack into the game: " + code.message()};
    return {true, "The pack is in the game, as " + kind.pack_file};
}

engine::outcome source_game::uninstall() const {
    std::error_code code;
    if (!std::filesystem::remove(installed_pack(), code)) {
        if (code) return {false, code.message()};
        return {true, "The pack wasn't in the game"};
    }
    if (kind.writes_own_pack) std::filesystem::remove(installed_pack().parent_path(), code);
    return {true, "Took the pack out of the game"};
}

engine::outcome source_game::launch(const std::string& edition, const std::string& level) const {
    auto* found = edition_by_folder(edition);
    if (!found) return {false, "Unknown edition " + edition};
    if (!found->app) found = &kind.editions.front();
    std::vector<std::string> arguments;
    if (!level.empty()) arguments = {"+map", level};
    if (!platform::launch(platform::steam_launch(found->app, arguments)))
        return {false, "Steam didn't start"};
    return {true, std::string("Starting ") + found->name + (level.empty() ? "" : " on " + level)};
}

}
