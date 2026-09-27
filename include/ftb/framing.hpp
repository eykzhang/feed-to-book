#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace ftb {

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
