#include "memodb/Evaluator.h"

#include <chrono>
#include <future>
#include <memory>

#include "MockStore.h"
#include "memodb/CID.h"
#include "memodb/Node.h"
#include "gtest/gtest.h"

using namespace memodb;
using ::testing::Return;

namespace {

NodeOrCID nullary(Evaluator &) { return Node("nullary"); }

NodeOrCID unary(Evaluator &, Link arg) {
  return Node(node_map_arg, {{"unary", *arg}});
}

NodeOrCID binary(Evaluator &, Link arg0, Link arg1) {
  return Node(arg0->as<int>() - arg1->as<int>());
}

TEST(EvaluatorTest, Nullary) {
  const Name name(Call("nullary", {}));
  const CID cid = *CID::parse("uAXEACGdudWxsYXJ5");
  const llvm::Optional<CID> no_cid;

  auto store = std::make_unique<MockStore>();
  EXPECT_CALL(*store, resolveOptional(name)).WillOnce(Return(no_cid));
  EXPECT_CALL(*store, put(Node("nullary"))).WillOnce(Return(cid));
  EXPECT_CALL(*store, set(name, cid));
  auto evaluator = Evaluator::createLocal(std::move(store));
  evaluator->registerFunc("nullary", nullary);
  EXPECT_EQ(cid, evaluator->evaluate("nullary").getCID());
}

TEST(EvaluatorTest, NullaryCached) {
  const Name name(Call("nullary", {}));
  const CID cid = *CID::parse("uAXEACGdudWxsYXJ5");
  const llvm::Optional<CID> no_cid;

  auto store = std::make_unique<MockStore>();
  EXPECT_CALL(*store, resolveOptional(name)).WillOnce(Return(cid));
  EXPECT_CALL(*store, put).Times(0);
  EXPECT_CALL(*store, set).Times(0);
  auto evaluator = Evaluator::createLocal(std::move(store));
  evaluator->registerFunc("nullary", nullary);
  EXPECT_EQ(cid, evaluator->evaluate("nullary").getCID());
}

TEST(EvaluatorTest, Unary) {
  const Node arg0 = "test";
  Node result_node(node_map_arg, {{"unary", "test"}});
  const CID arg0_cid = *CID::parse("uAXEABWR0ZXN0");
  const CID result_cid = *CID::parse("uAXEADKFldW5hcnlkdGVzdA");
  const Name name(Call("unary", {arg0_cid}));
  const llvm::Optional<CID> no_cid;

  auto store = std::make_unique<MockStore>();
  EXPECT_CALL(*store, resolveOptional(name)).WillOnce(Return(no_cid));
  EXPECT_CALL(*store, getOptional(arg0_cid)).WillOnce(Return(arg0));
  EXPECT_CALL(*store, put(result_node)).WillOnce(Return(result_cid));
  EXPECT_CALL(*store, set(name, result_cid));
  auto evaluator = Evaluator::createLocal(std::move(store));
  evaluator->registerFunc("unary", unary);
  EXPECT_EQ(result_cid, evaluator->evaluate("unary", arg0_cid).getCID());
}

TEST(EvaluatorTest, Binary) {
  const Node arg0 = 5, arg1 = 3, result_node = 2;
  const CID arg0_cid = *CID::parse("uAXEAAQU");
  const CID arg1_cid = *CID::parse("uAXEAAQM");
  const CID result_cid = *CID::parse("uAXEAAQI");
  const Name name(Call("binary", {arg0_cid, arg1_cid}));
  const llvm::Optional<CID> no_cid;

  auto store = std::make_unique<MockStore>();
  EXPECT_CALL(*store, resolveOptional(name)).WillOnce(Return(no_cid));
  EXPECT_CALL(*store, getOptional(arg0_cid)).WillOnce(Return(arg0));
  EXPECT_CALL(*store, getOptional(arg1_cid)).WillOnce(Return(arg1));
  EXPECT_CALL(*store, put(result_node)).WillOnce(Return(result_cid));
  EXPECT_CALL(*store, set(name, result_cid));
  auto evaluator = Evaluator::createLocal(std::move(store));
  evaluator->registerFunc("binary", binary);
  EXPECT_EQ(result_cid,
            evaluator->evaluate("binary", arg0_cid, arg1_cid).getCID());
}

TEST(EvaluatorTest, Async) {
  const Node arg0 = 5, arg1 = 3, result_node = 2;
  const CID arg0_cid = *CID::parse("uAXEAAQU");
  const CID arg1_cid = *CID::parse("uAXEAAQM");
  const CID result_cid = *CID::parse("uAXEAAQI");
  Call call("binary", {arg0_cid, arg1_cid});
  const Name name(call);
  const llvm::Optional<CID> no_cid;

  auto store = std::make_unique<MockStore>();
  EXPECT_CALL(*store, resolveOptional(name)).WillOnce(Return(no_cid));
  EXPECT_CALL(*store, getOptional(arg0_cid)).WillOnce(Return(arg0));
  EXPECT_CALL(*store, getOptional(arg1_cid)).WillOnce(Return(arg1));
  EXPECT_CALL(*store, put(result_node)).WillOnce(Return(result_cid));
  EXPECT_CALL(*store, set(name, result_cid));
  auto evaluator = Evaluator::createLocal(std::move(store));
  evaluator->registerFunc("binary", binary);
  auto future = evaluator->evaluateAsync(call);
  // Make sure the Evaluator stores a copy of the Call, not a reference to it.
  call.Name = "invalid";
  EXPECT_EQ(result_cid, future.getCID());
}

TEST(EvaluatorTest, ThreadPool) {
  const Node arg0 = 5, arg1 = 3, result_node = 2;
  const CID arg0_cid = *CID::parse("uAXEAAQU");
  const CID arg1_cid = *CID::parse("uAXEAAQM");
  const CID result_cid = *CID::parse("uAXEAAQI");
  Call call("binary", {arg0_cid, arg1_cid});
  const Name name(call);
  const llvm::Optional<CID> no_cid;

  auto store = std::make_unique<MockStore>();
  EXPECT_CALL(*store, resolveOptional(name)).WillOnce(Return(no_cid));
  EXPECT_CALL(*store, getOptional(arg0_cid)).WillOnce(Return(arg0));
  EXPECT_CALL(*store, getOptional(arg1_cid)).WillOnce(Return(arg1));
  EXPECT_CALL(*store, put(result_node)).WillOnce(Return(result_cid));
  EXPECT_CALL(*store, set(name, result_cid));
  auto evaluator = Evaluator::createLocal(std::move(store), 1);
  evaluator->registerFunc("binary", binary);
  auto future = evaluator->evaluateAsync("binary", arg0_cid, arg1_cid);
  for (unsigned i = 0; i < 100; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (future.checkForResult())
      break;
  }
  EXPECT_TRUE(future.checkForResult());
  EXPECT_EQ(result_cid, future.getCID());
}

} // end anonymous namespace
