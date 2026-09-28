#include "ftb/framing.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
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

// Total message count and per-type counts from the header of a reference
// file written by tools/itch_ref.py, indexed by type byte.
struct RefCounts {
  std::size_t messages = 0;
  std::array<std::size_t, 256> by_type{};
};

RefCounts reference_counts(const fs::path& ref) {
  std::ifstream in(ref);
  RefCounts c;
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream fields(line);
    std::string tag;
    fields >> tag;
    if (tag == "messages") {
      fields >> c.messages;
    } else if (tag == "count") {
      char type = 0;
      std::size_t n = 0;
      fields >> type >> n;
      c.by_type[static_cast<unsigned char>(type)] = n;
    } else {
      break;  // counts come first; the rest is sampled messages
    }
  }
  return c;
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

// MappedFile.

TEST(MappedFile, BytesMatchFileContents) {
  const fs::path path = kTestData / "all_types.itch";
  ftb::MappedFile f(path);
  Bytes want = read_file(path);
  ASSERT_EQ(f.bytes().size(), want.size());
  EXPECT_TRUE(std::equal(want.begin(), want.end(), f.bytes().begin()));
}

TEST(MappedFile, MissingFileThrows) {
  EXPECT_THROW(ftb::MappedFile(kTestData / "does_not_exist.itch"), std::system_error);
}

TEST(MappedFile, MissingFileMessageNamesCallAndPath) {
  try {
    ftb::MappedFile f(kTestData / "does_not_exist.itch");
    FAIL() << "expected std::system_error";
  } catch (const std::system_error& e) {
    EXPECT_EQ(e.code(), std::errc::no_such_file_or_directory);
    std::string what = e.what();
    EXPECT_NE(what.find("open"), std::string::npos) << what;
    EXPECT_NE(what.find("does_not_exist.itch"), std::string::npos) << what;
  }
}

TEST(MappedFile, EmptyFileGivesEmptyBytes) {
  const fs::path path = fs::temp_directory_path() / "ftb_empty_file_test.itch";
  { std::ofstream create(path, std::ios::binary | std::ios::trunc); }
  ASSERT_TRUE(fs::exists(path));
  ASSERT_EQ(fs::file_size(path), 0u);

  {
    ftb::MappedFile f(path);
    EXPECT_TRUE(f.bytes().empty());
  }
  fs::remove(path);
}

TEST(MappedFile, MoveConstructorTransfersMapping) {
  ftb::MappedFile a(kTestData / "all_types.itch");
  const std::uint8_t* data = a.bytes().data();

  ftb::MappedFile b(std::move(a));
  EXPECT_EQ(b.bytes().data(), data);
  EXPECT_EQ(b.bytes().size(), 899u);
  EXPECT_TRUE(a.bytes().empty());  // NOLINT(bugprone-use-after-move): checking the moved-from state
}

TEST(MappedFile, MoveAssignmentTransfersMapping) {
  ftb::MappedFile a(kTestData / "all_types.itch");
  ftb::MappedFile b(kTestData / "all_types.ref");
  const std::uint8_t* data = a.bytes().data();

  b = std::move(a);  // b's old mapping is released here
  EXPECT_EQ(b.bytes().data(), data);
  EXPECT_EQ(b.bytes().size(), 899u);
  EXPECT_TRUE(a.bytes().empty());  // NOLINT(bugprone-use-after-move)
}

TEST(MappedFile, MoveAssignmentIntoEmpty) {
  ftb::MappedFile a(kTestData / "all_types.itch");
  ftb::MappedFile b(kTestData / "all_types.itch");
  ftb::MappedFile c(std::move(b));  // b is now empty

  b = std::move(a);  // nothing to release in b
  EXPECT_EQ(b.bytes().size(), 899u);
}

// Real data. data/ is local only, so these skip in CI.

// Frames a whole slice and checks the total and per-type counts against the
// reference decoder. Per-type counts only read each payload's first byte, but
// a framing error of even one byte would scramble them.
void check_slice(const fs::path& slice, const fs::path& ref, std::size_t bytes) {
  ftb::MappedFile f(slice);
  ASSERT_EQ(f.bytes().size(), bytes);  // data/manifest.json

  RefCounts want = reference_counts(ref);
  ASSERT_GT(want.messages, 0u) << ref << " missing or empty";

  RefCounts got;
  std::size_t consumed = ftb::frame(f.bytes(), [&](std::span<const std::uint8_t> payload) {
    ++got.messages;
    if (!payload.empty()) ++got.by_type[payload[0]];
  });
  EXPECT_EQ(consumed, f.bytes().size());
  EXPECT_EQ(got.messages, want.messages);
  for (std::size_t t = 0; t < want.by_type.size(); ++t) {
    EXPECT_EQ(got.by_type[t], want.by_type[t]) << "type '" << static_cast<char>(t) << "'";
  }
}

TEST(Frame, Slice100k) {
  const fs::path slice = kRepoData / "slices" / "01302019-first100000.itch";
  if (!fs::exists(slice)) GTEST_SKIP() << slice << " not present";
  check_slice(slice, kRepoData / "ref" / "01302019-first100000.every1000.ref", 2852306u);
}

TEST(Frame, SliceUntil10am) {
  const fs::path slice = kRepoData / "slices" / "01302019-until100000.itch";
  if (!fs::exists(slice)) GTEST_SKIP() << slice << " not present";
  check_slice(slice, kRepoData / "ref" / "01302019-until100000.every1000.ref", 1490615266u);
}

}  // namespace
