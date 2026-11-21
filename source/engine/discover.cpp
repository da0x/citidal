#include "engine/game.hpp"

#include "engines/source-engine/half-life-2.hpp"
#include "platform/install.hpp"

namespace engine {

std::vector<std::unique_ptr<game>> discover(std::vector<std::string>& problems) {
    std::vector<std::unique_ptr<game>> games;
    for (const auto& library : platform::steam_libraries()) {
        const auto root = library / "steamapps/common/Half-Life 2";
        if (!std::filesystem::exists(root / "hl2/gameinfo.txt")) continue;
        std::string error;
        if (auto found = source_engine::half_life_2::open(root, error)) games.push_back(std::move(found));
        else problems.push_back(error);
    }
    return games;
}

}
