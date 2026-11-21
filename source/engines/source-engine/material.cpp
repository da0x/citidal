#include "engines/source-engine/material.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace source_engine {

namespace {

struct token {
    std::string text;
    std::size_t start = 0;
    std::size_t end = 0;
};

std::vector<token> placed_tokens(std::string_view text) {
    std::vector<token> tokens;
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
            tokens.push_back({std::string(text.substr(at + 1, close - at - 1)), at, std::min(close + 1, text.size())});
            at = close + 1;
        } else if (c == '{' || c == '}') {
            tokens.push_back({std::string(1, c), at, at + 1});
            ++at;
        } else {
            auto start = at;
            while (at < text.size() && !std::isspace(static_cast<unsigned char>(text[at])) &&
                   text[at] != '"' && text[at] != '{' && text[at] != '}')
                ++at;
            tokens.push_back({std::string(text.substr(start, at - start)), start, at});
        }
    }
    return tokens;
}

}

std::vector<std::string> keyvalue_tokens(std::string_view text) {
    std::vector<std::string> tokens;
    for (auto& item : placed_tokens(text)) tokens.push_back(std::move(item.text));
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

namespace source_engine {

namespace {

// A key in a material file, with the token after it: its value, or the "{" its block
// opens with. A block's keys follow at one more depth.
struct key {
    std::size_t at = 0;
    int depth = 0;
    bool block = false;
};

std::vector<key> keys_of(const std::vector<token>& tokens) {
    std::vector<key> keys;
    int depth = 0;
    for (std::size_t i = 0; i < tokens.size();) {
        if (tokens[i].text == "}") {
            --depth;
            ++i;
            continue;
        }
        if (i + 1 >= tokens.size()) break;
        const bool block = tokens[i + 1].text == "{";
        keys.push_back({i, depth, block});
        if (block) ++depth;
        i += 2;
    }
    return keys;
}

// Whether the material, or a material it includes through a patch, sets the key, and
// the value it ends up with.
struct found_value {
    std::string value;
    bool included = false;
};

std::optional<found_value> value_of(const content& files, const std::string& material, const std::string& name) {
    std::string current = material;
    for (int depth = 0; depth < 4 && !current.empty(); ++depth) {
        auto bytes = files.read(current);
        if (!bytes) return std::nullopt;
        std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        auto tokens = placed_tokens(text);
        std::string include;
        for (const auto& item : keys_of(tokens)) {
            const auto word = normalized(tokens[item.at].text);
            if (!item.block && word == name) return found_value{tokens[item.at + 1].text, depth > 0};
            if (!item.block && word == "include") include = normalized(tokens[item.at + 1].text);
        }
        if (tokens.empty() || normalized(tokens[0].text) != "patch") return std::nullopt;
        current = include;
    }
    return std::nullopt;
}

// Sets one key in the material's text: blanks it wherever the text sets it, then sets it
// in the top block, or for a patch in its replace block when the included material has
// the key and its insert block when it doesn't.
bool set_value(std::string& text, const std::string& name, const std::string& value, bool included_has_it) {
    auto tokens = placed_tokens(text);
    auto keys = keys_of(tokens);
    if (keys.empty() || !keys.front().block) return false;
    const bool patch = normalized(tokens[0].text) == "patch";

    const std::string line = "\"" + name + "\" \"" + value + "\"";
    std::size_t at = tokens[keys.front().at + 1].end;
    std::string insert = "\n\t" + line;
    if (patch) {
        const std::string wanted = included_has_it ? "replace" : "insert";
        insert = "\n\t\"" + wanted + "\"\n\t{\n\t\t" + line + "\n\t}";
        for (const auto& item : keys)
            if (item.block && item.depth == 1 && normalized(tokens[item.at].text) == wanted) {
                at = tokens[item.at + 1].end;
                insert = "\n\t\t" + line;
                break;
            }
    }
    for (const auto& item : keys)
        if (!item.block && normalized(tokens[item.at].text) == name) {
            const auto start = tokens[item.at].start, end = tokens[item.at + 1].end;
            text.replace(start, end - start, std::string(end - start, ' '));
        }
    text.insert(at, insert);
    return true;
}

}

std::optional<std::string> changed_material(const content& files, const std::string& material, const material_change& change) {
    auto bytes = files.read(material);
    if (!bytes) return std::nullopt;
    std::string text(bytes->begin(), bytes->end());

    if (change.smaller > 1) {
        auto scale = value_of(files, material, "$decalscale");
        double now = scale ? std::atof(scale->value.c_str()) : 1;
        if (now <= 0) now = 1;
        char value[32];
        std::snprintf(value, sizeof value, "%g", now / change.smaller);
        if (!set_value(text, "$decalscale", value, scale && scale->included)) return std::nullopt;
    }
    if (!change.base_texture.empty()) {
        auto texture = value_of(files, material, "$basetexture");
        if (!set_value(text, "$basetexture", change.base_texture, texture && texture->included)) return std::nullopt;
    }
    return text;
}

}
