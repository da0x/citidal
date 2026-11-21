#include "pack/pack.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace pack {

namespace {

const char* fit_name(image::fit mode) {
    switch (mode) {
    case image::fit::fit: return "fit";
    case image::fit::stretch: return "stretch";
    case image::fit::fill: break;
    }
    return "fill";
}

const char* edges_name(image::edges kind) {
    switch (kind) {
    case image::edges::torn: return "torn";
    case image::edges::weathered: return "weathered";
    case image::edges::clean: break;
    }
    return "clean";
}

image::edges edges_from(const std::string& name) {
    if (name == "torn") return image::edges::torn;
    if (name == "weathered") return image::edges::weathered;
    return image::edges::clean;
}

image::fit fit_from(const std::string& name) {
    if (name == "fit") return image::fit::fit;
    if (name == "stretch") return image::fit::stretch;
    return image::fit::fill;
}

std::string content_name(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    char text[17];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(hash));
    return text;
}

}

pack pack::open(const std::filesystem::path& folder) {
    pack result;
    result.location = folder;
    std::ifstream in(folder / "pack.json");
    if (!in) return result;
    auto document = nlohmann::json::parse(in, nullptr, false);
    if (document.is_discarded()) return result;
    for (const auto& entry : document.value("replacements", nlohmann::json::array())) {
        replacement item;
        item.surface = entry.value("surface", "");
        item.picture = entry.value("picture", "");
        item.placement.mode = fit_from(entry.value("fit", "fill"));
        item.placement.zoom = entry.value("zoom", 1.0f);
        item.placement.pan_x = entry.value("pan_x", 0.0f);
        item.placement.pan_y = entry.value("pan_y", 0.0f);
        item.placement.quarter_turns = entry.value("turns", 0);
        item.edges = edges_from(entry.value("edges", "clean"));
        if (!item.surface.empty() && !item.picture.empty()) result.items.push_back(item);
    }
    return result;
}

bool pack::save(std::string& error) const {
    nlohmann::json list = nlohmann::json::array();
    for (const auto& item : items) {
        list.push_back({
            {"surface", item.surface},
            {"picture", item.picture},
            {"fit", fit_name(item.placement.mode)},
            {"zoom", item.placement.zoom},
            {"pan_x", item.placement.pan_x},
            {"pan_y", item.placement.pan_y},
            {"turns", item.placement.quarter_turns},
            {"edges", edges_name(item.edges)},
        });
    }
    std::error_code code;
    std::filesystem::create_directories(location, code);
    auto temporary = location / "pack.json.new";
    {
        std::ofstream out(temporary);
        out << nlohmann::json{{"replacements", list}}.dump(2) << '\n';
        if (!out) {
            error = "Can't write " + temporary.string();
            return false;
        }
    }
    std::filesystem::rename(temporary, location / "pack.json", code);
    if (code) {
        error = code.message();
        return false;
    }
    return true;
}

std::optional<std::string> pack::adopt_picture(const std::filesystem::path& file, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "Can't open " + file.filename().string();
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string extension = file.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
    std::string name = content_name(bytes) + extension;

    std::error_code code;
    std::filesystem::create_directories(location / "pictures", code);
    auto destination = location / "pictures" / name;
    if (!std::filesystem::exists(destination)) {
        std::ofstream out(destination, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            error = "Can't copy the picture into " + location.string();
            return std::nullopt;
        }
    }
    return name;
}

const replacement* pack::find(const std::string& surface) const {
    for (const auto& item : items)
        if (item.surface == surface) return &item;
    return nullptr;
}

replacement* pack::find(const std::string& surface) {
    for (auto& item : items)
        if (item.surface == surface) return &item;
    return nullptr;
}

replacement& pack::put(const std::string& surface, const std::string& picture) {
    if (auto* existing = find(surface)) {
        existing->picture = picture;
        existing->placement = {};
        return *existing;
    }
    items.push_back({surface, picture, {}, image::edges::clean});
    return items.back();
}

void pack::remove(const std::string& surface) {
    std::erase_if(items, [&](const replacement& item) { return item.surface == surface; });
}

std::filesystem::path pack::picture_file(const std::string& picture) const {
    return location / "pictures" / picture;
}

image::picture render(const replacement& item, const image::picture& photo, const image::picture& original) {
    return render(item, photo, original, original.width, original.height);
}

image::picture render(const replacement& item, const image::picture& photo, const image::picture& original, int width, int height) {
    auto result = image::compose(photo, width, height, item.placement);
    image::apply_edges(result, original, item.edges);
    return result;
}

}
