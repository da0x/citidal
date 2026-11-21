#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace image {

struct picture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    bool empty() const { return width <= 0 || height <= 0; }
    std::uint8_t* at(int x, int y) { return pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4; }
    const std::uint8_t* at(int x, int y) const { return pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4; }
};

enum class fit { fill, fit, stretch };

// How much of the original's see-through channel a new picture takes on.
enum class edges { clean, torn, weathered };

struct placement {
    fit mode = fit::fill;
    float zoom = 1.0f;
    float pan_x = 0.0f;
    float pan_y = 0.0f;
    int quarter_turns = 0;

    bool operator==(const placement&) const = default;
};

picture blank(int width, int height, std::array<std::uint8_t, 4> color);
std::optional<picture> load(const std::filesystem::path& file, std::string& error);
std::optional<picture> decode(std::span<const std::uint8_t> bytes, std::string& error);

picture resize(const picture& source, int width, int height);
picture rotate(const picture& source, int quarter_turns);
bool has_transparency(const picture& source);

struct size {
    double width = 0;
    double height = 0;
};

size placed_size(int source_width, int source_height, int width, int height, const placement& place);
picture compose(const picture& source, int width, int height, const placement& place);
void apply_edges(picture& target, const picture& original, edges kind);

}
