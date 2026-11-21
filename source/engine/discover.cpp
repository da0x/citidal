#include "engine/game.hpp"

#include "engines/id-tech-3/quake-3-arena.hpp"
#include "engines/source-engine/half-life-2.hpp"
#include "platform/install.hpp"

namespace engine {

std::vector<std::unique_ptr<game>> discover(std::vector<std::string>& problems) {
    std::vector<std::unique_ptr<game>> games;
    for (const auto& library : platform::steam_libraries()) {
        std::string error;
        if (const auto root = library / "steamapps/common/Half-Life 2"; std::filesystem::exists(root / "hl2/gameinfo.txt")) {
            if (auto found = source_engine::half_life_2::open(root, error)) games.push_back(std::move(found));
            else problems.push_back(error);
        }
        if (const auto root = library / "steamapps/common/Quake 3 Arena"; std::filesystem::exists(root / "baseq3/pak0.pk3")) {
            if (auto found = id_tech_3::quake_3_arena::open(root, error)) games.push_back(std::move(found));
            else problems.push_back(error);
        }
    }
    return games;
}

}
