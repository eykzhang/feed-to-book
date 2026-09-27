#include "ftb/framing.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;

const fs::path kTestData = FTB_TEST_DATA_DIR;
const fs::path kRepoData = fs::path(FTB_TEST_DATA_DIR).parent_path().parent_path() / "data";

Bytes read_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct Result {
  std::size_t consumed;
  std::vector<Bytes> payloads;
};

Result run(std::span<const std::uint8_t> buf) {
  Result r{};
  r.consumed = ftb::frame(buf, [&](std::span<const std::uint8_t> payload) {
    r.payloads.emplace_back(payload.begin(), payload.end());
  });
  return r;
}

// Message types in file order, from the `msg` lines of a reference file
// written by tools/itch_ref.py with --every 1.
std::string reference_types(const fs::path& ref) {
  std::ifstream in(ref);
  std::string line, types;
  while (std::getline(in, line)) {
    std::istringstream fields(line);
    std::string tag, index, type;
    if (fields >> tag >> index >> type && tag == "msg") types += type;
  }
  return types;
}

// Hand-built buffers: return value, payload count, and payload contents.

TEST(Frame, EmptyBuffer) {
  Result r = run({});
  EXPECT_EQ(r.consumed, 0u);
  EXPECT_TRUE(r.payloads.empty());
}

TEST(Frame, OneByteIsNotAPrefix) {
  Bytes b{0x00};
  Result r = run(b);
  EXPECT_EQ(r.consumed, 0u);
  EXPECT_TRUE(r.payloads.empty());
}

TEST(Frame, PayloadShorterThanPrefixSays) {
  Bytes b{0x00, 0x03, 0xAA, 0xBB};
  Result r = run(b);
  EXPECT_EQ(r.consumed, 0u);
  EXPECT_TRUE(r.payloads.empty());
}

TEST(Frame, FrameEndingExactlyAtBufferEnd) {
  Bytes b{0x00, 0x02, 0xAA, 0xBB};
  Result r = run(b);
  EXPECT_EQ(r.consumed, 4u);
  ASSERT_EQ(r.payloads.size(), 1u);
  EXPECT_EQ(r.payloads[0], (Bytes{0xAA, 0xBB}));
}

TEST(Frame, ZeroLengthFrameIsPassedThrough) {
  Bytes b{0x00, 0x00};
  Result r = run(b);
  EXPECT_EQ(r.consumed, 2u);
  ASSERT_EQ(r.payloads.size(), 1u);
  EXPECT_TRUE(r.payloads[0].empty());
}

TEST(Frame, LengthIsBigEndian) {
  // 0x0102 = 258 bytes. Read little-endian it would be 0x0201 = 513, past the end.
  Bytes b{0x01, 0x02};
  b.resize(2 + 258, 0x5A);
  Result r = run(b);
  EXPECT_EQ(r.consumed, b.size());
  ASSERT_EQ(r.payloads.size(), 1u);
  EXPECT_EQ(r.payloads[0].size(), 258u);
}

TEST(Frame, StopsAtPartialSecondFrame) {
  Bytes b{0x00, 0x01, 0xAA, 0x00, 0x05, 0xBB};
  Result r = run(b);
  EXPECT_EQ(r.consumed, 3u);  // start of the partial frame, not past its prefix
  ASSERT_EQ(r.payloads.size(), 1u);
  EXPECT_EQ(r.payloads[0], (Bytes{0xAA}));
}

// The synthetic all-types fixture, tests/data/all_types.itch.

TEST(Frame, FixtureFramesMatchReference) {
  Bytes b = read_file(kTestData / "all_types.itch");
  ASSERT_EQ(b.size(), 899u) << "fixture missing or changed; see tools/make_fixture.py";
  std::string want = reference_types(kTestData / "all_types.ref");
  ASSERT_EQ(want.size(), 27u);

  Result r = run(b);
  EXPECT_EQ(r.consumed, b.size());
  ASSERT_EQ(r.payloads.size(), want.size());
  for (std::size_t i = 0; i < want.size(); ++i) {
    ASSERT_FALSE(r.payloads[i].empty()) << "payload " << i;
    EXPECT_EQ(static_cast<char>(r.payloads[i][0]), want[i]) << "payload " << i;
  }
}

// Cutting the fixture at every byte covers truncated files and, later, chunk
// boundaries in a stream: the framer must return the last frame boundary at or
// before the cut and hand out exactly the frames that end by then.
TEST(Frame, FixtureCutAtEveryByte) {
  Bytes b = read_file(kTestData / "all_types.itch");
  ASSERT_EQ(b.size(), 899u);

  std::vector<std::size_t> ends;  // offset just past each frame
  for (std::size_t pos = 0; pos < b.size();) {
    pos += 2 + (static_cast<std::size_t>(b[pos]) << 8 | b[pos + 1]);
    ends.push_back(pos);
  }
  ASSERT_EQ(ends.back(), b.size());

  for (std::size_t cut = 0; cut <= b.size(); ++cut) {
    std::size_t complete = 0, boundary = 0;
    for (std::size_t e : ends) {
      if (e > cut) break;
      ++complete;
      boundary = e;
    }
    Result r = run(std::span<const std::uint8_t>(b).first(cut));
    ASSERT_EQ(r.consumed, boundary) << "cut at " << cut;
    ASSERT_EQ(r.payloads.size(), complete) << "cut at " << cut;
  }
}

// Real data. data/ is local only, so these skip in CI.

TEST(Frame, Slice100k) {
  const fs::path slice = kRepoData / "slices" / "01302019-first100000.itch";
  if (!fs::exists(slice)) GTEST_SKIP() << slice << " not present";
  Bytes b = read_file(slice);
  ASSERT_EQ(b.size(), 2852306u);  // data/manifest.json

  std::size_t count = 0;
  std::size_t consumed = ftb::frame(b, [&](std::span<const std::uint8_t>) { ++count; });
  EXPECT_EQ(consumed, b.size());
  EXPECT_EQ(count, 100000u);
}

}  // namespace
