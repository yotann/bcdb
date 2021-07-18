#include "memodb/Evaluator.h"

#include <cassert>
#include <functional>
#include <future>
#include <utility>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>

using namespace memodb;

Evaluator::Evaluator(std::unique_ptr<Store> store, unsigned num_threads)
    : store(std::move(store)) {
  threads.reserve(num_threads);
  for (unsigned i = 0; i < num_threads; ++i)
    threads.emplace_back(&Evaluator::threadImpl, this);
}

Evaluator::~Evaluator() {
  {
    std::lock_guard<std::mutex> lock(work_mutex);
    work_done = true;
  }
  work_cv.notify_all();
  for (auto &thread : threads)
    thread.join();
}

NodeRef Evaluator::evaluate(const Call &call) {
  auto cid_or_null = getStore().resolveOptional(call);
  if (cid_or_null)
    return NodeRef(getStore(), *cid_or_null);
  const auto func_iter = funcs.find(call.Name);
  if (func_iter == funcs.end())
    llvm::report_fatal_error("No implementation of " + call.Name +
                             " available");
  auto result = func_iter->getValue()(*this, call);
  auto cid = getStore().put(result);
  getStore().call_set(call.Name, call.Args, cid);
  return NodeRef(getStore(), std::move(cid), std::move(result));
}

std::shared_future<NodeRef> Evaluator::evaluateAsync(const Call &call) {
  // Select a specific overload of evaluate().
  NodeRef (Evaluator::*evaluate)(const Call &) = &Evaluator::evaluate;
  std::shared_future<NodeRef> future =
      std::async(std::launch::deferred, evaluate, this, call);

  if (!threads.empty()) {
    {
      std::lock_guard<std::mutex> lock(work_mutex);
      work_queue.emplace(future);
    }
    work_cv.notify_one();
  }
  return future;
}

void Evaluator::registerFunc(
    llvm::StringRef name, std::function<Node(Evaluator &, const Call &)> func) {
  assert(!funcs.count(name) && "duplicate func");
  funcs[name] = std::move(func);
}

void Evaluator::threadImpl() {
  while (true) {
    std::unique_lock<std::mutex> lock(work_mutex);
    work_cv.wait(lock);
    if (work_done)
      break;
    std::shared_future<NodeRef> future = std::move(work_queue.front());
    lock.unlock();
    future.get();
  }
}
