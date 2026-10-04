#include "ftb/itch.hpp"
#include "ftb/framing.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

const fs::path kTestData = FTB_TEST_DATA_DIR;

// Distinct bytes catch a missed swap or a wrong shift; a leading 0x80+ byte
// catches sign extension from int promotion.
constexpr std::array<std::uint8_t, 8> kAscending{0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
constexpr std::array<std::uint8_t, 8> kHighBit{0x80, 0x91, 0xA2, 0xB3, 0xC4, 0xD5, 0xE6, 0xF7};
constexpr std::array<std::uint8_t, 8> kOnes{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

TEST(LoadBe, Be16) {
  EXPECT_EQ(ftb::load_be16(kAscending.data()), 0x0123u);
  EXPECT_EQ(ftb::load_be16(kHighBit.data()), 0x8091u);
  EXPECT_EQ(ftb::load_be16(kOnes.data()), 0xFFFFu);
}

TEST(LoadBe, Be32) {
  EXPECT_EQ(ftb::load_be32(kAscending.data()), 0x01234567u);
  EXPECT_EQ(ftb::load_be32(kHighBit.data()), 0x8091A2B3u);
  EXPECT_EQ(ftb::load_be32(kOnes.data()), 0xFFFFFFFFu);
}

TEST(LoadBe, Be48) {
  EXPECT_EQ(ftb::load_be48(kAscending.data()), 0x0123456789ABull);
  EXPECT_EQ(ftb::load_be48(kHighBit.data()), 0x8091A2B3C4D5ull);
  EXPECT_EQ(ftb::load_be48(kOnes.data()), 0xFFFFFFFFFFFFull);
}

// The last representable ITCH timestamp, 23:59:59.999999999, fits in 47 bits.
TEST(LoadBe, Be48EndOfDay) {
  constexpr std::uint64_t ns = 86'399'999'999'999ull;
  const std::array<std::uint8_t, 6> bytes{
      static_cast<std::uint8_t>(ns >> 40), static_cast<std::uint8_t>(ns >> 32),
      static_cast<std::uint8_t>(ns >> 24), static_cast<std::uint8_t>(ns >> 16),
      static_cast<std::uint8_t>(ns >> 8),  static_cast<std::uint8_t>(ns)};
  EXPECT_EQ(ftb::load_be48(bytes.data()), ns);
}

TEST(LoadBe, Be64) {
  EXPECT_EQ(ftb::load_be64(kAscending.data()), 0x0123456789ABCDEFull);
  EXPECT_EQ(ftb::load_be64(kHighBit.data()), 0x8091A2B3C4D5E6F7ull);
  EXPECT_EQ(ftb::load_be64(kOnes.data()), 0xFFFFFFFFFFFFFFFFull);
}

// Reads only the field's own bytes: a 6-byte buffer at the end of an
// allocation would trip ASan if load_be48 touched a seventh byte.
TEST(LoadBe, Be48ReadsSixBytes) {
  const auto* bytes = new std::uint8_t[6]{0x00, 0x00, 0x00, 0x00, 0x00, 0x2A};
  EXPECT_EQ(ftb::load_be48(bytes), 42u);
  delete[] bytes;
}

// Parse tests take their bytes from the all-types fixture and their expected
// values from all_types.ref, so offsets are checked against tools/itch_ref.py.
class FixtureMessages : public ::testing::Test {
 protected:
  void SetUp() override {
    ftb::frame(file_.bytes(), [&](std::span<const std::uint8_t> p) { payloads_.push_back(p); });
  }

  // The first payload whose type byte is `type`.
  std::span<const std::uint8_t> first(char type) const {
    for (auto p : payloads_) {
      if (!p.empty() && p[0] == static_cast<std::uint8_t>(type)) return p;
    }
    ADD_FAILURE() << "fixture has no message of type " << type;
    return {};
  }

  ftb::MappedFile file_{kTestData / "all_types.itch"};
  std::vector<std::span<const std::uint8_t>> payloads_;
};

TEST_F(FixtureMessages, AddOrder) {
  auto p = first('A');
  ASSERT_EQ(p.size(), ftb::AddOrder::wire_size);
  const auto m = ftb::AddOrder::parse(p.data());
  EXPECT_EQ(m.header.stock_locate, 4370u);
  EXPECT_EQ(m.header.tracking_number, 6169u);
  EXPECT_EQ(m.header.timestamp, 34222855299876ull);
  EXPECT_EQ(m.order_ref, 2749210254799219757ull);
  EXPECT_EQ(m.side, 'Q');
  EXPECT_EQ(m.shares, 875902519u);
  EXPECT_EQ(std::string_view(m.stock.data(), m.stock.size()), "ZVZZT   ");
  EXPECT_EQ(m.price, 1111704645u);
}

}  // namespace
