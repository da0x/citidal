#pragma once

#include "engine/game.hpp"
#include "engines/id-tech-3/content.hpp"

#include <future>
#include <memory>
#include <optional>
#include <unordered_map>

namespace id_tech_3 {

class quake_3_arena final : public engine::game {
public:
    static std::unique_ptr<quake_3_arena> open(const std::filesystem::path& root, std::string& error);

    std::string slug() const override { return "quake-3-arena"; }
    std::string name() const override { return "Quake III Arena"; }
    std::filesystem::path location() const override { return root; }

    const std::vector<engine::surface>& surfaces() const override { return all_surfaces; }
    std::vector<engine::shelf> shelves() const override;
    std::optional<engine::art> original(const std::string& surface, int at_least) const override;

    bool levels_ready() const override;
    std::vector<engine::level> levels_using(const std::string& surface) const override;

    std::vector<engine::edition> editions() const override { return {{"quake-3-arena", "Quake III Arena"}}; }
    engine::outcome build(const pack::pack& pack, const std::filesystem::path& staging) const override;
    std::filesystem::path installed_pack() const override;
    engine::outcome install(const std::filesystem::path& staging) const override;
    engine::outcome uninstall() const override;
    engine::outcome launch(const std::string& edition, const std::string& level) const override;

private:
    using map_index = std::unordered_map<std::string, std::vector<std::string>>;

    std::filesystem::path root;
    std::optional<content> files;
    std::vector<engine::surface> all_surfaces;
    mutable std::future<map_index> index_job;
    mutable std::optional<map_index> index;
};

}
