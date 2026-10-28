#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace mtfui::io {
    inline constexpr std::size_t kDdsMagicSize = 4;
    inline constexpr std::size_t kDdsHeaderSize = 124;
    inline constexpr std::size_t kDdsDx10Size = 20;

    // True when the magic and header in head announce a DX10 extension header after them.
    bool DdsHasDx10(const std::vector<std::uint8_t>& head);

    // Given the magic and header(s) of a 2D DDS, returns a header for the mip chain starting at the
    // first mip no larger than maxSize, plus how many data bytes to skip and keep. nullopt for
    // layouts it can't reason about (cubemaps, arrays, unknown formats).
    std::optional<std::vector<std::uint8_t>> ShrinkDds(const std::vector<std::uint8_t>& header, int maxSize, std::size_t* skipBytes, std::size_t* keepBytes);
}
