#include "render/DdsCache.h"

#include "game/Targets.h"
#include "io/DdsHeader.h"
#include "io/UiSettings.h"

#include <condition_variable>
#include <cstring>
#include <thread>

namespace mtfui::render {
    namespace {
        constexpr std::uint64_t kEvictAfterFrames = 60 * 60;  // about a minute unused
        constexpr std::size_t kMaxEntries = 384;
        constexpr std::size_t kMaxBytes = 256u << 20;

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

    }

    DdsCache& DdsCache::Get() {
        static DdsCache instance;
        return instance;
    }

    DdsCache::~DdsCache() {
        // Static destruction runs at process exit, after Windows has already ended the worker and possibly the
        // D3D device: joining or releasing here could hang or crash, so only let go of the thread.
        stop_ = true;
        cv_.notify_all();
        if (thread_.joinable()) {
            thread_.detach();
        }
    }

    void DdsCache::Init(ID3D11Device* device) {
        device_ = device;
        if (device_ && !thread_.joinable()) {
            stop_ = false;
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
        std::scoped_lock lock(mutex_);
        for (const auto& r : retired_) {
            r.srv->Release();
        }
        retired_.clear();
    }

    void DdsCache::Retire(ID3D11ShaderResourceView* srv) {
        if (srv) {
            retired_.push_back({ srv, frame_ });
        }
    }

    std::size_t DdsCache::Bytes() const {
        std::scoped_lock lock(mutex_);
        return bytes_;
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
        // A view retired this frame may still be in this frame's draw lists; free it once those have been rendered.
        std::erase_if(retired_, [this](const Retired& r) {
            if (frame_ - r.frame < 2) {
                return false;
            }
            r.srv->Release();
            return true;
        });

        const bool overBudget = bytes_ > kMaxBytes;
        if (frame_ % 120 != 0 && !overBudget) {
            return;
        }
        for (auto it = entries_.begin(); it != entries_.end();) {
            const bool stale = frame_ - it->second.lastUsed > kEvictAfterFrames;
            if (stale || (entries_.size() > kMaxEntries && frame_ - it->second.lastUsed > 120)) {
                Retire(it->second.tex.srv);
                bytes_ -= it->second.bytes;
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
        if (bytes_ > kMaxBytes) {
            // Least recently used first; anything used in the last couple of frames is on screen and stays.
            std::vector<std::pair<std::uint64_t, std::string>> lru;
            for (const auto& [k, e] : entries_) {
                if (e.bytes > 0 && frame_ - e.lastUsed > 2) {
                    lru.emplace_back(e.lastUsed, k);
                }
            }
            std::sort(lru.begin(), lru.end());
            for (const auto& [_, k] : lru) {
                if (bytes_ <= kMaxBytes * 3 / 4) {
                    break;
                }
                auto it = entries_.find(k);
                Retire(it->second.tex.srv);
                bytes_ -= it->second.bytes;
                entries_.erase(it);
            }
        }
    }

    void DdsCache::Clear() {
        std::scoped_lock lock(mutex_);
        for (auto& [k, e] : entries_) {
            Retire(e.tex.srv);
        }
        entries_.clear();
        bytes_ = 0;
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
                    std::vector<std::uint8_t> head(io::kDdsMagicSize + io::kDdsHeaderSize);
                    if (total >= head.size() && stream.read(reinterpret_cast<char*>(head.data()), static_cast<std::uint32_t>(head.size()))) {
                        if (io::DdsHasDx10(head)) {
                            head.resize(head.size() + io::kDdsDx10Size);
                            stream.read(reinterpret_cast<char*>(head.data() + io::kDdsMagicSize + io::kDdsHeaderSize), static_cast<std::uint32_t>(io::kDdsDx10Size));
                        }
                        std::size_t skip = 0, keep = 0;
                        std::vector<std::uint8_t> bytes;
                        if (auto shrunk = io::ShrinkDds(head, job.maxSize, &skip, &keep); shrunk && head.size() + skip + keep <= total) {
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
                                // Full mip chain is about a third more than the top level.
                                entry.bytes = static_cast<std::size_t>(desc.Width) * desc.Height * DirectX::BitsPerPixel(desc.Format) / 8 * 4 / 3;
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
            ++generation_;
            entry.lastUsed = frame_;
            if (auto it = entries_.find(job.key); it != entries_.end() && !it->second.failed) {
                // A duplicate load: the UI may already be drawing the existing texture, so keep that one.
                if (entry.tex.srv) {
                    entry.tex.srv->Release();
                }
                continue;
            } else if (it != entries_.end()) {
                bytes_ -= it->second.bytes;
            }
            bytes_ += entry.bytes;
            entries_[job.key] = entry;
        }
    }
}
