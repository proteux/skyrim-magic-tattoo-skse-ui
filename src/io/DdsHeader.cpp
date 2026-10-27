#include "io/DdsHeader.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace mtfui::io {
    namespace {
        std::uint32_t U32(const std::uint8_t* p) {
            std::uint32_t v;
            std::memcpy(&v, p, 4);
            return v;
        }
        void SetU32(std::uint8_t* p, std::uint32_t v) { std::memcpy(p, &v, 4); }

        constexpr std::uint32_t FourCC(char a, char b, char c, char d) {
            return static_cast<std::uint32_t>(a) | (static_cast<std::uint32_t>(b) << 8) | (static_cast<std::uint32_t>(c) << 16) |
                   (static_cast<std::uint32_t>(d) << 24);
        }

        // Returns {blockBytes, true} for block-compressed formats or {bytesPerPixel, false}.
        std::optional<std::pair<int, bool>> FormatInfo(const std::uint8_t* h, bool dx10, std::uint32_t dxgi) {
            if (dx10) {
                if (dxgi >= 70 && dxgi <= 72) return std::pair{ 8, true };
                if (dxgi >= 73 && dxgi <= 78) return std::pair{ 16, true };
                if (dxgi >= 79 && dxgi <= 81) return std::pair{ 8, true };
                if (dxgi >= 82 && dxgi <= 84) return std::pair{ 16, true };
                if (dxgi >= 94 && dxgi <= 99) return std::pair{ 16, true };
                if ((dxgi >= 27 && dxgi <= 32) || (dxgi >= 87 && dxgi <= 93)) return std::pair{ 4, false };
                if (dxgi == 61 || dxgi == 62 || dxgi == 63 || dxgi == 64 || dxgi == 65) return std::pair{ 1, false };
                return std::nullopt;
            }
            const std::uint32_t pfFlags = U32(h + 76);
            const std::uint32_t fourcc = U32(h + 80);
            if (pfFlags & 0x4) {
                switch (fourcc) {
                    case FourCC('D', 'X', 'T', '1'):
                    case FourCC('A', 'T', 'I', '1'):
                    case FourCC('B', 'C', '4', 'U'):
                    case FourCC('B', 'C', '4', 'S'):
                        return std::pair{ 8, true };
                    case FourCC('D', 'X', 'T', '2'):
                    case FourCC('D', 'X', 'T', '3'):
                    case FourCC('D', 'X', 'T', '4'):
                    case FourCC('D', 'X', 'T', '5'):
                    case FourCC('A', 'T', 'I', '2'):
                    case FourCC('B', 'C', '5', 'U'):
                    case FourCC('B', 'C', '5', 'S'):
                        return std::pair{ 16, true };
                    default:
                        return std::nullopt;
                }
            }
            const std::uint32_t bits = U32(h + 84);
            if (bits == 8 || bits == 16 || bits == 24 || bits == 32) {
                return std::pair{ static_cast<int>(bits / 8), false };
            }
            return std::nullopt;
        }

        std::size_t MipBytes(std::uint32_t w, std::uint32_t h, std::pair<int, bool> fmt) {
            if (fmt.second) {
                return static_cast<std::size_t>(std::max(1u, (w + 3) / 4)) * std::max(1u, (h + 3) / 4) * fmt.first;
            }
            return static_cast<std::size_t>(w) * h * fmt.first;
        }
    }

    bool DdsHasDx10(const std::vector<std::uint8_t>& head) {
        if (head.size() < kDdsMagicSize + kDdsHeaderSize) {
            return false;
        }
        const std::uint8_t* h = head.data() + kDdsMagicSize;
        return (U32(h + 76) & 0x4) && U32(h + 80) == FourCC('D', 'X', '1', '0');
    }

    std::optional<std::vector<std::uint8_t>> ShrinkDds(const std::vector<std::uint8_t>& header, int maxSize, std::size_t* skipBytes,
        std::size_t* keepBytes) {
        if (header.size() < kDdsMagicSize + kDdsHeaderSize || U32(header.data()) != FourCC('D', 'D', 'S', ' ') || maxSize < 1) {
            return std::nullopt;
        }
        const std::uint8_t* h = header.data() + kDdsMagicSize;
        const bool dx10 = DdsHasDx10(header);
        if (dx10 && header.size() < kDdsMagicSize + kDdsHeaderSize + kDdsDx10Size) {
            return std::nullopt;
        }
        const std::uint32_t caps2 = U32(h + 108);
        if (caps2 & 0x200) {  // cubemap
            return std::nullopt;
        }
        std::uint32_t dxgi = 0;
        if (dx10) {
            const std::uint8_t* x = h + kDdsHeaderSize;
            dxgi = U32(x);
            if (U32(x + 12) > 1) {  // texture array
                return std::nullopt;
            }
        }
        const auto fmt = FormatInfo(h, dx10, dxgi);
        if (!fmt) {
            return std::nullopt;
        }
        std::uint32_t w = U32(h + 12);
        std::uint32_t ht = U32(h + 8);
        if (w == 0 || ht == 0) {
            return std::nullopt;
        }
        const std::uint32_t flags = U32(h + 4);
        std::uint32_t mips = (flags & 0x20000) ? std::max(1u, U32(h + 24)) : 1u;

        std::size_t skip = 0;
        std::uint32_t first = 0;
        while (first + 1 < mips && std::max(w, ht) > static_cast<std::uint32_t>(maxSize)) {
            skip += MipBytes(w, ht, *fmt);
            w = std::max(1u, w / 2);
            ht = std::max(1u, ht / 2);
            ++first;
        }
        std::size_t keep = 0;
        std::uint32_t mw = w, mh = ht;
        for (std::uint32_t i = first; i < mips; ++i) {
            keep += MipBytes(mw, mh, *fmt);
            mw = std::max(1u, mw / 2);
            mh = std::max(1u, mh / 2);
        }

        std::vector<std::uint8_t> out(header.begin(), header.begin() + static_cast<std::ptrdiff_t>(kDdsMagicSize + kDdsHeaderSize + (dx10 ? kDdsDx10Size : 0)));
        std::uint8_t* o = out.data() + kDdsMagicSize;
        SetU32(o + 8, ht);
        SetU32(o + 12, w);
        SetU32(o + 24, mips - first);
        if (fmt->second) {
            SetU32(o + 16, static_cast<std::uint32_t>(MipBytes(w, ht, *fmt)));
        }
        *skipBytes = skip;
        *keepBytes = keep;
        return out;
    }
}
