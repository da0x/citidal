#pragma once

#include "engine/game.hpp"
#include "engines/source-engine/content.hpp"
#include "engines/source-engine/maps.hpp"
#include "engines/source-engine/vtf.hpp"

#include <future>
#include <memory>
#include <optional>

namespace source_engine {

// Half-Life 2 and its two episodes share one install, and all three mount
// hl2/custom/ ahead of their own content, so one pack reaches every one of them.
class half_life_2 final : public engine::game {
public:
    static std::unique_ptr<half_life_2> open(const std::filesystem::path& root, std::string& error);

    std::string slug() const override { return "half-life-2"; }
    std::string name() const override { return "Half-Life 2"; }
    std::filesystem::path location() const override { return root; }

    const std::vector<engine::surface>& surfaces() const override { return all_surfaces; }
    std::vector<engine::shelf> shelves() const override;
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

    std::filesystem::path root;
    std::optional<content> files;
    std::vector<engine::surface> all_surfaces;
    std::vector<engine::edition> available_editions;
    mutable std::future<map_index> index_job;
    mutable std::optional<map_index> index;
};

}
