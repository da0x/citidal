// Runs the core against the installed game: mounts it, reads and decodes its art,
// writes a texture and reads it back, and builds a pack into a folder given on the
// command line. It never touches the game's own folders.

#include "engine/game.hpp"
#include "engines/source-engine/vtf.hpp"
#include "image/image.hpp"
#include "pack/pack.hpp"

#include <stb_image_write.h>

#include <chrono>
#include <cstdio>
#include <thread>

namespace {

int failures = 0;

void expect(bool condition, const std::string& what) {
    std::printf("%s  %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition) ++failures;
}

}

int main(int count, char** arguments) {
    if (count < 3) {
        std::printf("usage: citadel_check <picture> <scratch folder>\n");
        return 2;
    }
    const std::filesystem::path picture = arguments[1];
    const std::filesystem::path scratch = arguments[2];

    std::vector<std::string> problems;
    auto games = engine::discover(problems);
    for (const auto& problem : problems) std::printf("problem: %s\n", problem.c_str());
    expect(!games.empty(), "found an installed game");
    if (games.empty()) return 1;
    auto& game = *games.front();
    std::printf("     %s at %s, %zu surfaces\n", game.name().c_str(), game.location().c_str(), game.surfaces().size());

    const std::string poster = "materials/decals/decal_posters001a.vmt";
    auto full = game.original(poster, 0);
    expect(full.has_value(), "decoded " + poster);
    if (full) std::printf("     %s\n", full->detail.c_str());
    auto small = game.original(poster, 64);
    expect(small && small->picture.width <= full->picture.width, "decoded a small level for a thumbnail");

    int decoded = 0, tried = 0;
    for (const auto& surface : game.surfaces()) {
        if (surface.name.find("poster") == std::string::npos && surface.name.find("billboard") == std::string::npos) continue;
        ++tried;
        if (game.original(surface.id, 64)) ++decoded;
    }
    std::printf("     %d of %d poster and billboard materials decoded\n", decoded, tried);
    expect(tried > 0 && decoded * 10 >= tried * 8, "most poster and billboard materials decode");

    int readable = 0;
    for (const auto& surface : game.surfaces())
        if (game.original(surface.id, 64)) ++readable;
    std::printf("     %d of %zu materials have a picture citadel can read\n", readable, game.surfaces().size());

    std::string error;
    auto photo = image::load(picture, error);
    expect(photo.has_value(), "loaded " + picture.filename().string() + (photo ? "" : ": " + error));
    if (!photo || !full) return 1;

    pack::replacement item{poster, "", {}, image::edges::weathered};
    auto rendered = pack::render(item, *photo, full->picture);
    expect(rendered.width == full->width && rendered.height == full->height, "rendered at the original's size");

    auto bytes = source_engine::vtf::encode(rendered, 0);
    auto back = source_engine::vtf::decode(bytes, 0);
    expect(back && back->width == rendered.width && back->height == rendered.height, "a written VTF reads back");
    if (back) {
        long difference = 0;
        for (std::size_t i = 0; i < back->pixels.size(); ++i)
            difference += std::abs(int(back->pixels[i]) - int(rendered.pixels[i]));
        double average = double(difference) / double(back->pixels.size());
        std::printf("     average difference after DXT: %.2f of 255\n", average);
        expect(average < 12, "compression stays close to the picture");
        auto written = scratch / "poster-as-written.png";
        stbi_write_png(written.c_str(), back->width, back->height, 4, back->pixels.data(), back->width * 4);
        auto in_game = scratch / "poster-in-game.png";
        stbi_write_png(in_game.c_str(), full->picture.width, full->picture.height, 4, full->picture.pixels.data(), full->picture.width * 4);
    }

    if (auto sign = game.original("materials/decals/decalposter002a.vmt", 0)) {
        const std::pair<image::edges, const char*> kinds[] = {
            {image::edges::clean, "clean"}, {image::edges::torn, "torn"}, {image::edges::weathered, "weathered"}};
        int opaque_clean = 0, opaque_torn = 0, opaque_weathered = 0;
        for (auto [kind, name] : kinds) {
            pack::replacement edged{"", "", {}, kind};
            auto result = pack::render(edged, *photo, sign->picture);
            int opaque = 0;
            for (std::size_t i = 3; i < result.pixels.size(); i += 4) opaque += result.pixels[i] >= 128;
            (kind == image::edges::clean ? opaque_clean : kind == image::edges::torn ? opaque_torn : opaque_weathered) = opaque;
            for (std::size_t i = 0; i < result.pixels.size(); i += 4) {
                const int a = result.pixels[i + 3];
                for (int c = 0; c < 3; ++c) result.pixels[i + std::size_t(c)] = std::uint8_t((result.pixels[i + std::size_t(c)] * a + 255 * (255 - a)) / 255);
                result.pixels[i + 3] = 255;
            }
            auto file = scratch / (std::string("sign-") + name + ".png");
            stbi_write_png(file.c_str(), result.width, result.height, 4, result.pixels.data(), result.width * 4);
        }
        std::printf("     solid pixels on the sign: clean %d, torn %d, weathered %d\n", opaque_clean, opaque_torn, opaque_weathered);
        expect(opaque_clean > opaque_torn && opaque_torn > opaque_weathered, "each edge choice keeps less of the picture than the last");
    }

    auto made = pack::pack::open(scratch / "pack");
    auto adopted = made.adopt_picture(picture, error);
    expect(adopted.has_value(), "copied the picture into a pack");
    if (adopted) made.put(poster, *adopted).edges = image::edges::torn;
    made.put("materials/console/background01_widescreen.vmt", adopted.value_or(""));
    expect(made.save(error), "saved the pack");
    auto reopened = pack::pack::open(scratch / "pack");
    expect(reopened.replacements().size() == 2, "the pack reopens with its replacements");

    auto built = game.build(reopened, scratch / "staging");
    std::printf("     %s\n", built.message.c_str());
    expect(built.ok, "built the pack");

    auto start = std::chrono::steady_clock::now();
    while (!game.levels_ready()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    auto levels = game.levels_using(poster);
    std::printf("     map index ready after %.1fs more; %s appears in %zu maps:", seconds, poster.c_str(), levels.size());
    for (const auto& level : levels) std::printf(" %s(%s)", level.name.c_str(), level.edition.c_str());
    std::printf("\n");
    expect(!levels.empty(), "found maps that use the poster");

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
