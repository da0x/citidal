#include "engines/id-tech-3/content.hpp"

#include <algorithm>
#include <cctype>

namespace id_tech_3 {

namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return c == '\\' ? '/' : std::tolower(c); });
    return value;
}

std::string without_extension(std::string path) {
    auto dot = path.rfind('.');
    auto slash = path.rfind('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) path.resize(dot);
    return path;
}

struct token {
    std::string text;
    int line = 0;
};

std::vector<token> shader_tokens(std::string_view text) {
    std::vector<token> tokens;
    int line = 1;
    std::size_t at = 0;
    while (at < text.size()) {
        const char c = text[at];
        if (c == '\n') { ++line; ++at; continue; }
        if (std::isspace(static_cast<unsigned char>(c))) { ++at; continue; }
        if (c == '/' && at + 1 < text.size() && text[at + 1] == '/') {
            while (at < text.size() && text[at] != '\n') ++at;
            continue;
        }
        if (c == '/' && at + 1 < text.size() && text[at + 1] == '*') {
            at += 2;
            while (at + 1 < text.size() && !(text[at] == '*' && text[at + 1] == '/')) {
                if (text[at] == '\n') ++line;
                ++at;
            }
            at += 2;
            continue;
        }
        if (c == '{' || c == '}') {
            tokens.push_back({std::string(1, c), line});
            ++at;
            continue;
        }
        if (c == '"') {
            auto close = text.find('"', at + 1);
            if (close == std::string_view::npos) close = text.size();
            tokens.push_back({std::string(text.substr(at + 1, close - at - 1)), line});
            at = close + 1;
            continue;
        }
        auto start = at;
        while (at < text.size() && !std::isspace(static_cast<unsigned char>(text[at])) && text[at] != '{' && text[at] != '}') ++at;
        tokens.push_back({std::string(text.substr(start, at - start)), line});
    }
    return tokens;
}

}

std::optional<content> content::mount(const std::filesystem::path& folder, const std::string& skip, std::string& error) {
    std::vector<std::filesystem::path> packs;
    std::error_code code;
    for (const auto& item : std::filesystem::directory_iterator(folder, code)) {
        auto name = lowercase(item.path().filename().string());
        if (item.is_regular_file() && name.ends_with(".pk3") && name != skip) packs.push_back(item.path());
    }
    if (packs.empty()) {
        error = "No pk3 files in " + folder.string();
        return std::nullopt;
    }
    std::sort(packs.begin(), packs.end(), [](const auto& a, const auto& b) {
        return lowercase(a.filename().string()) < lowercase(b.filename().string());
    });

    content result;
    for (const auto& pack : packs) {
        auto opened = zip::archive::open(pack, error);
        if (!opened) return std::nullopt;
        const auto number = std::uint32_t(result.archives.size());
        const auto& entries = opened->entries();
        for (std::uint32_t i = 0; i < entries.size(); ++i) result.index[entries[i].path] = {number, i};
        result.archives.push_back(std::move(*opened));
    }
    return result;
}

std::optional<std::vector<std::uint8_t>> content::read(const std::string& path) const {
    auto found = index.find(path);
    if (found == index.end()) return std::nullopt;
    const auto& from = archives[found->second.archive];
    return from.read(from.entries()[found->second.index]);
}

std::string content::owner(const std::string& path) const {
    auto found = index.find(path);
    if (found == index.end()) return {};
    return archives[found->second.archive].file().stem().string();
}

std::vector<std::string> content::files(std::string_view prefix, std::string_view suffix) const {
    std::vector<std::string> paths;
    for (const auto& [path, where] : index)
        if (path.starts_with(prefix) && path.ends_with(suffix)) paths.push_back(path);
    std::sort(paths.begin(), paths.end());
    return paths;
}

std::unordered_map<std::string, std::vector<std::string>> shader_images(const content& files) {
    std::unordered_map<std::string, std::vector<std::string>> shaders;
    for (const auto& script : files.files("scripts/", ".shader")) {
        auto bytes = files.read(script);
        if (!bytes) continue;
        auto tokens = shader_tokens(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
        std::size_t at = 0;
        while (at + 1 < tokens.size()) {
            if (tokens[at + 1].text != "{") { ++at; continue; }
            const std::string name = without_extension(lowercase(tokens[at].text));
            std::vector<std::string> images;
            int depth = 0;
            at += 1;
            for (; at < tokens.size(); ++at) {
                const auto& word = tokens[at].text;
                if (word == "{") { ++depth; continue; }
                if (word == "}") {
                    if (--depth == 0) { ++at; break; }
                    continue;
                }
                const auto key = lowercase(word);
                auto add = [&](const std::string& image) {
                    if (image.empty() || image[0] == '$' || image[0] == '*') return;
                    images.push_back(without_extension(lowercase(image)));
                };
                if ((key == "map" || key == "clampmap" || key == "qer_editorimage") && at + 1 < tokens.size()) {
                    add(tokens[at + 1].text);
                } else if (key == "animmap") {
                    const int line = tokens[at].line;
                    for (std::size_t next = at + 2; next < tokens.size() && tokens[next].line == line; ++next)
                        add(tokens[next].text);
                }
            }
            auto& known = shaders[name];
            for (auto& image : images)
                if (std::find(known.begin(), known.end(), image) == known.end()) known.push_back(image);
        }
    }
    return shaders;
}

}
