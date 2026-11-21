#pragma once

#include "engine/game.hpp"
#include "engines/source-engine/content.hpp"
#include "engines/source-engine/maps.hpp"
#include "engines/source-engine/vtf.hpp"

#include <future>
#include <memory>
#include <optional>

namespace source_engine {

struct edition_info {
    std::string folder;
    std::string name;
    int app = 0;
};

// What makes one Source game different from another: where Steam installs it, its game
// folders, where a pack goes and how it is made, and its shelves. An edition with no
// Steam app is a folder of content that is played from the first edition.
struct description {
    std::string slug;
    std::string name;
    std::string steam_folder;
    std::vector<edition_info> editions;
    std::vector<std::string> folders;
    std::string pack_file;
    bool writes_own_pack = false;
    std::vector<engine::shelf> shelves;
};

const std::vector<description>& known_games();

class source_game final : public engine::game {
public:
    static std::unique_ptr<source_game> open(const description& kind, const std::filesystem::path& root, std::string& error);

    std::string slug() const override { return kind.slug; }
    std::string name() const override { return kind.name; }
    std::filesystem::path location() const override { return root; }

    const std::vector<engine::surface>& surfaces() const override { return all_surfaces; }
    std::vector<engine::shelf> shelves() const override { return kind.shelves; }
    std::optional<engine::art> original(const std::string& surface, int at_least) const override;

    bool levels_ready() const override;
    std::vector<engine::level> levels_using(const std::string& surface) const override;

    std::vector<engine::edition> editions() const override { return available_editions; }
    engine::outcome build(const pack::pack& pack, const std::filesystem::path& staging) const override;
    std::filesystem::path installed_pack() const override;
    engine::outcome install(const std::filesystem::path& staging) const override;
    engine::outcome uninstall() const override;
    engine::outcome launch(const std::string& edition, const std::string& level) const override;

private:
    struct texture {
        std::string path;
        vtf::info info;
        image::picture picture;
    };
    std::optional<texture> load_texture(const std::string& material, int at_least) const;

    const edition_info* edition_by_folder(const std::string& folder) const;
    std::string edition_name(const std::string& folder) const;

    description kind;
    std::filesystem::path root;
    std::optional<content> files;
    std::vector<engine::surface> all_surfaces;
    std::vector<engine::edition> available_editions;
    mutable std::future<map_index> index_job;
    mutable std::optional<map_index> index;
};

}
