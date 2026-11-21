#pragma once

#include "image/image.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace source_engine::vtf {

enum format : int {
    rgba8888 = 0, abgr8888 = 1, rgb888 = 2, bgr888 = 3, rgb565 = 4, i8 = 5, ia88 = 6, p8 = 7,
    a8 = 8, rgb888_bluescreen = 9, bgr888_bluescreen = 10, argb8888 = 11, bgra8888 = 12,
    dxt1 = 13, dxt3 = 14, dxt5 = 15, bgrx8888 = 16, bgr565 = 17, bgrx5551 = 18, bgra4444 = 19,
    dxt1_one_bit_alpha = 20, bgra5551 = 21, uv88 = 22, uvwq8888 = 23, rgba16161616f = 24,
    rgba16161616 = 25, uvlx8888 = 26,
};

namespace flag {
inline constexpr std::uint32_t no_mipmaps = 0x100;
inline constexpr std::uint32_t one_bit_alpha = 0x1000;
inline constexpr std::uint32_t eight_bit_alpha = 0x2000;
inline constexpr std::uint32_t environment_map = 0x4000;
}

struct info {
    int major = 0;
    int minor = 0;
    int width = 0;
    int height = 0;
    int depth = 1;
    std::uint32_t flags = 0;
    int frames = 1;
    int faces = 1;
    int format = -1;
    int mipmaps = 1;
    std::size_t data_offset = 0;
};

inline constexpr std::size_t header_read_size = 1024;

std::optional<info> inspect(std::span<const std::uint8_t> header);
std::string format_name(int format);
int level_for(const info& texture, int at_least);
std::pair<std::size_t, std::size_t> locate(const info& texture, int level);
std::optional<image::picture> decode_level(const info& texture, int level, std::span<const std::uint8_t> slice);
std::optional<image::picture> decode(std::span<const std::uint8_t> file, int at_least = 0);

std::vector<std::uint8_t> encode(const image::picture& top, std::uint32_t original_flags);

}
