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

std::optional<std::string> with_decal_scale(const content& files, const std::string& material, int times) {
    auto bytes = files.read(material);
    if (!bytes) return std::nullopt;
    std::string text(bytes->begin(), bytes->end());
    const auto tokens = placed_tokens(text);
    if (tokens.size() < 2) return std::nullopt;
    const bool patch = normalized(tokens[0].text) == "patch";

    // The scale it has now, following a patch to the material it includes.
    double scale = 1;
    bool included_has_scale = false;
    std::string current = material;
    for (int depth = 0; depth < 4 && !current.empty(); ++depth) {
        auto read = depth == 0 ? bytes : files.read(current);
        if (!read) break;
        auto words = keyvalue_tokens(std::string_view(reinterpret_cast<const char*>(read->data()), read->size()));
        std::string include;
        bool found = false;
        for (std::size_t i = 0; i + 1 < words.size(); ++i) {
            if (normalized(words[i]) == "$decalscale") {
                scale = std::atof(words[i + 1].c_str());
                found = true;
                included_has_scale = depth > 0;
                break;
            }
            if (normalized(words[i]) == "include") include = normalized(words[i + 1]);
        }
        if (found || words.empty() || normalized(words[0]) != "patch") break;
        current = include;
    }
    if (scale <= 0) scale = 1;

    char value[32];
    std::snprintf(value, sizeof value, "%g", scale / times);
    const std::string line = std::string("\"$decalscale\" \"") + value + "\"";

    // Blank out the scale this file sets, then set the new one in the top block, or for a
    // patch in its insert block, or its replace block when the included material has one.
    std::vector<std::pair<std::size_t, std::size_t>> cuts;
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i)
        if (normalized(tokens[i].text) == "$decalscale") cuts.emplace_back(tokens[i].start, tokens[i + 1].end);

    std::size_t top = 0;
    while (top < tokens.size() && tokens[top].text != "{") ++top;
    if (top == tokens.size()) return std::nullopt;
    std::size_t at = tokens[top].end;
    std::string insert = "\n\t" + line;
    if (patch) {
        const std::string block = included_has_scale ? "replace" : "insert";
        std::size_t found = tokens.size();
        int depth = 0;
        for (std::size_t i = top + 1; i + 1 < tokens.size(); ++i) {
            if (tokens[i].text == "{") ++depth;
            if (tokens[i].text == "}") --depth;
            if (depth == 0 && normalized(tokens[i].text) == block && tokens[i + 1].text == "{") { found = i + 1; break; }
        }
        if (found < tokens.size()) {
            at = tokens[found].end;
            insert = "\n\t\t" + line;
        } else {
            insert = "\n\t\"" + block + "\"\n\t{\n\t\t" + line + "\n\t}";
        }
    }
    for (auto cut = cuts.rbegin(); cut != cuts.rend(); ++cut) {
        text.replace(cut->first, cut->second - cut->first, std::string(cut->second - cut->first, ' '));
    }
    text.insert(at, insert);
    return text;
}

}
