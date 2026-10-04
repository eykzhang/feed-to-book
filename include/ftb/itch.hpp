#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace ftb {
    inline std::uint16_t load_be16(const std::uint8_t* p) {
        std::uint16_t ret = 0;
        ret |= *p << 8;
        ret |= *(p + 1);
        return ret;
    }
    inline std::uint32_t load_be32(const std::uint8_t* p) {
        std::uint32_t ret = 0;
        for (int i {0}; i < 4; i++) ret |= static_cast<std::uint32_t>(p[i]) << (24 - (8 * i));
        return ret;
    }
    inline std::uint64_t load_be48(const std::uint8_t* p) {
        std::uint64_t ret = 0;
        for (int i {0}; i < 6; i++) ret |= static_cast<std::uint64_t>(p[i]) << (40 - (8 * i));
        return ret;
    }
    inline std::uint64_t load_be64(const std::uint8_t* p) {
        std::uint64_t ret = 0;
        for (int i {0}; i < 8; i++) ret |= static_cast<std::uint64_t>(p[i]) << (56 - (8 * i));
        return ret;
    }

    struct MessageHeader {
        std::uint16_t stock_locate;
        std::uint16_t tracking_number;
        std::uint64_t timestamp;

        static MessageHeader parse(const std::uint8_t* p) {
            MessageHeader ret;
            ret.stock_locate = load_be16(p+1);
            ret.tracking_number = load_be16(p+3);
            ret.timestamp = load_be48(p+5);
            return ret;
        }
    };

    struct AddOrder {
        MessageHeader header;
        std::uint64_t order_ref;
        char side;
        std::uint32_t shares;
        std::array<char, 8> stock;
        std::uint32_t price;
        static constexpr char type = 'A';
        static constexpr std::size_t wire_size = 36;

        static AddOrder parse(const std::uint8_t* p) {
            AddOrder ret;
            ret.header = MessageHeader::parse(p);
            ret.order_ref = load_be64(p+11);
            ret.side = static_cast<char>(p[19]);
            ret.shares = load_be32(p+20);
            std::memcpy(&ret.stock, p+24, 8);
            ret.price = load_be32(p+32);
            return ret;
        }
    };
}
