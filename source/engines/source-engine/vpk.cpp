#include "engines/source-engine/vpk.hpp"

#include <zlib.h>

#include <algorithm>
#include <map>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace source_engine::vpk {

namespace {

constexpr std::uint32_t signature = 0x55aa1234;
constexpr std::uint16_t inside_directory = 0x7fff;

struct reader {
    const std::vector<std::uint8_t>& bytes;
    std::size_t at = 0;
    bool failed = false;

    template <typename T> T number() {
        T value{};
        if (at + sizeof(T) > bytes.size()) { failed = true; return value; }
        std::memcpy(&value, bytes.data() + at, sizeof(T));
        at += sizeof(T);
        return value;
    }

    std::string text() {
        auto end = std::find(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.end(), 0);
        if (end == bytes.end()) { failed = true; return {}; }
        std::string value(bytes.begin() + static_cast<std::ptrdiff_t>(at), end);
        at = static_cast<std::size_t>(end - bytes.begin()) + 1;
        return value;
    }
};

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

}

std::optional<archive> archive::open(const std::filesystem::path& directory_file, std::string& error) {
    std::ifstream in(directory_file, std::ios::binary);
    if (!in) {
        error = "Can't open " + directory_file.string();
        return std::nullopt;
    }
    std::uint32_t header[3] = {};
    in.read(reinterpret_cast<char*>(header), sizeof header);
    if (!in || header[0] != signature || (header[1] != 1 && header[1] != 2)) {
        error = directory_file.filename().string() + " is not a VPK directory";
        return std::nullopt;
    }
    const std::uint32_t header_size = header[1] == 1 ? 12 : 28;
    const std::uint32_t tree_size = header[2];

    std::vector<std::uint8_t> tree(tree_size);
    in.seekg(header_size);
    in.read(reinterpret_cast<char*>(tree.data()), tree_size);
    if (!in) {
        error = directory_file.filename().string() + " is cut short";
        return std::nullopt;
    }

    archive result;
    result.directory_file = directory_file;
    result.data_start = std::uint64_t(header_size) + tree_size;
    std::string name = directory_file.string();
    result.chunk_prefix = name.ends_with("_dir.vpk") ? name.substr(0, name.size() - 8) : name.substr(0, name.size() - 4);

    reader tree_reader{tree};
    while (!tree_reader.failed) {
        std::string extension = tree_reader.text();
        if (extension.empty()) break;
        while (!tree_reader.failed) {
            std::string folder = tree_reader.text();
            if (folder.empty()) break;
            while (!tree_reader.failed) {
                std::string file = tree_reader.text();
                if (file.empty()) break;
                entry item;
                tree_reader.number<std::uint32_t>();
                item.preload_length = tree_reader.number<std::uint16_t>();
                item.archive = tree_reader.number<std::uint16_t>();
                item.offset = tree_reader.number<std::uint32_t>();
                item.length = tree_reader.number<std::uint32_t>();
                tree_reader.number<std::uint16_t>();
                item.preload_offset = header_size + tree_reader.at;
                tree_reader.at += item.preload_length;
                std::string path = folder == " " ? std::string() : folder + "/";
                path += file;
                if (extension != " ") path += "." + extension;
                item.path = lowercase(path);
                result.items.push_back(std::move(item));
            }
        }
    }
    if (tree_reader.failed) {
        error = directory_file.filename().string() + " has a damaged file list";
        return std::nullopt;
    }
    return result;
}

std::vector<std::uint8_t> archive::read(const entry& item, std::uint64_t offset, std::uint64_t length) const {
    const std::uint64_t total = item.size();
    if (offset >= total) return {};
    length = std::min(length, total - offset);
    std::vector<std::uint8_t> bytes(length);
    std::size_t written = 0;

    if (offset < item.preload_length) {
        std::uint64_t count = std::min<std::uint64_t>(item.preload_length - offset, length);
        std::ifstream in(directory_file, std::ios::binary);
        in.seekg(static_cast<std::streamoff>(item.preload_offset + offset));
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(count));
        if (!in) return {};
        written += count;
        offset = 0;
    } else {
        offset -= item.preload_length;
    }

    if (written < length) {
        std::filesystem::path chunk;
        std::uint64_t start = item.offset + offset;
        if (item.archive == inside_directory) {
            chunk = directory_file;
            start += data_start;
        } else {
            char suffix[16];
            std::snprintf(suffix, sizeof suffix, "_%03u.vpk", unsigned(item.archive));
            chunk = chunk_prefix + suffix;
        }
        std::ifstream in(chunk, std::ios::binary);
        in.seekg(static_cast<std::streamoff>(start));
        in.read(reinterpret_cast<char*>(bytes.data() + written), static_cast<std::streamsize>(length - written));
        if (!in) return {};
    }
    return bytes;
}

bool write(const std::filesystem::path& file,
           const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files, std::string& error) {
    std::map<std::string, std::map<std::string, std::vector<std::pair<std::string, const std::vector<std::uint8_t>*>>>> tree;
    for (const auto& [path, bytes] : files) {
        auto slash = path.rfind('/');
        std::string folder = slash == std::string::npos ? " " : path.substr(0, slash);
        std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        auto dot = name.rfind('.');
        std::string extension = dot == std::string::npos ? " " : name.substr(dot + 1);
        if (dot != std::string::npos) name.resize(dot);
        tree[extension][folder].emplace_back(name, &bytes);
    }

    std::vector<std::uint8_t> directory, data;
    auto text = [&](const std::string& value) {
        directory.insert(directory.end(), value.begin(), value.end());
        directory.push_back(0);
    };
    auto number = [&](std::uint64_t value, int size) {
        for (int i = 0; i < size; ++i) directory.push_back(std::uint8_t(value >> (8 * i)));
    };
    for (const auto& [extension, folders] : tree) {
        text(extension);
        for (const auto& [folder, names] : folders) {
            text(folder);
            for (const auto& [name, bytes] : names) {
                text(name);
                number(crc32(0, bytes->data(), uInt(bytes->size())), 4);
                number(0, 2);
                number(inside_directory, 2);
                number(data.size(), 4);
                number(bytes->size(), 4);
                number(0xffff, 2);
                data.insert(data.end(), bytes->begin(), bytes->end());
            }
            text("");
        }
        text("");
    }
    text("");

    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    const std::uint32_t header[3] = {signature, 1, std::uint32_t(directory.size())};
    out.write(reinterpret_cast<const char*>(header), sizeof header);
    out.write(reinterpret_cast<const char*>(directory.data()), std::streamsize(directory.size()));
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    if (!out) {
        error = "Can't write " + file.string();
        return false;
    }
    return true;
}

}
