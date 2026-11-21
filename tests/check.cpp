// Runs the core against the installed game: mounts it, reads and decodes its art,
// writes a texture and reads it back, and builds a pack into a folder given on the
// command line. It never touches the game's own folders.

#include "engine/game.hpp"
#include "engines/id-tech-3/zip.hpp"
#include "engines/source-engine/material.hpp"
#include "engines/source-engine/vpk.hpp"
#include "engines/source-engine/vtf.hpp"
#include "image/image.hpp"
#include "pack/pack.hpp"

#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
    {
        auto packed = source_engine::vpk::archive::open(scratch / "staging" / "citadel.vpk", error);
        int width = 0;
        std::string scale;
        if (packed)
            for (const auto& entry : packed->entries()) {
                auto bytes = packed->read(entry);
                if (entry.path == "materials/decals/decal_posters001a.vtf")
                    if (auto info = source_engine::vtf::inspect(bytes)) width = info->width;
                if (entry.path == "materials/decals/decal_posters001a.vmt") {
                    auto tokens = source_engine::keyvalue_tokens(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
                    for (std::size_t i = 0; i + 1 < tokens.size(); ++i)
                        if (tokens[i] == "$decalscale") scale = tokens[i + 1];
                }
            }
        std::printf("     the poster went in %d wide with $decalscale %s\n", width, scale.c_str());
        expect(width > 512 && !scale.empty() && std::atof(scale.c_str()) * width == 0.25 * 512,
               "a sharper decal covers the same part of the wall");
    }

    auto sharing = pack::pack::open(scratch / "sharing-pack");
    if (auto adopted_here = sharing.adopt_picture(picture, error)) {
        sharing.put(poster, *adopted_here);
        sharing.put("materials/decals/decalposter002a.vmt", *adopted_here);
        sharing.put("materials/decals/decalposter002b.vmt", *adopted_here).placement.quarter_turns = 1;
    }
    auto refused = game.build(sharing, scratch / "sharing-staging");
    std::printf("     %s\n", refused.message.c_str());
    expect(!refused.ok && refused.message.find("props/metalsign001b.vtf") != std::string::npos,
           "two signs on one texture are refused by name");

    auto start = std::chrono::steady_clock::now();
    while (!game.levels_ready()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    auto levels = game.levels_using(poster);
    std::printf("     map index ready after %.1fs more; %s appears in %zu maps:", seconds, poster.c_str(), levels.size());
    for (const auto& level : levels) std::printf(" %s(%s)", level.name.c_str(), level.edition.c_str());
    std::printf("\n");
    expect(!levels.empty(), "found maps that use the poster");

    for (auto& other : games) {
        if (other->slug() != "portal" && other->slug() != "portal-2") continue;
        auto& portal = *other;
        std::printf("     %s at %s, %zu surfaces\n", portal.name().c_str(), portal.location().c_str(), portal.surfaces().size());
        int readable = 0;
        for (const auto& surface : portal.surfaces())
            if (portal.original(surface.id, 64)) ++readable;
        std::printf("     %d of %zu materials have a picture citadel can read\n", readable, portal.surfaces().size());
        expect(readable * 10 >= int(portal.surfaces().size()) * 8, portal.name() + ": most materials read");

        for (const auto& shelf : portal.shelves()) {
            int count = 0;
            std::string sample;
            for (const auto& surface : portal.surfaces()) {
                auto contains = [&](const std::string& word) { return surface.name.find(word) != std::string::npos; };
                if (std::any_of(shelf.words.begin(), shelf.words.end(), contains) &&
                    std::none_of(shelf.except.begin(), shelf.except.end(), contains)) {
                    ++count;
                    if (sample.empty()) sample = surface.name;
                }
            }
            std::printf("     shelf %s: %d, such as %s\n", shelf.label.c_str(), count, sample.c_str());
        }

        while (!portal.levels_ready()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::string sign;
        std::size_t most = 0;
        for (const auto& surface : portal.surfaces()) {
            if (surface.name.find("sign") == std::string::npos || !portal.original(surface.id, 64)) continue;
            auto used = portal.levels_using(surface.id).size();
            if (used > most) { most = used; sign = surface.id; }
        }
        std::printf("     most-used sign: %s, in %zu maps\n", sign.c_str(), most);
        expect(!sign.empty() && most > 0, portal.name() + ": found a sign that maps use");

        auto portal_pack = pack::pack::open(scratch / (portal.slug() + "-pack"));
        if (auto adopted_here = portal_pack.adopt_picture(picture, error)) portal_pack.put(sign, *adopted_here);
        auto portal_built = portal.build(portal_pack, scratch / (portal.slug() + "-staging"));
        std::printf("     %s\n", portal_built.message.c_str());
        expect(portal_built.ok, portal.name() + ": built a pack");
        std::printf("     it would be installed as %s\n", portal.installed_pack().c_str());
    }

    for (auto& other : games) {
        if (other->slug() != "quake-3-arena") continue;
        auto& quake = *other;
        std::printf("     %s at %s, %zu surfaces\n", quake.name().c_str(), quake.location().c_str(), quake.surfaces().size());
        int readable = 0;
        for (const auto& surface : quake.surfaces())
            if (quake.original(surface.id, 64)) ++readable;
        std::printf("     %d of %zu pictures citadel can read\n", readable, quake.surfaces().size());
        expect(readable * 10 >= int(quake.surfaces().size()) * 9, "almost every Quake III picture reads");

        std::string tga, jpg;
        for (const auto& surface : quake.surfaces()) {
            if (tga.empty() && surface.id.ends_with(".tga") && surface.name.find("logo") != std::string::npos) tga = surface.id;
            if (jpg.empty() && surface.id.ends_with(".jpg") && surface.id.starts_with("textures/")) jpg = surface.id;
        }
        std::printf("     trying %s and %s\n", tga.c_str(), jpg.c_str());
        auto quake_pack = pack::pack::open(scratch / "quake-pack");
        auto adopted_again = quake_pack.adopt_picture(picture, error);
        if (adopted_again) {
            quake_pack.put(tga, *adopted_again);
            quake_pack.put(jpg, *adopted_again);
        }
        auto quake_built = quake.build(quake_pack, scratch / "quake-staging");
        std::printf("     %s\n", quake_built.message.c_str());
        expect(quake_built.ok, "built a Quake III pack");

        auto written = id_tech_3::zip::archive::open(scratch / "quake-staging/zz-citadel.pk3", error);
        expect(written && written->entries().size() == 2, "the pk3 holds both pictures");
        if (written) {
            for (const auto& entry : written->entries()) {
                auto bytes = written->read(entry);
                auto back = bytes ? image::decode(*bytes, error) : std::nullopt;
                auto original = quake.original(entry.path, 0);
                expect(back && original && back->width == original->width && back->height == original->height,
                       entry.path + " reads back at the original's size");
                if (back) {
                    auto file = scratch / ("quake-" + std::filesystem::path(entry.path).stem().string() + ".png");
                    stbi_write_png(file.c_str(), back->width, back->height, 4, back->pixels.data(), back->width * 4);
                }
            }
        }
        while (!quake.levels_ready()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto quake_levels = quake.levels_using(jpg);
        std::printf("     %s appears in %zu maps:", jpg.c_str(), quake_levels.size());
        for (const auto& level : quake_levels) std::printf(" %s", level.name.c_str());
        std::printf("\n");
        expect(!quake_levels.empty(), "found Quake III maps that use it");
    }

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
