#include "memodb/Evaluator.h"

#include <cassert>
#include <chrono>
#include <functional>
#include <future>
#include <thread>
#include <utility>

#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/raw_ostream.h>

using namespace memodb;

NodeRef &Future::get() {
  // We need to return a non-const reference so NodeRef::operator*() and
  // NodeRef::getCID() will work. The const_cast is safe because the
  // shared_future's state is only used in two places (this Future, and
  // Evaluator::workerThreadImpl) and only this Future will actually access the
  // NodeRef.
  return const_cast<NodeRef &>(future.get());
}

void Future::wait() { future.wait(); }

bool Future::checkForResult() const {
  return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

const Node &Future::operator*() { return *get(); }

const Node *Future::operator->() { return &operator*(); }

const CID &Future::getCID() { return get().getCID(); }

Future::Future(std::shared_future<NodeRef> &&future)
    : future(std::move(future)) {}

Evaluator::Evaluator(std::unique_ptr<Store> store, unsigned num_threads)
    : store(std::move(store)) {
  threads.reserve(num_threads);
  for (unsigned i = 0; i < num_threads; ++i)
    threads.emplace_back(&Evaluator::workerThreadImpl, this);
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
  auto result = NodeRef(getStore(), func_iter->getValue()(*this, call));
  getStore().set(call, result.getCID());
  return result;
}

Future Evaluator::evaluateAsync(const Call &call) {
  num_queued++;
  std::shared_future<NodeRef> future = std::async(
      std::launch::deferred, &Evaluator::evaluateDeferred, this, call);

  if (!threads.empty()) {
    {
      std::lock_guard<std::mutex> lock(work_mutex);
      work_queue.emplace(future);
    }
    work_cv.notify_one();
  }
  return Future(std::move(future));
}

void Evaluator::registerFunc(
    llvm::StringRef name,
    std::function<NodeOrCID(Evaluator &, const Call &)> func) {
  assert(!funcs.count(name) && "duplicate func");
  funcs[name] = std::move(func);
}

void Evaluator::workerThreadImpl() {
  while (true) {
    std::unique_lock<std::mutex> lock(work_mutex);
    work_cv.wait(lock, [this] { return work_done || !work_queue.empty(); });
    if (work_done)
      break;
    std::shared_future<NodeRef> future = std::move(work_queue.front());
    work_queue.pop();
    lock.unlock();
    future.get();
  }
}

NodeRef Evaluator::evaluateDeferred(const Call &call) {
  num_started++;

  // Use try_to_lock so that printing to stderr doesn't become a bottleneck. If
  // there are multiple threads, messages may be skipped, but if the thread
  // pool is empty and Evaluator is only used by one thread, all messages will
  // be printed.
  if (auto stderr_lock =
          std::unique_lock<std::mutex>(stderr_mutex, std::try_to_lock)) {
    printProgress();
    llvm::errs() << " starting " << call << "\n";
    stderr_lock.unlock();
  }

  auto result = evaluate(call);
  num_finished++;

  if (auto stderr_lock =
          std::unique_lock<std::mutex>(stderr_mutex, std::try_to_lock)) {
    printProgress();
    llvm::errs() << " finished " << call << "\n";
    stderr_lock.unlock();
  }

  return result;
}

void Evaluator::printProgress() {
  // Load atomics in this order to avoid getting negative values.
  unsigned finished = num_finished;
  unsigned started = num_started;
  unsigned queued = num_queued;
  llvm::errs() << (queued - started) << " -> " << (started - finished) << " -> "
               << finished;
}
