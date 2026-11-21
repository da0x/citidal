#include "engines/id-tech-3/quake-3-arena.hpp"

#include "platform/install.hpp"
#include "platform/process.hpp"

#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <set>

namespace id_tech_3 {

namespace {

constexpr const char* pack_file = "zz-citadel.pk3";
constexpr int steam_app = 2200;
constexpr int shader_lump = 1;

const char* const art_folders[] = {"textures/", "models/mapobjects/", "gfx/", "menu/", "levelshots/"};

std::string without_extension(const std::string& path) {
    auto dot = path.rfind('.');
    return dot == std::string::npos ? path : path.substr(0, dot);
}

std::string extension_of(const std::string& path) {
    auto dot = path.rfind('.');
    return dot == std::string::npos ? std::string() : path.substr(dot + 1);
}

void append(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<std::uint8_t>*>(context);
    auto* bytes = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

std::vector<std::uint8_t> encode(const image::picture& picture, const std::string& extension, bool keep_alpha) {
    std::vector<std::uint8_t> out;
    if (extension == "jpg") {
        stbi_write_jpg_to_func(append, &out, picture.width, picture.height, 4, picture.pixels.data(), 92);
        return out;
    }
    stbi_write_tga_with_rle = 0;
    if (keep_alpha) {
        stbi_write_tga_to_func(append, &out, picture.width, picture.height, 4, picture.pixels.data());
        return out;
    }
    std::vector<std::uint8_t> solid;
    solid.reserve(picture.pixels.size() / 4 * 3);
    for (std::size_t i = 0; i < picture.pixels.size(); i += 4) solid.insert(solid.end(), &picture.pixels[i], &picture.pixels[i] + 3);
    stbi_write_tga_to_func(append, &out, picture.width, picture.height, 3, solid.data());
    return out;
}

}

std::unique_ptr<quake_3_arena> quake_3_arena::open(const std::filesystem::path& root, std::string& error) {
    auto game = std::unique_ptr<quake_3_arena>(new quake_3_arena());
    game->root = root;
    game->files = content::mount(root / "baseq3", pack_file, error);
    if (!game->files) return nullptr;

    std::set<std::string> named;
    for (const char* extension : {".tga", ".jpg"}) {
        for (const char* folder : art_folders) {
            for (const auto& path : game->files->files(folder, extension)) {
                const auto name = without_extension(path);
                if (!named.insert(name).second) continue;
                game->all_surfaces.push_back({path, name, game->files->owner(path)});
            }
        }
    }
    std::sort(game->all_surfaces.begin(), game->all_surfaces.end(),
              [](const engine::surface& a, const engine::surface& b) { return a.name < b.name; });

    const content* source = &*game->files;
    game->index_job = std::async(std::launch::async, [source] {
        map_index uses;
        const auto shaders = shader_images(*source);
        for (const auto& path : source->files("maps/", ".bsp")) {
            auto bytes = source->read(path);
            if (!bytes || bytes->size() < 8 + 17 * 8 || std::memcmp(bytes->data(), "IBSP", 4) != 0) continue;
            std::int32_t offset = 0, length = 0;
            std::memcpy(&offset, bytes->data() + 8 + shader_lump * 8, 4);
            std::memcpy(&length, bytes->data() + 12 + shader_lump * 8, 4);
            if (offset < 0 || length < 0 || std::size_t(offset) + std::size_t(length) > bytes->size()) continue;
            const std::string map = path.substr(5, path.size() - 9);
            std::set<std::string> images;
            for (std::int32_t at = offset; at + 72 <= offset + length; at += 72) {
                std::string shader(reinterpret_cast<const char*>(bytes->data() + at), strnlen(reinterpret_cast<const char*>(bytes->data() + at), 64));
                std::transform(shader.begin(), shader.end(), shader.begin(), [](unsigned char c) { return std::tolower(c); });
                if (auto found = shaders.find(shader); found != shaders.end())
                    images.insert(found->second.begin(), found->second.end());
                else
                    images.insert(shader);
            }
            for (const auto& image : images) uses[image].push_back(map);
        }
        return uses;
    });
    return game;
}

std::vector<engine::shelf> quake_3_arena::shelves() const {
    return {
        {"Banners and flags", {"banner", "flag"}},
        {"Logos and signs", {"logo", "sign"}},
        {"Screens", {"screen", "comp", "monitor"}},
        {"Decals", {"decal"}},
        {"Menu and level pictures", {"menu/", "levelshots/"}},
    };
}

std::optional<engine::art> quake_3_arena::original(const std::string& surface, int at_least) const {
    auto bytes = files->read(surface);
    if (!bytes) return std::nullopt;
    std::string error;
    auto picture = image::decode(*bytes, error);
    if (!picture) return std::nullopt;
    engine::art result;
    result.width = picture->width;
    result.height = picture->height;
    result.transparent = image::has_transparency(*picture);
    std::string kind = extension_of(surface);
    std::transform(kind.begin(), kind.end(), kind.begin(), [](unsigned char c) { return std::toupper(c); });
    result.detail = std::to_string(result.width) + " × " + std::to_string(result.height) + "  " + kind + "  " + surface;
    if (at_least > 0 && std::max(picture->width, picture->height) > at_least * 2) {
        const double scale = double(at_least) / std::max(picture->width, picture->height);
        *picture = image::resize(*picture, std::max(1, int(picture->width * scale)), std::max(1, int(picture->height * scale)));
    }
    result.picture = std::move(*picture);
    return result;
}

bool quake_3_arena::levels_ready() const {
    if (index) return true;
    if (!index_job.valid() || index_job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
    index = index_job.get();
    return true;
}

std::vector<engine::level> quake_3_arena::levels_using(const std::string& surface) const {
    std::vector<engine::level> levels;
    if (!levels_ready()) return levels;
    if (auto found = index->find(without_extension(surface)); found != index->end())
        for (const auto& map : found->second) levels.push_back({map, "quake-3-arena"});
    return levels;
}

engine::outcome quake_3_arena::build(const pack::pack& pack, const std::filesystem::path& staging) const {
    std::error_code code;
    std::filesystem::create_directories(staging, code);
    if (code) return {false, "Can't make " + staging.string() + ": " + code.message()};

    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> written;
    for (const auto& item : pack.replacements()) {
        auto original = this->original(item.surface, 0);
        if (!original) return {false, "Can't read the game's picture for " + item.surface};
        std::string error;
        auto photo = image::load(pack.picture_file(item.picture), error);
        if (!photo) return {false, "Can't read the picture for " + item.surface + ": " + error};
        auto rendered = pack::render(item, *photo, original->picture);
        const bool alpha = original->transparent || image::has_transparency(rendered);
        written.emplace_back(item.surface, encode(rendered, extension_of(item.surface), alpha));
    }
    if (written.empty()) return {false, "The pack has no pictures yet"};

    const auto built = staging / pack_file;
    std::string error;
    if (!zip::write(built, written, error)) return {false, error};

    auto packed = zip::archive::open(built, error);
    if (!packed) return {false, "The pack can't be read back: " + error};
    std::size_t matched = 0;
    for (const auto& entry : packed->entries()) {
        auto found = std::find_if(written.begin(), written.end(), [&](const auto& file) { return file.first == entry.path; });
        auto bytes = packed->read(entry);
        if (found == written.end() || !bytes || *bytes != found->second)
            return {false, "The pack's copy of " + entry.path + " doesn't match what was written"};
        ++matched;
    }
    if (matched != written.size()) return {false, "The pack holds " + std::to_string(matched) + " of " + std::to_string(written.size()) + " pictures"};
    return {true, "Built " + std::to_string(written.size()) + (written.size() == 1 ? " picture" : " pictures")};
}

std::filesystem::path quake_3_arena::installed_pack() const {
    return root / "baseq3" / pack_file;
}

engine::outcome quake_3_arena::install(const std::filesystem::path& staging) const {
    std::error_code code;
    std::filesystem::copy_file(staging / pack_file, installed_pack(), std::filesystem::copy_options::overwrite_existing, code);
    if (code) return {false, "Can't copy the pack into the game: " + code.message()};
    return {true, std::string("The pack is in the game, as baseq3/") + pack_file};
}

engine::outcome quake_3_arena::uninstall() const {
    std::error_code code;
    if (!std::filesystem::remove(installed_pack(), code)) {
        if (code) return {false, code.message()};
        return {true, "The pack wasn't in the game"};
    }
    return {true, "Took the pack out of the game"};
}

engine::outcome quake_3_arena::launch(const std::string&, const std::string& level) const {
    std::vector<std::string> arguments;
    if (!level.empty()) arguments = {"+map", level};
    if (!platform::launch(platform::steam_launch(steam_app, arguments))) return {false, "Steam didn't start"};
    return {true, "Starting Quake III Arena" + (level.empty() ? std::string() : " on " + level)};
}

}
