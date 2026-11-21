#pragma once

#include "image/image.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pack {

struct replacement {
    std::string surface;
    std::string picture;
    image::placement placement;
    bool keep_shape = false;
};

class pack {
public:
    static pack open(const std::filesystem::path& folder);

    bool save(std::string& error) const;
    std::optional<std::string> adopt_picture(const std::filesystem::path& file, std::string& error);

    const replacement* find(const std::string& surface) const;
    replacement* find(const std::string& surface);
    replacement& put(const std::string& surface, const std::string& picture);
    void remove(const std::string& surface);

    std::filesystem::path picture_file(const std::string& picture) const;
    const std::filesystem::path& folder() const { return location; }
    const std::vector<replacement>& replacements() const { return items; }

private:
    std::filesystem::path location;
    std::vector<replacement> items;
};

image::picture render(const replacement& item, const image::picture& photo, const image::picture& original);

}
