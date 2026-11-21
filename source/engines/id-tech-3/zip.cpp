#include "engines/id-tech-3/zip.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace id_tech_3::zip {

namespace {

constexpr std::uint32_t local_signature = 0x04034b50;
constexpr std::uint32_t central_signature = 0x02014b50;
constexpr std::uint32_t end_signature = 0x06054b50;

std::uint16_t u16(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    return at + 2 <= bytes.size() ? std::uint16_t(bytes[at] | bytes[at + 1] << 8) : 0;
}

std::uint32_t u32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    if (at + 4 > bytes.size()) return 0;
    return std::uint32_t(bytes[at]) | std::uint32_t(bytes[at + 1]) << 8 | std::uint32_t(bytes[at + 2]) << 16 |
           std::uint32_t(bytes[at + 3]) << 24;
}

void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(std::uint8_t(value));
    out.push_back(std::uint8_t(value >> 8));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(value >> (8 * i)));
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return c == '\\' ? '/' : std::tolower(c); });
    return value;
}

}

std::optional<archive> archive::open(const std::filesystem::path& file, std::string& error) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) {
        error = "Can't open " + file.string();
        return std::nullopt;
    }
    const std::uint64_t size = std::uint64_t(in.tellg());
    const std::uint64_t tail_size = std::min<std::uint64_t>(size, 65536 + 22);
    std::vector<std::uint8_t> tail(tail_size);
    in.seekg(std::streamoff(size - tail_size));
    in.read(reinterpret_cast<char*>(tail.data()), std::streamsize(tail_size));

    std::size_t end = std::string::npos;
    for (std::size_t at = tail_size >= 22 ? tail_size - 22 : 0;; --at) {
        if (u32(tail, at) == end_signature) { end = at; break; }
        if (at == 0) break;
    }
    if (end == std::string::npos) {
        error = file.filename().string() + " is not a zip file";
        return std::nullopt;
    }
    const std::uint16_t count = u16(tail, end + 10);
    const std::uint32_t directory_size = u32(tail, end + 12);
    const std::uint32_t directory_offset = u32(tail, end + 16);

    std::vector<std::uint8_t> directory(directory_size);
    in.seekg(directory_offset);
    in.read(reinterpret_cast<char*>(directory.data()), directory_size);
    if (!in) {
        error = file.filename().string() + " has a damaged file list";
        return std::nullopt;
    }

    archive result;
    result.location = file;
    std::size_t at = 0;
    for (std::uint16_t i = 0; i < count; ++i) {
        if (u32(directory, at) != central_signature) {
            error = file.filename().string() + " has a damaged file list";
            return std::nullopt;
        }
        entry item;
        item.method = u16(directory, at + 10);
        item.compressed_size = u32(directory, at + 20);
        item.size = u32(directory, at + 24);
        const std::uint16_t name_length = u16(directory, at + 28);
        const std::uint16_t extra_length = u16(directory, at + 30);
        const std::uint16_t comment_length = u16(directory, at + 32);
        item.header_offset = u32(directory, at + 42);
        if (at + 46 + name_length > directory.size()) break;
        item.path = lowercase(std::string(directory.begin() + std::ptrdiff_t(at + 46),
                                          directory.begin() + std::ptrdiff_t(at + 46 + name_length)));
        at += 46 + name_length + extra_length + comment_length;
        if (!item.path.empty() && item.path.back() != '/') result.items.push_back(std::move(item));
    }
    return result;
}

std::optional<std::vector<std::uint8_t>> archive::read(const entry& item) const {
    std::ifstream in(location, std::ios::binary);
    std::vector<std::uint8_t> header(30);
    in.seekg(item.header_offset);
    in.read(reinterpret_cast<char*>(header.data()), 30);
    if (!in || u32(header, 0) != local_signature) return std::nullopt;
    in.seekg(std::streamoff(item.header_offset) + 30 + u16(header, 26) + u16(header, 28));

    std::vector<std::uint8_t> packed(item.compressed_size);
    in.read(reinterpret_cast<char*>(packed.data()), std::streamsize(packed.size()));
    if (!in) return std::nullopt;
    if (item.method == 0) return packed;
    if (item.method != 8) return std::nullopt;

    std::vector<std::uint8_t> bytes(item.size);
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return std::nullopt;
    stream.next_in = packed.data();
    stream.avail_in = uInt(packed.size());
    stream.next_out = bytes.data();
    stream.avail_out = uInt(bytes.size());
    const int status = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (status != Z_STREAM_END || stream.total_out != item.size) return std::nullopt;
    return bytes;
}

bool write(const std::filesystem::path& file,
           const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files, std::string& error) {
    std::vector<std::uint8_t> out, directory;
    const std::uint16_t time = 0, date = (46 << 9) | (11 << 5) | 20;
    for (const auto& [path, bytes] : files) {
        const std::uint32_t crc = std::uint32_t(crc32(0, bytes.data(), uInt(bytes.size())));
        const std::uint32_t offset = std::uint32_t(out.size());
        const std::uint32_t size = std::uint32_t(bytes.size());

        put32(out, local_signature);
        put16(out, 20); put16(out, 0); put16(out, 0); put16(out, time); put16(out, date);
        put32(out, crc); put32(out, size); put32(out, size);
        put16(out, std::uint16_t(path.size())); put16(out, 0);
        out.insert(out.end(), path.begin(), path.end());
        out.insert(out.end(), bytes.begin(), bytes.end());

        put32(directory, central_signature);
        put16(directory, 20); put16(directory, 20); put16(directory, 0); put16(directory, 0);
        put16(directory, time); put16(directory, date);
        put32(directory, crc); put32(directory, size); put32(directory, size);
        put16(directory, std::uint16_t(path.size())); put16(directory, 0); put16(directory, 0);
        put16(directory, 0); put16(directory, 0); put32(directory, 0); put32(directory, offset);
        directory.insert(directory.end(), path.begin(), path.end());
    }
    const std::uint32_t directory_offset = std::uint32_t(out.size());
    out.insert(out.end(), directory.begin(), directory.end());
    put32(out, end_signature);
    put16(out, 0); put16(out, 0);
    put16(out, std::uint16_t(files.size())); put16(out, std::uint16_t(files.size()));
    put32(out, std::uint32_t(directory.size())); put32(out, directory_offset);
    put16(out, 0);

    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    if (!stream) {
        error = "Can't write " + file.string();
        return false;
    }
    return true;
}

}
