#include "render/DdsCache.h"

#include "game/Targets.h"
#include "io/UiSettings.h"

#include <condition_variable>
#include <cstring>
#include <thread>

namespace mtfui::render {
    namespace {
        constexpr std::size_t kMagic = 4;
        constexpr std::size_t kHeader = 124;
        constexpr std::size_t kDx10 = 20;
        constexpr std::uint64_t kEvictAfterFrames = 60 * 60;  // about a minute unused
        constexpr std::size_t kMaxEntries = 384;

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

        // Finds where the ink is on a small CPU-decoded mip. Overlay textures are mostly empty
        // canvas (transparent, or a flat color), with the design somewhere inside it.
        void FindContent(const std::vector<std::uint8_t>& dds, Texture& tex) {
            DirectX::TexMetadata meta{};
            DirectX::ScratchImage loaded;
            if (FAILED(DirectX::LoadFromDDSMemory(dds.data(), dds.size(), DirectX::DDS_FLAGS_NONE, &meta, loaded))) {
                return;
            }
            std::size_t mip = 0;
            while (mip + 1 < meta.mipLevels && std::max(meta.width >> mip, meta.height >> mip) > 128) {
                ++mip;
            }
            const DirectX::Image* src = loaded.GetImage(mip, 0, 0);
            if (!src) {
                return;
            }
            DirectX::ScratchImage rgba;
            const HRESULT hr = DirectX::IsCompressed(src->format)
                                   ? DirectX::Decompress(*src, DXGI_FORMAT_R8G8B8A8_UNORM, rgba)
                                   : DirectX::Convert(*src, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, rgba);
            const DirectX::Image* img = SUCCEEDED(hr) ? rgba.GetImage(0, 0, 0) : nullptr;
            if (!img || img->width < 4 || img->height < 4) {
                return;
            }
            const auto w = static_cast<int>(img->width), h = static_cast<int>(img->height);
            auto px = [&](int x, int y) { return img->pixels + static_cast<std::size_t>(y) * img->rowPitch + static_cast<std::size_t>(x) * 4; };

            int minA = 255, maxA = 0;
            long border[3] = {};
            int borderN = 0;
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const auto* p = px(x, y);
                    minA = std::min<int>(minA, p[3]);
                    maxA = std::max<int>(maxA, p[3]);
                    if (x == 0 || y == 0 || x == w - 1 || y == h - 1) {
                        border[0] += p[0], border[1] += p[1], border[2] += p[2];
                        ++borderN;
                    }
                }
            }
            const bool useAlpha = maxA - minA > 32;
            const int bg[3] = { static_cast<int>(border[0] / borderN), static_cast<int>(border[1] / borderN), static_cast<int>(border[2] / borderN) };
            auto ink = [&](const std::uint8_t* p) {
                if (useAlpha) {
                    return p[3] > minA + 24;
                }
                return std::max({ std::abs(p[0] - bg[0]), std::abs(p[1] - bg[1]), std::abs(p[2] - bg[2]) }) > 40;
            };

            int x0 = w, y0 = h, x1 = -1, y1 = -1;
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    if (ink(px(x, y))) {
                        x0 = std::min(x0, x), y0 = std::min(y0, y);
                        x1 = std::max(x1, x), y1 = std::max(y1, y);
                    }
                }
            }
            if (x1 < x0) {
                return;
            }
            tex.u0 = static_cast<float>(x0) / w;
            tex.v0 = static_cast<float>(y0) / h;
            tex.u1 = static_cast<float>(x1 + 1) / w;
            tex.v1 = static_cast<float>(y1 + 1) / h;
        }

        std::size_t MipBytes(std::uint32_t w, std::uint32_t h, std::pair<int, bool> fmt) {
            if (fmt.second) {
                return static_cast<std::size_t>(std::max(1u, (w + 3) / 4)) * std::max(1u, (h + 3) / 4) * fmt.first;
            }
            return static_cast<std::size_t>(w) * h * fmt.first;
        }
    }

    std::optional<std::vector<std::uint8_t>> ShrinkDds(const std::vector<std::uint8_t>& header, int maxSize, std::size_t* skipBytes,
        std::size_t* keepBytes) {
        if (header.size() < kMagic + kHeader || U32(header.data()) != FourCC('D', 'D', 'S', ' ')) {
            return std::nullopt;
        }
        const std::uint8_t* h = header.data() + kMagic;
        const bool dx10 = (U32(h + 76) & 0x4) && U32(h + 80) == FourCC('D', 'X', '1', '0');
        if (dx10 && header.size() < kMagic + kHeader + kDx10) {
            return std::nullopt;
        }
        const std::uint32_t caps2 = U32(h + 108);
        if (caps2 & 0x200) {  // cubemap
            return std::nullopt;
        }
        std::uint32_t dxgi = 0;
        if (dx10) {
            const std::uint8_t* x = h + kHeader;
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

        std::vector<std::uint8_t> out(header.begin(), header.begin() + static_cast<std::ptrdiff_t>(kMagic + kHeader + (dx10 ? kDx10 : 0)));
        std::uint8_t* o = out.data() + kMagic;
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

    DdsCache& DdsCache::Get() {
        static DdsCache instance;
        return instance;
    }

    void DdsCache::Init(ID3D11Device* device) {
        device_ = device;
        if (device_ && !thread_.joinable()) {
            thread_ = std::thread([this] { Worker(); });
        }
    }

    void DdsCache::Shutdown() {
        stop_ = true;
        cv_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
        Clear();
    }

    const Texture* DdsCache::Request(const std::string& path, int maxSize) {
        if (path.empty() || !device_) {
            return nullptr;
        }
        const std::string key = io::ToLower(path) + "@" + std::to_string(maxSize);
        std::scoped_lock lock(mutex_);
        if (auto it = entries_.find(key); it != entries_.end()) {
            it->second.lastUsed = frame_;
            return it->second.failed || !it->second.tex.srv ? nullptr : &it->second.tex;
        }
        if (!queued_.contains(key)) {
            queued_[key] = true;
            // Newest requests first: they are what is on screen now.
            queue_.push_front({ key, path, maxSize });
            if (queue_.size() > 256) {
                queued_.erase(queue_.back().key);
                queue_.pop_back();
            }
            cv_.notify_one();
        }
        return nullptr;
    }

    bool DdsCache::Failed(const std::string& path, int maxSize) const {
        const std::string key = io::ToLower(path) + "@" + std::to_string(maxSize);
        std::scoped_lock lock(mutex_);
        auto it = entries_.find(key);
        return it != entries_.end() && it->second.failed;
    }

    int DdsCache::Pending() const {
        std::scoped_lock lock(mutex_);
        return static_cast<int>(queue_.size());
    }

    void DdsCache::EndFrame() {
        std::scoped_lock lock(mutex_);
        ++frame_;
        if (frame_ % 120 != 0) {
            return;
        }
        for (auto it = entries_.begin(); it != entries_.end();) {
            const bool stale = frame_ - it->second.lastUsed > kEvictAfterFrames;
            if (stale || (entries_.size() > kMaxEntries && frame_ - it->second.lastUsed > 120)) {
                if (it->second.tex.srv) {
                    it->second.tex.srv->Release();
                }
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void DdsCache::Clear() {
        std::scoped_lock lock(mutex_);
        for (auto& [k, e] : entries_) {
            if (e.tex.srv) {
                e.tex.srv->Release();
            }
        }
        entries_.clear();
        queue_.clear();
        queued_.clear();
    }

    void DdsCache::Worker() {
        while (!stop_) {
            Job job;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                if (stop_) {
                    return;
                }
                job = std::move(queue_.front());
                queue_.pop_front();
            }

            Entry entry;
            entry.failed = true;
            try {
                const auto resPath = game::TexturePath(job.path);
                RE::BSResourceNiBinaryStream stream(resPath.c_str());
                if (stream.good() && stream.stream) {
                    const std::size_t total = stream.stream->totalSize;
                    std::vector<std::uint8_t> head(kMagic + kHeader);
                    if (total >= head.size() && stream.read(reinterpret_cast<char*>(head.data()), static_cast<std::uint32_t>(head.size()))) {
                        const std::uint8_t* h = head.data() + kMagic;
                        if ((U32(h + 76) & 0x4) && U32(h + 80) == FourCC('D', 'X', '1', '0')) {
                            head.resize(head.size() + kDx10);
                            stream.read(reinterpret_cast<char*>(head.data() + kMagic + kHeader), static_cast<std::uint32_t>(kDx10));
                        }
                        std::size_t skip = 0, keep = 0;
                        std::vector<std::uint8_t> bytes;
                        if (auto shrunk = ShrinkDds(head, job.maxSize, &skip, &keep); shrunk && head.size() + skip + keep <= total) {
                            const auto before = stream.tell();
                            if (skip > 0) {
                                stream.seek(static_cast<std::int32_t>(skip));
                            }
                            if (stream.tell() == before + skip) {
                                bytes = std::move(*shrunk);
                                bytes.resize(bytes.size() + keep);
                                if (!stream.read(reinterpret_cast<char*>(bytes.data() + bytes.size() - keep), static_cast<std::uint32_t>(keep))) {
                                    bytes.clear();
                                }
                            }
                        }
                        if (bytes.empty()) {
                            // Unknown layout: load the whole file and let DirectXTK drop large mips.
                            RE::BSResourceNiBinaryStream whole(resPath.c_str());
                            bytes.resize(total);
                            if (!whole.good() || !whole.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::uint32_t>(total))) {
                                bytes.clear();
                            }
                        }
                        if (!bytes.empty()) {
                            ID3D11ShaderResourceView* srv = nullptr;
                            ID3D11Resource* res = nullptr;
                            const HRESULT hr = DirectX::CreateDDSTextureFromMemoryEx(device_, bytes.data(), bytes.size(), static_cast<size_t>(job.maxSize),
                                D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, DirectX::DDS_LOADER_DEFAULT, &res, &srv);
                            if (SUCCEEDED(hr) && srv) {
                                D3D11_TEXTURE2D_DESC desc{};
                                ID3D11Texture2D* tex2d = nullptr;
                                if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex2d)))) {
                                    tex2d->GetDesc(&desc);
                                    tex2d->Release();
                                }
                                entry.tex.srv = srv;
                                entry.tex.width = static_cast<int>(desc.Width);
                                entry.tex.height = static_cast<int>(desc.Height);
                                entry.failed = false;
                                FindContent(bytes, entry.tex);
                            } else {
                                logger::warn("Could not decode {} (hr {:08X})", job.path, static_cast<std::uint32_t>(hr));
                            }
                            if (res) {
                                res->Release();
                            }
                        }
                    }
                } else {
                    logger::warn("Texture not found: {}", resPath);
                }
            } catch (...) {
                logger::warn("Texture load threw: {}", job.path);
            }

            std::scoped_lock lock(mutex_);
            queued_.erase(job.key);
            entry.lastUsed = frame_;
            if (auto it = entries_.find(job.key); it != entries_.end() && it->second.tex.srv) {
                it->second.tex.srv->Release();
            }
            entries_[job.key] = entry;
        }
    }
}
