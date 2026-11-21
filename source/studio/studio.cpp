#include "studio/studio.hpp"

#include "platform/install.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>

namespace studio {

namespace {

constexpr float row_height = 60;
constexpr int thumbnail_size = 64;
constexpr int thumbnails_per_frame = 8;
constexpr int preview_size = 1024;
constexpr int photo_preview_size = 2048;

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

std::string shortened(const std::filesystem::path& path) {
    std::string text = path.string();
    const char* home = std::getenv("HOME");
    if (home && *home && text.starts_with(home)) text = "~" + text.substr(std::string(home).size());
    return text;
}

image::picture within(const image::picture& source, int largest) {
    if (source.width <= largest && source.height <= largest) return source;
    double scale = double(largest) / std::max(source.width, source.height);
    return image::resize(source, std::max(1, int(source.width * scale)), std::max(1, int(source.height * scale)));
}

std::pair<float, float> fitted(int width, int height, float most_width, float most_height) {
    if (width <= 0 || height <= 0) return {most_width, most_height};
    float scale = std::min(most_width / float(width), most_height / float(height));
    return {float(width) * scale, float(height) * scale};
}

}

studio::studio(ui::window& owner) : window(owner) {
    games = engine::discover(problems);
    if (games.empty()) return;
    std::size_t first = 0;
    std::ifstream remembered(platform::cache_folder() / "last-game");
    std::string slug;
    std::getline(remembered, slug);
    for (std::size_t i = 0; i < games.size(); ++i)
        if (games[i]->slug() == slug) first = i;
    open_game(first);
}

studio::~studio() {
    if (build_job.valid()) build_job.wait();
    clear_selection();
    for (auto& [surface, texture] : thumbnails) ui::release(texture);
}

void studio::open_game(std::size_t number) {
    clear_selection();
    for (auto& [surface, texture] : thumbnails) ui::release(texture);
    thumbnails.clear();
    without_thumbnail.clear();

    game = games[number].get();
    std::error_code made;
    std::filesystem::create_directories(platform::cache_folder(), made);
    std::ofstream(platform::cache_folder() / "last-game") << game->slug() << '\n';
    query.clear();
    shelves = game->shelves();
    shelf = 0;
    current = pack::pack::open(platform::documents_folder() / "citadel" / game->slug());
    std::error_code code;
    const auto installed = game->installed_pack();
    const auto saved = current->folder() / "pack.json";
    changed_since_build = !(std::filesystem::exists(installed, code) && std::filesystem::exists(saved, code) &&
                            std::filesystem::last_write_time(installed, code) >= std::filesystem::last_write_time(saved, code));
    needs_filter = true;
}

void studio::refilter() {
    needs_filter = false;
    shown.clear();
    const auto words = lowercase(query);
    const auto& surfaces = game->surfaces();
    const int count = int(shelves.size());
    for (std::size_t i = 0; i < surfaces.size(); ++i) {
        const auto& name = surfaces[i].name;
        if (!words.empty() && name.find(words) == std::string::npos) continue;
        bool keep = true;
        if (shelf < count) {
            const auto& chosen_shelf = shelves[std::size_t(shelf)];
            auto contains = [&](const std::string& word) { return name.find(word) != std::string::npos; };
            keep = std::any_of(chosen_shelf.words.begin(), chosen_shelf.words.end(), contains) &&
                   std::none_of(chosen_shelf.except.begin(), chosen_shelf.except.end(), contains);
        } else if (shelf == count) {
            keep = current->find(surfaces[i].id) != nullptr;
        }
        if (keep) shown.push_back(i);
    }
}

void studio::clear_selection() {
    ui::release(chosen.original_texture);
    ui::release(chosen.result_texture);
    chosen = {};
}

void studio::select(const std::string& surface) {
    if (chosen.surface == surface) return;
    clear_selection();
    chosen.surface = surface;
    chosen.original = game->original(surface, 0);
    if (chosen.original) {
        chosen.preview_original = within(chosen.original->picture, preview_size);
        chosen.original_texture = ui::upload(chosen.preview_original);
    }
    if (const auto* item = current->find(surface)) {
        std::string error;
        if (auto photo = image::load(current->picture_file(item->picture), error)) {
            chosen.picture = item->picture;
            chosen.photo = within(*photo, photo_preview_size);
            chosen.needs_render = true;
        } else {
            status = "Can't read this surface's picture: " + error;
            status_bad = true;
        }
    }
}

void studio::use_picture(const std::string& file) {
    if (chosen.surface.empty() || !chosen.original) {
        status = "Choose a poster, sign or screen first, then drop the picture on it";
        status_bad = true;
        return;
    }
    std::string error;
    auto photo = image::load(file, error);
    if (!photo) {
        status = "Can't read " + std::filesystem::path(file).filename().string() + ": " + error;
        status_bad = true;
        return;
    }
    auto adopted = current->adopt_picture(file, error);
    if (!adopted) {
        status = error;
        status_bad = true;
        return;
    }
    current->put(chosen.surface, *adopted);
    chosen.picture = *adopted;
    chosen.photo = within(*photo, photo_preview_size);
    chosen.needs_render = true;
    status = "Put " + std::filesystem::path(file).filename().string() + " on " + chosen.surface;
    status_bad = false;
    changed();
}

void studio::render_selection() {
    chosen.needs_render = false;
    ui::release(chosen.result_texture);
    const auto* item = current->find(chosen.surface);
    if (!item || chosen.photo.empty() || chosen.preview_original.empty()) return;
    chosen.result_texture = ui::upload(pack::render(*item, chosen.photo, chosen.preview_original));
}

void studio::changed() {
    needs_save = true;
    changed_since_build = true;
    if (shelf == int(shelves.size())) needs_filter = true;
}

void studio::start_build(const std::string& edition, const std::string& level) {
    if (build_job.valid()) return;
    if (!changed_since_build && !edition.empty() && std::filesystem::exists(game->installed_pack())) {
        auto launched = game->launch(edition, level);
        status = launched.message;
        status_bad = !launched.ok;
        return;
    }
    if (current->replacements().empty()) {
        status = "Put a picture on a poster, sign or screen first";
        status_bad = true;
        return;
    }
    std::string error;
    if (needs_save && current->save(error)) needs_save = false;

    const engine::game* target = game;
    const auto staging = platform::cache_folder() / "build" / game->slug();
    build_job = std::async(std::launch::async, [target, snapshot = *current, staging] {
        auto built = target->build(snapshot, staging);
        if (!built.ok) return built;
        auto installed = target->install(staging);
        if (!installed.ok) return installed;
        return engine::outcome{true, built.message + ". " + installed.message};
    });
    launch_edition = edition;
    launch_level = level;
    launch_after_build = !edition.empty();
    changed_since_build = false;
    status = "Building the pack";
    status_bad = false;
}

void studio::finish_build() {
    if (!build_job.valid()) return;
    if (build_job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        window.keep_drawing();
        return;
    }
    auto result = build_job.get();
    status = result.message;
    status_bad = !result.ok;
    if (!result.ok) {
        changed_since_build = true;
        return;
    }
    if (launch_after_build) {
        auto launched = game->launch(launch_edition, launch_level);
        status += ". " + launched.message;
        status_bad = !launched.ok;
    }
}

const ui::texture* studio::thumbnail(const engine::surface& surface) {
    if (auto found = thumbnails.find(surface.id); found != thumbnails.end()) return &found->second;
    if (without_thumbnail.contains(surface.id)) return nullptr;
    if (thumbnails_this_frame >= thumbnails_per_frame) {
        window.keep_drawing();
        return nullptr;
    }
    ++thumbnails_this_frame;
    auto art = game->original(surface.id, thumbnail_size);
    if (!art) {
        without_thumbnail.insert(surface.id);
        return nullptr;
    }
    return &thumbnails.emplace(surface.id, ui::upload(art->picture)).first->second;
}

std::string studio::edition_name(const std::string& id) const {
    for (const auto& edition : game->editions())
        if (edition.id == id) return edition.name;
    return id;
}

void studio::draw() {
    thumbnails_this_frame = 0;
    for (const auto& file : window.take_pictures()) use_picture(file);
    finish_build();
    if (chosen.needs_render) render_selection();
    if (needs_save && !ui::pointer_down()) {
        std::string error;
        if (current->save(error)) needs_save = false;
        else { status = error; status_bad = true; needs_save = false; }
    }

    if (!ui::typing()) {
        if (ui::pressed(ui::key::quit)) window.request_close();
        if (ui::pressed(ui::key::back)) {
            if (!query.empty()) { query.clear(); needs_filter = true; }
            else clear_selection();
        }
    }

    ui::begin_screen();
    if (!game) {
        ui::heading("No game found");
        ui::space(8);
        ui::wrapped("citadel looks for Half-Life 2 and Quake III Arena in every Steam library on this machine. "
                    "Install one from Steam, then open citadel again.");
        for (const auto& problem : problems) ui::bad(problem);
        ui::end_screen();
        return;
    }
    if (needs_filter) refilter();

    draw_top();
    if (needs_filter) refilter();
    ui::space(4);
    const float height = ui::available_height() - ui::line_height();
    const float list_width = std::max(320.0f, ui::available_width() * 0.3f);
    draw_list(list_width, height);
    ui::same_line();
    draw_detail(ui::available_width(), height);

    if (status_bad) ui::bad(status);
    else ui::quiet(status.empty() ? std::to_string(current->replacements().size()) + " pictures in this pack, kept in " +
                                         shortened(current->folder())
                                  : status);
    ui::end_screen();
}

void studio::draw_top() {
    ui::heading("citadel");
    for (std::size_t i = 0; i < games.size(); ++i) {
        ui::same_line(i == 0 ? 20 : -1);
        if (ui::tab(games[i]->name(), games[i].get() == game) && games[i].get() != game && !build_job.valid()) {
            open_game(i);
            return;
        }
    }

    std::vector<std::string> labels{changed_since_build ? "Build pack" : "Pack is built"};
    for (const auto& edition : game->editions()) labels.push_back("Play " + edition.name);
    const bool installed = std::filesystem::exists(game->installed_pack());
    if (installed) labels.push_back("Take pack out");
    float width = 0;
    for (const auto& label : labels) width += ui::button_width(label);

    ui::same_line();
    ui::push_right(width);
    const bool busy = build_job.valid();
    bool disabled = ui::disabled_begin(busy);
    if (ui::primary_button(labels[0])) start_build("", "");
    for (const auto& edition : game->editions()) {
        ui::same_line();
        if (ui::button("Play " + edition.name)) start_build(edition.id, "");
    }
    if (installed) {
        ui::same_line();
        if (ui::button("Take pack out")) {
            auto removed = game->uninstall();
            status = removed.message;
            status_bad = !removed.ok;
            changed_since_build = true;
        }
    }
    ui::disabled_end(disabled);
}

void studio::draw_list(float width, float height) {
    ui::begin_area("list", width, height, true);
    std::vector<std::string> labels;
    for (const auto& item : shelves) labels.push_back(item.label);
    labels.push_back("Yours");
    labels.push_back("All");
    float used = 0;
    const float room = ui::available_width();
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const float needed = ui::button_width(labels[i]);
        if (i > 0 && used + needed <= room) ui::same_line();
        else if (i > 0) used = 0;
        used += needed;
        if (ui::tab(labels[i], shelf == int(i))) {
            shelf = int(i);
            needs_filter = true;
        }
    }
    if (ui::search("Search " + std::to_string(game->surfaces().size()) + " surfaces", query, ui::available_width()))
        needs_filter = true;
    ui::quiet(std::to_string(shown.size()) + (shown.size() == 1 ? " surface" : " surfaces"));

    ui::begin_area("rows", 0, 0, false);
    const auto& surfaces = game->surfaces();
    ui::each_visible(int(shown.size()), row_height, [&](int row) {
        const auto& surface = surfaces[shown[std::size_t(row)]];
        const bool yours = current->find(surface.id) != nullptr;
        if (ui::list_row(surface.id, thumbnail(surface), surface.name,
                         surface.edition + (yours ? "   your picture" : ""), chosen.surface == surface.id, row_height))
            select(surface.id);
    });
    ui::end_area();
    ui::end_area();
}

void studio::draw_detail(float width, float height) {
    ui::begin_area("detail", width, height, true);
    if (chosen.surface.empty()) {
        ui::heading("Choose a surface");
        ui::space(6);
        ui::quiet_wrapped("Pick a poster, sign, billboard or screen on the left, then drop a picture or photo on it. "
                          "Build the pack and press Play to see it in the game.");
        ui::end_area();
        return;
    }
    const engine::surface* surface = nullptr;
    for (const auto& item : game->surfaces())
        if (item.id == chosen.surface) surface = &item;

    ui::heading(surface ? surface->name : chosen.surface);
    if (!chosen.original) {
        ui::bad("citadel can't read this surface's picture. It may use a texture format it doesn't know yet, "
                "or have no base texture at all.");
        ui::end_area();
        return;
    }
    ui::quiet((surface ? surface->edition + "   " : std::string()) + chosen.original->detail);
    ui::space(4);

    const float gap = 16;
    const float pane = (ui::available_width() - gap) / 2;
    const float most_height = std::max(160.0f, ui::available_height() * 0.5f);
    auto [show_width, show_height] = fitted(chosen.original->width, chosen.original->height, pane, most_height);

    ui::begin_area("in-game", pane, show_height + ui::line_height(), false);
    ui::quiet("In the game");
    ui::picture(chosen.original_texture, show_width, show_height);
    ui::end_area();
    ui::same_line(gap);
    ui::begin_area("yours", pane, show_height + ui::line_height(), false);
    auto* item = current->find(chosen.surface);
    if (item && chosen.result_texture) {
        ui::quiet("With your picture. Drag to move it, scroll to zoom");
        float moved_x = 0, moved_y = 0, wheel = 0;
        if (ui::picture_drag("result", chosen.result_texture, show_width, show_height, moved_x, moved_y, wheel)) {
            const int width_in_texture = chosen.preview_original.width, height_in_texture = chosen.preview_original.height;
            auto placed = image::placed_size(chosen.photo.width, chosen.photo.height, width_in_texture, height_in_texture, item->placement);
            const double free_x = width_in_texture - placed.width, free_y = height_in_texture - placed.height;
            if (std::abs(free_x) > 0.5)
                item->placement.pan_x = std::clamp(float(item->placement.pan_x + moved_x * (width_in_texture / show_width) / (0.5 * free_x)), -1.0f, 1.0f);
            if (std::abs(free_y) > 0.5)
                item->placement.pan_y = std::clamp(float(item->placement.pan_y + moved_y * (height_in_texture / show_height) / (0.5 * free_y)), -1.0f, 1.0f);
            if (wheel != 0) item->placement.zoom = std::clamp(item->placement.zoom * std::pow(1.1f, wheel), 0.2f, 5.0f);
            chosen.needs_render = true;
            changed();
        }
    } else {
        ui::quiet("Your picture");
        if (ui::drop_zone("drop", "Drop a picture or photo here, or click to choose one", show_width, show_height))
            window.choose_picture();
    }
    ui::end_area();

    ui::space(4);
    if (item) draw_controls(*item);
    ui::space(8);
    draw_levels();
    ui::end_area();
}

void studio::draw_controls(pack::replacement& item) {
    auto& place = item.placement;
    bool edited = false;
    if (ui::tab("Fill", place.mode == image::fit::fill)) { place.mode = image::fit::fill; edited = true; }
    ui::same_line();
    if (ui::tab("Fit", place.mode == image::fit::fit)) { place.mode = image::fit::fit; edited = true; }
    ui::same_line();
    if (ui::tab("Stretch", place.mode == image::fit::stretch)) { place.mode = image::fit::stretch; edited = true; }
    ui::same_line(24);
    if (ui::button("Turn left")) { place.quarter_turns = (place.quarter_turns + 3) % 4; edited = true; }
    ui::same_line();
    if (ui::button("Turn right")) { place.quarter_turns = (place.quarter_turns + 1) % 4; edited = true; }
    ui::same_line();
    if (ui::button("Center")) { place.pan_x = place.pan_y = 0; place.zoom = 1; edited = true; }

    const float third = (ui::available_width() - 40) / 3;
    ui::begin_area("zoom", third, ui::line_height(), false);
    edited |= ui::slider("##zoom", place.zoom, 0.2f, 5.0f, "zoom %.2f×");
    ui::end_area();
    ui::same_line();
    ui::begin_area("across", third, ui::line_height(), false);
    edited |= ui::slider("##across", place.pan_x, -1.0f, 1.0f, "left to right %.2f");
    ui::end_area();
    ui::same_line();
    ui::begin_area("down", third, ui::line_height(), false);
    edited |= ui::slider("##down", place.pan_y, -1.0f, 1.0f, "top to bottom %.2f");
    ui::end_area();

    if (chosen.original && chosen.original->transparent) {
        ui::quiet("Edges");
        ui::same_line();
        if (ui::tab("Clean", item.edges == image::edges::clean)) { item.edges = image::edges::clean; edited = true; }
        ui::same_line();
        if (ui::tab("Torn edges", item.edges == image::edges::torn)) { item.edges = image::edges::torn; edited = true; }
        ui::same_line();
        if (ui::tab("Weathered", item.edges == image::edges::weathered)) { item.edges = image::edges::weathered; edited = true; }
        ui::same_line(24);
    }
    if (ui::button("Choose another picture")) window.choose_picture();
    ui::same_line();
    if (ui::button("Remove picture")) {
        current->remove(chosen.surface);
        chosen.needs_render = true;
        changed();
        return;
    }
    if (edited) {
        chosen.needs_render = true;
        changed();
    }
}

void studio::draw_levels() {
    ui::text("Where it appears");
    if (chosen.surface.starts_with("materials/console/")) {
        ui::quiet_wrapped("This is a main menu background. Build the pack and press Play to see it behind the menu. "
                          "The game picks the background from how far you are in the story.");
        return;
    }
    if (!game->levels_ready()) {
        ui::quiet("Looking through the maps");
        window.keep_drawing();
        return;
    }
    auto levels = game->levels_using(chosen.surface);
    if (levels.empty()) {
        ui::quiet_wrapped("No map places it directly. It may be painted on a model, or shown by the game itself.");
        return;
    }
    ui::begin_area("levels", 0, 0, false);
    for (const auto& level : levels) {
        ui::text(level.name);
        ui::same_line(12);
        ui::quiet(edition_name(level.edition));
        ui::same_line();
        ui::push_right(ui::button_width("Play here"));
        bool disabled = ui::disabled_begin(build_job.valid());
        if (ui::button("Play here##" + level.name)) start_build(level.edition, level.name);
        ui::disabled_end(disabled);
    }
    ui::end_area();
}

}
