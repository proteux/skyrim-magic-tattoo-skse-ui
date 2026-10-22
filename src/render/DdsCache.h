#pragma once

#include <condition_variable>
#include <thread>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace mtfui::render {
    struct Texture {
        ID3D11ShaderResourceView* srv = nullptr;
        int width = 0;
        int height = 0;
        // Bounding box of the visible ink in UV space; the full texture when nothing stands out.
        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    };

    // Loads DDS textures (loose or from BSAs) on a worker thread, keeping only the mips that
    // fit maxSize so 4K overlays stay cheap. UI thread calls Request every frame it needs one.
    class DdsCache {
    public:
        static DdsCache& Get();

        void Init(ID3D11Device* device);
        void Shutdown();

        // nullptr until loaded (or if it failed; see Failed).
        const Texture* Request(const std::string& path, int maxSize);
        bool Failed(const std::string& path, int maxSize) const;
        void EndFrame();  // evicts textures unused for a while
        void Clear();
        int Pending() const;

    private:
        struct Entry {
            Texture tex;
            bool failed = false;
            std::uint64_t lastUsed = 0;
        };
        struct Job {
            std::string key;
            std::string path;
            int maxSize = 0;
        };

        void Worker();

        ID3D11Device* device_ = nullptr;
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::unordered_map<std::string, Entry> entries_;
        std::deque<Job> queue_;
        std::unordered_map<std::string, bool> queued_;
        std::thread thread_;
        std::atomic<bool> stop_{ false };
        std::uint64_t frame_ = 0;
    };

    // Exposed for tests: builds a smaller DDS from the tail of a mipmapped one.
    std::optional<std::vector<std::uint8_t>> ShrinkDds(const std::vector<std::uint8_t>& header, int maxSize, std::size_t* skipBytes, std::size_t* keepBytes);
}
