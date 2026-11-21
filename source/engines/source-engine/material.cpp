#include "engines/source-engine/material.hpp"

#include <algorithm>
#include <cctype>

namespace source_engine {

std::vector<std::string> keyvalue_tokens(std::string_view text) {
    std::vector<std::string> tokens;
    std::size_t at = 0;
    while (at < text.size()) {
        const char c = text[at];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++at;
        } else if (c == '/' && at + 1 < text.size() && text[at + 1] == '/') {
            while (at < text.size() && text[at] != '\n') ++at;
        } else if (c == '"') {
            auto close = text.find('"', at + 1);
            if (close == std::string_view::npos) close = text.size();
            tokens.emplace_back(text.substr(at + 1, close - at - 1));
            at = close + 1;
        } else if (c == '{' || c == '}') {
            tokens.emplace_back(1, c);
            ++at;
        } else {
            auto start = at;
            while (at < text.size() && !std::isspace(static_cast<unsigned char>(text[at])) &&
                   text[at] != '"' && text[at] != '{' && text[at] != '}')
                ++at;
            tokens.emplace_back(text.substr(start, at - start));
        }
    }
    return tokens;
}

std::string normalized(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return c == '\\' ? '/' : std::tolower(c); });
    while (value.starts_with("/")) value.erase(0, 1);
    return value;
}

std::string base_texture(const content& files, const std::string& material) {
    std::string current = material;
    for (int depth = 0; depth < 4; ++depth) {
        auto bytes = files.read(current);
        if (!bytes) return {};
        auto tokens = keyvalue_tokens(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
        for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
            if (normalized(tokens[i]) != "$basetexture") continue;
            std::string texture = normalized(tokens[i + 1]);
            if (texture.ends_with(".vtf")) texture.resize(texture.size() - 4);
            if (!texture.starts_with("materials/")) texture = "materials/" + texture;
            return texture + ".vtf";
        }
        if (tokens.empty() || normalized(tokens[0]) != "patch") return {};
        std::string include;
        for (std::size_t i = 0; i + 1 < tokens.size(); ++i)
            if (normalized(tokens[i]) == "include") include = normalized(tokens[i + 1]);
        if (include.empty()) return {};
        current = include;
    }
    return {};
}

}
