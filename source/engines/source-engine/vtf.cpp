#include "engines/source-engine/vtf.hpp"

#include <stb_dxt.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace source_engine::vtf {

namespace {

std::uint32_t u32(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::uint32_t value = 0;
    if (at + 4 <= bytes.size()) std::memcpy(&value, bytes.data() + at, 4);
    return value;
}

std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::uint16_t value = 0;
    if (at + 2 <= bytes.size()) std::memcpy(&value, bytes.data() + at, 2);
    return value;
}

int bytes_per_pixel(int format) {
    switch (format) {
    case rgba8888: case abgr8888: case argb8888: case bgra8888: case bgrx8888: case uvwq8888: case uvlx8888:
        return 4;
    case rgb888: case bgr888: case rgb888_bluescreen: case bgr888_bluescreen:
        return 3;
    case rgb565: case ia88: case bgr565: case bgrx5551: case bgra4444: case bgra5551: case uv88:
        return 2;
    case i8: case p8: case a8:
        return 1;
    case rgba16161616f: case rgba16161616:
        return 8;
    default:
        return 0;
    }
}

bool compressed(int format) {
    return format == dxt1 || format == dxt1_one_bit_alpha || format == dxt3 || format == dxt5;
}

std::size_t image_bytes(int format, int width, int height, int depth) {
    const std::size_t blocks = std::size_t((width + 3) / 4) * std::size_t((height + 3) / 4);
    if (format == dxt1 || format == dxt1_one_bit_alpha) return blocks * 8 * depth;
    if (format == dxt3 || format == dxt5) return blocks * 16 * depth;
    return std::size_t(bytes_per_pixel(format)) * width * height * depth;
}

int level_size(int size, int level) { return std::max(1, size >> level); }

void expand_565(std::uint16_t color, std::uint8_t* out) {
    int red = (color >> 11) & 31, green = (color >> 5) & 63, blue = color & 31;
    out[0] = std::uint8_t(red << 3 | red >> 2);
    out[1] = std::uint8_t(green << 2 | green >> 4);
    out[2] = std::uint8_t(blue << 3 | blue >> 2);
    out[3] = 255;
}

void color_block(const std::uint8_t* block, std::uint8_t out[16][4], bool three_color_allowed) {
    std::uint16_t c0 = std::uint16_t(block[0] | block[1] << 8), c1 = std::uint16_t(block[2] | block[3] << 8);
    std::uint8_t palette[4][4];
    expand_565(c0, palette[0]);
    expand_565(c1, palette[1]);
    if (!three_color_allowed || c0 > c1) {
        for (int i = 0; i < 3; ++i) {
            palette[2][i] = std::uint8_t((2 * palette[0][i] + palette[1][i]) / 3);
            palette[3][i] = std::uint8_t((palette[0][i] + 2 * palette[1][i]) / 3);
        }
        palette[2][3] = palette[3][3] = 255;
    } else {
        for (int i = 0; i < 3; ++i) palette[2][i] = std::uint8_t((palette[0][i] + palette[1][i]) / 2);
        palette[2][3] = 255;
        std::memset(palette[3], 0, 4);
    }
    std::uint32_t bits = std::uint32_t(block[4]) | std::uint32_t(block[5]) << 8 | std::uint32_t(block[6]) << 16 | std::uint32_t(block[7]) << 24;
    for (int i = 0; i < 16; ++i) std::memcpy(out[i], palette[(bits >> (2 * i)) & 3], 4);
}

void explicit_alpha(const std::uint8_t* block, std::uint8_t out[16][4]) {
    for (int i = 0; i < 16; ++i) out[i][3] = std::uint8_t(((block[i / 2] >> (4 * (i % 2))) & 15) * 17);
}

void interpolated_alpha(const std::uint8_t* block, std::uint8_t out[16][4]) {
    int a0 = block[0], a1 = block[1];
    int table[8] = {a0, a1};
    if (a0 > a1) {
        for (int i = 1; i <= 6; ++i) table[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    } else {
        for (int i = 1; i <= 4; ++i) table[i + 1] = ((5 - i) * a0 + i * a1) / 5;
        table[6] = 0;
        table[7] = 255;
    }
    std::uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= std::uint64_t(block[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) out[i][3] = std::uint8_t(table[(bits >> (3 * i)) & 7]);
}

image::picture decode_blocks(int format, int width, int height, std::span<const std::uint8_t> data) {
    image::picture result = image::blank(width, height, {0, 0, 0, 255});
    const int block_bytes = format == dxt3 || format == dxt5 ? 16 : 8;
    const int across = (width + 3) / 4, down = (height + 3) / 4;
    for (int by = 0; by < down; ++by) {
        for (int bx = 0; bx < across; ++bx) {
            const std::uint8_t* block = data.data() + (std::size_t(by) * across + bx) * block_bytes;
            std::uint8_t pixels[16][4];
            if (format == dxt3) {
                color_block(block + 8, pixels, false);
                explicit_alpha(block, pixels);
            } else if (format == dxt5) {
                color_block(block + 8, pixels, false);
                interpolated_alpha(block, pixels);
            } else {
                color_block(block, pixels, true);
            }
            for (int i = 0; i < 16; ++i) {
                int x = bx * 4 + i % 4, y = by * 4 + i / 4;
                if (x < width && y < height) std::memcpy(result.at(x, y), pixels[i], 4);
            }
        }
    }
    return result;
}

std::optional<image::picture> decode_pixels(int format, int width, int height, std::span<const std::uint8_t> data) {
    const int size = bytes_per_pixel(format);
    image::picture result = image::blank(width, height, {0, 0, 0, 255});
    for (int i = 0; i < width * height; ++i) {
        const std::uint8_t* in = data.data() + std::size_t(i) * size;
        std::uint8_t* out = result.pixels.data() + std::size_t(i) * 4;
        auto set = [&](int r, int g, int b, int a) {
            out[0] = std::uint8_t(r); out[1] = std::uint8_t(g); out[2] = std::uint8_t(b); out[3] = std::uint8_t(a);
        };
        std::uint16_t packed = size == 2 ? std::uint16_t(in[0] | in[1] << 8) : 0;
        auto five = [](int v) { return (v << 3) | (v >> 2); };
        auto six = [](int v) { return (v << 2) | (v >> 4); };
        switch (format) {
        case rgba8888: set(in[0], in[1], in[2], in[3]); break;
        case abgr8888: set(in[3], in[2], in[1], in[0]); break;
        case argb8888: set(in[1], in[2], in[3], in[0]); break;
        case bgra8888: set(in[2], in[1], in[0], in[3]); break;
        case bgrx8888: set(in[2], in[1], in[0], 255); break;
        case rgb888: set(in[0], in[1], in[2], 255); break;
        case bgr888: set(in[2], in[1], in[0], 255); break;
        case rgb888_bluescreen:
            set(in[0], in[1], in[2], in[0] == 0 && in[1] == 0 && in[2] == 255 ? 0 : 255);
            break;
        case bgr888_bluescreen:
            set(in[2], in[1], in[0], in[2] == 0 && in[1] == 0 && in[0] == 255 ? 0 : 255);
            break;
        case i8: set(in[0], in[0], in[0], 255); break;
        case ia88: set(in[0], in[0], in[0], in[1]); break;
        case a8: set(0, 0, 0, in[0]); break;
        case rgb565: set(five(packed & 31), six((packed >> 5) & 63), five(packed >> 11), 255); break;
        case bgr565: set(five(packed >> 11), six((packed >> 5) & 63), five(packed & 31), 255); break;
        case bgra4444:
            set(((packed >> 8) & 15) * 17, ((packed >> 4) & 15) * 17, (packed & 15) * 17, (packed >> 12) * 17);
            break;
        case bgrx5551: set(five((packed >> 10) & 31), five((packed >> 5) & 31), five(packed & 31), 255); break;
        case bgra5551:
            set(five((packed >> 10) & 31), five((packed >> 5) & 31), five(packed & 31), packed & 0x8000 ? 255 : 0);
            break;
        default: return std::nullopt;
        }
    }
    return result;
}

std::vector<std::uint8_t> compress(const image::picture& source, bool alpha) {
    const int across = (source.width + 3) / 4, down = (source.height + 3) / 4;
    std::vector<std::uint8_t> out(std::size_t(across) * down * (alpha ? 16 : 8));
    std::uint8_t* write = out.data();
    for (int by = 0; by < down; ++by) {
        for (int bx = 0; bx < across; ++bx) {
            std::uint8_t block[64];
            for (int i = 0; i < 16; ++i) {
                int x = std::min(bx * 4 + i % 4, source.width - 1), y = std::min(by * 4 + i / 4, source.height - 1);
                std::memcpy(block + i * 4, source.at(x, y), 4);
            }
            stb_compress_dxt_block(write, block, alpha ? 1 : 0, STB_DXT_HIGHQUAL);
            write += alpha ? 16 : 8;
        }
    }
    return out;
}

void put_u32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t value) { std::memcpy(out.data() + at, &value, 4); }
void put_u16(std::vector<std::uint8_t>& out, std::size_t at, std::uint16_t value) { std::memcpy(out.data() + at, &value, 2); }
void put_f32(std::vector<std::uint8_t>& out, std::size_t at, float value) { std::memcpy(out.data() + at, &value, 4); }

}

std::optional<info> inspect(std::span<const std::uint8_t> header) {
    if (header.size() < 64 || std::memcmp(header.data(), "VTF\0", 4) != 0) return std::nullopt;
    info texture;
    texture.major = int(u32(header, 4));
    texture.minor = int(u32(header, 8));
    const std::uint32_t header_size = u32(header, 12);
    texture.width = u16(header, 16);
    texture.height = u16(header, 18);
    texture.flags = u32(header, 20);
    texture.frames = std::max<int>(1, u16(header, 24));
    const std::uint16_t first_frame = u16(header, 26);
    texture.format = int(u32(header, 52));
    texture.mipmaps = std::max<int>(1, header[56]);
    const int low_format = int(u32(header, 57));
    const int low_width = header[61], low_height = header[62];
    texture.depth = texture.minor >= 2 ? std::max<int>(1, u16(header, 63)) : 1;
    if (texture.flags & flag::environment_map)
        texture.faces = first_frame != 0xffff && texture.minor < 5 ? 7 : 6;
    if (texture.major != 7 || texture.width == 0 || texture.height == 0) return std::nullopt;
    if (bytes_per_pixel(texture.format) == 0 && !compressed(texture.format)) return std::nullopt;

    if (texture.minor >= 3) {
        const std::uint32_t resources = u32(header, 68);
        bool found = false;
        for (std::uint32_t i = 0; i < resources && 80 + 8 * (i + 1) <= header.size(); ++i) {
            const std::size_t at = 80 + 8 * std::size_t(i);
            if (header[at] == 0x30 && header[at + 1] == 0 && header[at + 2] == 0) {
                texture.data_offset = u32(header, at + 4);
                found = true;
            }
        }
        if (!found) return std::nullopt;
    } else {
        texture.data_offset = header_size;
        if (low_format != -1 && low_width > 0 && low_height > 0)
            texture.data_offset += image_bytes(low_format, low_width, low_height, 1);
    }
    return texture;
}

std::string format_name(int format) {
    static const std::array<const char*, 27> names{
        "RGBA8888", "ABGR8888", "RGB888", "BGR888", "RGB565", "I8", "IA88", "P8", "A8",
        "RGB888 bluescreen", "BGR888 bluescreen", "ARGB8888", "BGRA8888", "DXT1", "DXT3", "DXT5",
        "BGRX8888", "BGR565", "BGRX5551", "BGRA4444", "DXT1 one-bit alpha", "BGRA5551", "UV88",
        "UVWQ8888", "RGBA16161616F", "RGBA16161616", "UVLX8888"};
    if (format >= 0 && format < int(names.size())) return names[std::size_t(format)];
    return "unknown";
}

int level_for(const info& texture, int at_least) {
    int level = 0;
    if (at_least <= 0) return 0;
    while (level + 1 < texture.mipmaps &&
           std::max(level_size(texture.width, level + 1), level_size(texture.height, level + 1)) >= at_least)
        ++level;
    return level;
}

std::pair<std::size_t, std::size_t> locate(const info& texture, int level) {
    std::size_t offset = texture.data_offset;
    for (int m = texture.mipmaps - 1; m > level; --m)
        offset += image_bytes(texture.format, level_size(texture.width, m), level_size(texture.height, m),
                              level_size(texture.depth, m)) * std::size_t(texture.frames) * std::size_t(texture.faces);
    return {offset, image_bytes(texture.format, level_size(texture.width, level), level_size(texture.height, level), 1)};
}

std::optional<image::picture> decode_level(const info& texture, int level, std::span<const std::uint8_t> slice) {
    const int width = level_size(texture.width, level), height = level_size(texture.height, level);
    if (slice.size() < image_bytes(texture.format, width, height, 1)) return std::nullopt;
    if (compressed(texture.format)) return decode_blocks(texture.format, width, height, slice);
    return decode_pixels(texture.format, width, height, slice);
}

std::optional<image::picture> decode(std::span<const std::uint8_t> file, int at_least) {
    auto texture = inspect(file);
    if (!texture) return std::nullopt;
    int level = level_for(*texture, at_least);
    auto [offset, length] = locate(*texture, level);
    if (offset + length > file.size()) return std::nullopt;
    return decode_level(*texture, level, file.subspan(offset, length));
}

std::vector<std::uint8_t> encode(const image::picture& top, std::uint32_t original_flags) {
    const bool alpha = image::has_transparency(top);
    const bool mipmaps = !(original_flags & flag::no_mipmaps);

    std::vector<image::picture> levels{top};
    while (mipmaps && (levels.back().width > 1 || levels.back().height > 1))
        levels.push_back(image::resize(levels.back(), std::max(1, levels.back().width / 2), std::max(1, levels.back().height / 2)));

    int low_width = top.width, low_height = top.height;
    while (low_width > 16 || low_height > 16) {
        low_width = std::max(1, low_width / 2);
        low_height = std::max(1, low_height / 2);
    }

    double sum[3] = {};
    for (std::size_t i = 0; i < top.pixels.size(); i += 4)
        for (int c = 0; c < 3; ++c) sum[c] += std::pow(top.pixels[i + std::size_t(c)] / 255.0, 2.2);
    const double count = std::max<double>(1, double(top.width) * top.height);

    constexpr std::uint32_t kept = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | flag::no_mipmaps | 0x200 | 0x400;
    const std::uint32_t flags = (original_flags & kept) | (alpha ? flag::eight_bit_alpha : 0);

    std::vector<std::uint8_t> out(80, 0);
    std::memcpy(out.data(), "VTF\0", 4);
    put_u32(out, 4, 7);
    put_u32(out, 8, 2);
    put_u32(out, 12, 80);
    put_u16(out, 16, std::uint16_t(top.width));
    put_u16(out, 18, std::uint16_t(top.height));
    put_u32(out, 20, flags);
    put_u16(out, 24, 1);
    put_u16(out, 26, 0);
    for (int c = 0; c < 3; ++c) put_f32(out, 32 + std::size_t(c) * 4, float(sum[c] / count));
    put_f32(out, 48, 1.0f);
    put_u32(out, 52, std::uint32_t(alpha ? dxt5 : dxt1));
    out[56] = std::uint8_t(levels.size());
    put_u32(out, 57, std::uint32_t(dxt1));
    out[61] = std::uint8_t(low_width);
    out[62] = std::uint8_t(low_height);
    put_u16(out, 63, 1);

    auto low = compress(image::resize(top, low_width, low_height), false);
    out.insert(out.end(), low.begin(), low.end());
    for (auto level = levels.rbegin(); level != levels.rend(); ++level) {
        auto data = compress(*level, alpha);
        out.insert(out.end(), data.begin(), data.end());
    }
    return out;
}

}
