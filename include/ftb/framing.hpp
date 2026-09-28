#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <filesystem>

namespace ftb {

    class MappedFile {
    public:
        explicit MappedFile(const std::filesystem::path& path);
        ~MappedFile();

        MappedFile(const MappedFile&) = delete;
        MappedFile& operator=(const MappedFile&) = delete;
        MappedFile(MappedFile&& other) noexcept;
        MappedFile& operator=(MappedFile&& other) noexcept;

        std::span<const std::uint8_t> bytes() const noexcept { return {data_, size_}; }

    private:
        void release() noexcept;
        
        const std::uint8_t* data_ = nullptr;
        std::size_t size_ = 0;
    };

    template <class F>
    std::size_t frame(std::span<const std::uint8_t> buf, F&& on_payload) {
        std::size_t pos{0};
        
        while (buf.size() - pos >= 2) {
            auto& big = buf[pos];
            auto& small = buf[pos+1];
            std::size_t size = static_cast<std::size_t>(big << 8) + small;
            if (buf.size() - pos < size + 2) return pos;
            on_payload(buf.subspan(pos+2, size));
            pos += size + 2;
        }
        return pos;
    }
}
