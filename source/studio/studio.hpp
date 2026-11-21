#pragma once

#include "engine/game.hpp"
#include "pack/pack.hpp"
#include "ui/ui.hpp"

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace studio {

class studio {
public:
    explicit studio(ui::window& window);
    ~studio();

    void draw();

private:
    struct selection {
        std::string surface;
        std::optional<engine::art> original;
        image::picture preview_original;
        ui::texture original_texture;
        std::string picture;
        image::picture photo;
        int photo_width = 0;
        int photo_height = 0;
        engine::resolution goes_in;
        bool goes_in_knew_maps = false;
        ui::texture result_texture;
        bool needs_render = false;
    };

    void open_game(std::size_t number);
    void refilter();
    void select(const std::string& surface);
    void clear_selection();
    void use_picture(const std::string& file);
    void render_selection();
    void changed();
    void start_build(const std::string& edition, const std::string& level);
    void finish_build();
    const ui::texture* thumbnail(const engine::surface& surface);
    std::string edition_name(const std::string& id) const;

    void draw_top();
    void draw_list(float width, float height);
    void draw_detail(float width, float height);
    void draw_controls(pack::replacement& item);
    void draw_levels();

    ui::window& window;
    std::vector<std::unique_ptr<engine::game>> games;
    std::vector<std::string> problems;
    engine::game* game = nullptr;
    std::optional<pack::pack> current;
    std::vector<engine::shelf> shelves;

    int shelf = 0;
    std::string query;
    std::vector<std::size_t> shown;
    bool needs_filter = true;

    selection chosen;
    std::unordered_map<std::string, ui::texture> thumbnails;
    std::unordered_set<std::string> without_thumbnail;
    int thumbnails_this_frame = 0;

    bool needs_save = false;
    bool changed_since_build = true;
    std::future<engine::outcome> build_job;
    std::string launch_edition;
    std::string launch_level;
    bool launch_after_build = false;
    std::string status;
    bool status_bad = false;
};

}
