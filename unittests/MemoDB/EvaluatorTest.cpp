#include "memodb/Evaluator.h"

#include <chrono>
#include <future>

#include "memodb/CID.h"
#include "memodb/Node.h"
#include "memodb/Store.h"
#include "gtest/gtest.h"

using namespace memodb;

namespace {

Node nullary(Evaluator &) { return "nullary"; }

Node unary(Evaluator &, const Node &arg) {
  return Node(node_map_arg, {{"unary", arg}});
}

Node binary(Evaluator &, const Node &arg0, const Node &arg1) {
  return arg0.as<int>() - arg1.as<int>();
}

TEST(EvaluatorTest, Nullary) {
  Evaluator evaluator(Store::open("sqlite:test?mode=memory", true));
  evaluator.registerFunc("nullary", nullary);
  EXPECT_EQ(Node("nullary"), *evaluator.evaluate("nullary"));
}

TEST(EvaluatorTest, Unary) {
  Evaluator evaluator(Store::open("sqlite:test?mode=memory", true));
  evaluator.registerFunc("unary", unary);
  CID test = evaluator.getStore().put("test");
  EXPECT_EQ(Node(node_map_arg, {{"unary", "test"}}),
            *evaluator.evaluate("unary", test));
}

// Note that each test uses different arguments to binary(), to make sure calls
// are always missing from the cache.

TEST(EvaluatorTest, Binary) {
  Evaluator evaluator(Store::open("sqlite:test?mode=memory", true));
  evaluator.registerFunc("binary", binary);
  CID five = evaluator.getStore().put(5);
  CID three = evaluator.getStore().put(3);
  EXPECT_EQ(Node(2), *evaluator.evaluate("binary", five, three));
}

TEST(EvaluatorTest, Async) {
  Evaluator evaluator(Store::open("sqlite:test?mode=memory", true));
  evaluator.registerFunc("binary", binary);
  CID five = evaluator.getStore().put(5);
  CID three = evaluator.getStore().put(3);
  Call call("binary", {three, five});
  auto future = evaluator.evaluateAsync(call);
  // Make sure the Evaluator stores a copy of the Call, not a reference to it.
  call.Name = "invalid";
  EXPECT_EQ(Node(-2), *future.get());
}

TEST(EvaluatorTest, ThreadPool) {
  Evaluator evaluator(Store::open("sqlite:test?mode=memory", true), 1);
  evaluator.registerFunc("binary", binary);
  CID four = evaluator.getStore().put(4);
  auto future = evaluator.evaluateAsync("binary", four, four);
  for (unsigned i = 0; i < 100; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (future.checkForResult())
      break;
  }
  EXPECT_TRUE(future.checkForResult());
  EXPECT_EQ(Node(0), *future.get());
}

} // end anonymous namespace
