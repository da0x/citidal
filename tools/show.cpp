// Prints a file from the installed game's content, as the game would find it.
// With --picture <folder>, a texture is written there as two PNGs instead: its color,
// and its see-through channel as gray.

#include "engines/source-engine/content.hpp"
#include "engines/source-engine/vtf.hpp"
#include "platform/install.hpp"

#include <stb_image_write.h>

#include <cstdio>
#include <string_view>

int main(int count, char** arguments) {
    if (count < 2) {
        std::fprintf(stderr, "usage: citadel_show <path inside the game>...\n");
        return 2;
    }
    for (const auto& library : platform::steam_libraries()) {
        const auto root = library / "steamapps/common/Half-Life 2";
        if (!std::filesystem::exists(root / "ep2/gameinfo.txt")) continue;
        std::string error;
        auto files = source_engine::content::mount(root, "ep2", error);
        if (!files) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        std::filesystem::path pictures;
        int first = 1;
        if (count > 3 && std::string_view(arguments[1]) == "--picture") {
            pictures = arguments[2];
            first = 3;
        }
        for (int i = first; i < count; ++i) {
            if (!pictures.empty()) {
                auto bytes = files->read(arguments[i]);
                auto picture = bytes ? source_engine::vtf::decode(*bytes) : std::nullopt;
                if (!picture) {
                    std::printf("%s: can't decode\n", arguments[i]);
                    continue;
                }
                auto shape = *picture;
                for (std::size_t p = 0; p < shape.pixels.size(); p += 4)
                    shape.pixels[p] = shape.pixels[p + 1] = shape.pixels[p + 2] = shape.pixels[p + 3], shape.pixels[p + 3] = 255;
                auto name = std::filesystem::path(arguments[i]).stem().string();
                stbi_write_png((pictures / (name + "-color.png")).c_str(), picture->width, picture->height, 4, picture->pixels.data(), picture->width * 4);
                stbi_write_png((pictures / (name + "-shape.png")).c_str(), shape.width, shape.height, 4, shape.pixels.data(), shape.width * 4);
                std::printf("%s: %d x %d\n", arguments[i], picture->width, picture->height);
                continue;
            }
            auto bytes = files->read(arguments[i]);
            std::printf("== %s (%s)\n", arguments[i], bytes ? files->owner(arguments[i]).c_str() : "not found");
            if (bytes) std::fwrite(bytes->data(), 1, bytes->size(), stdout);
            std::printf("\n");
        }
        return 0;
    }
    std::fprintf(stderr, "Half-Life 2 isn't installed\n");
    return 1;
}
