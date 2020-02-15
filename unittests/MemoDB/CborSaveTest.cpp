#include "memodb/memodb.h"

#include "gtest/gtest.h"

namespace {

TEST(CborSaveTest, Undefined) {
  memodb_value value;
  std::vector<uint8_t> out;
  value.save_cbor(out);
  EXPECT_EQ(std::vector<uint8_t>{0xf7}, out);
}

} // end anonymous namespace
