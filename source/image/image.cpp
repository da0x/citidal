#include "image/image.hpp"

#include <stb_image.h>
#include <deprecated/stb_image_resize2.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace image {

namespace {

picture resize_region(const picture& source, int x, int y, int width, int height, int out_width, int out_height) {
    picture result{out_width, out_height, std::vector<std::uint8_t>(static_cast<std::size_t>(out_width) * out_height * 4)};
    stbir_resize_uint8_srgb(source.at(x, y), width, height, source.width * 4,
                            result.pixels.data(), out_width, out_height, out_width * 4, STBIR_RGBA);
    return result;
}

}

picture blank(int width, int height, std::array<std::uint8_t, 4> color) {
    picture result{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
    for (std::size_t i = 0; i < result.pixels.size(); i += 4) std::memcpy(&result.pixels[i], color.data(), 4);
    return result;
}

std::optional<picture> load(const std::filesystem::path& file, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "Can't open " + file.filename().string();
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return decode(bytes, error);
}

std::optional<picture> decode(std::span<const std::uint8_t> bytes, std::string& error) {
    int width = 0, height = 0, channels = 0;
    unsigned char* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (!data) {
        error = stbi_failure_reason();
        return std::nullopt;
    }
    picture result{width, height, std::vector<std::uint8_t>(data, data + static_cast<std::size_t>(width) * height * 4)};
    stbi_image_free(data);
    return result;
}

picture resize(const picture& source, int width, int height) {
    return resize_region(source, 0, 0, source.width, source.height, width, height);
}

picture rotate(const picture& source, int quarter_turns) {
    int turns = ((quarter_turns % 4) + 4) % 4;
    if (turns == 0) return source;
    bool sideways = turns % 2 == 1;
    picture result{sideways ? source.height : source.width, sideways ? source.width : source.height, {}};
    result.pixels.resize(source.pixels.size());
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            int to_x = x, to_y = y;
            if (turns == 1) { to_x = source.height - 1 - y; to_y = x; }
            if (turns == 2) { to_x = source.width - 1 - x; to_y = source.height - 1 - y; }
            if (turns == 3) { to_x = y; to_y = source.width - 1 - x; }
            std::memcpy(result.at(to_x, to_y), source.at(x, y), 4);
        }
    }
    return result;
}

bool has_transparency(const picture& source) {
    for (std::size_t i = 3; i < source.pixels.size(); i += 4)
        if (source.pixels[i] < 250) return true;
    return false;
}

size placed_size(int source_width, int source_height, int width, int height, const placement& place) {
    double from_width = source_width, from_height = source_height;
    if (((place.quarter_turns % 4) + 4) % 2 == 1) std::swap(from_width, from_height);
    if (from_width <= 0 || from_height <= 0) return {};
    switch (place.mode) {
    case fit::fit: {
        double scale = std::min(width / from_width, height / from_height) * place.zoom;
        return {from_width * scale, from_height * scale};
    }
    case fit::stretch:
        return {width * double(place.zoom), height * double(place.zoom)};
    case fit::fill:
        break;
    }
    double scale = std::max(width / from_width, height / from_height) * place.zoom;
    return {from_width * scale, from_height * scale};
}

picture compose(const picture& unturned, int width, int height, const placement& place) {
    picture result = blank(width, height, {0, 0, 0, 255});
    if (unturned.empty() || width <= 0 || height <= 0) return result;
    picture source = rotate(unturned, place.quarter_turns);

    const auto placed = placed_size(unturned.width, unturned.height, width, height, place);
    const double placed_width = placed.width, placed_height = placed.height;
    const double scale_x = placed_width / source.width, scale_y = placed_height / source.height;
    const double left = (width - placed_width) * (0.5 + 0.5 * place.pan_x);
    const double top = (height - placed_height) * (0.5 + 0.5 * place.pan_y);

    const int x0 = static_cast<int>(std::lround(std::max(0.0, left)));
    const int y0 = static_cast<int>(std::lround(std::max(0.0, top)));
    const int x1 = static_cast<int>(std::lround(std::min<double>(width, left + placed_width)));
    const int y1 = static_cast<int>(std::lround(std::min<double>(height, top + placed_height)));
    if (x1 <= x0 || y1 <= y0) return result;

    const int from_x0 = std::clamp(static_cast<int>(std::floor((x0 - left) / scale_x)), 0, source.width - 1);
    const int from_y0 = std::clamp(static_cast<int>(std::floor((y0 - top) / scale_y)), 0, source.height - 1);
    const int from_x1 = std::clamp(static_cast<int>(std::ceil((x1 - left) / scale_x)), from_x0 + 1, source.width);
    const int from_y1 = std::clamp(static_cast<int>(std::ceil((y1 - top) / scale_y)), from_y0 + 1, source.height);

    picture region = resize_region(source, from_x0, from_y0, from_x1 - from_x0, from_y1 - from_y0, x1 - x0, y1 - y0);
    for (int y = 0; y < region.height; ++y)
        std::memcpy(result.at(x0, y0 + y), region.at(0, y), static_cast<std::size_t>(region.width) * 4);
    return result;
}

void apply_edges(picture& target, const picture& original, edges kind) {
    if (kind == edges::clean || original.empty()) return;
    picture resized;
    const picture* mask = &original;
    if (original.width != target.width || original.height != target.height) {
        resized = resize(original, target.width, target.height);
        mask = &resized;
    }
    const int width = target.width, height = target.height;
    auto alpha = [&](int x, int y) { return mask->at(x, y)[3]; };

    if (kind == edges::weathered) {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) target.at(x, y)[3] = std::min(target.at(x, y)[3], alpha(x, y));
        return;
    }

    // Torn: the see-through parts reached from the border, within a band along it, are
    // the rips. Everything else, the grime and holes inside, stays solid.
    const int band = std::max(2, std::min(width, height) / 5);
    std::vector<std::uint8_t> outside(std::size_t(width) * height, 0);
    std::vector<std::pair<int, int>> stack;
    auto reach = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        auto& seen = outside[std::size_t(y) * width + x];
        if (seen || alpha(x, y) >= 128) return;
        if (std::min({x, y, width - 1 - x, height - 1 - y}) > band) return;
        seen = 1;
        stack.emplace_back(x, y);
    };
    for (int x = 0; x < width; ++x) { reach(x, 0); reach(x, height - 1); }
    for (int y = 0; y < height; ++y) { reach(0, y); reach(width - 1, y); }
    while (!stack.empty()) {
        auto [x, y] = stack.back();
        stack.pop_back();
        reach(x + 1, y); reach(x - 1, y); reach(x, y + 1); reach(x, y - 1);
    }
    auto near_outside = [&](int x, int y) {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                int nx = x + dx, ny = y + dy;
                if (nx >= 0 && ny >= 0 && nx < width && ny < height && outside[std::size_t(ny) * width + nx]) return true;
            }
        return false;
    };
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (near_outside(x, y)) target.at(x, y)[3] = std::min(target.at(x, y)[3], alpha(x, y));
}

}
