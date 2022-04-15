#include "memodb/Evaluator.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Threading.h>
#include <llvm/Support/raw_ostream.h>

#include "memodb/ToolSupport.h"
#include "memodb_internal.h"

using namespace memodb;
using llvm::Twine;

const Link &Future::get() {
  // The shared_future's state will be accessed from two places: this Future,
  // and Evaluator::workerThreadImpl. Only this Future will actually access the
  // Link, so there's no race condition on the fields of the Link.
  return future.get();
}

void Future::wait() { future.wait(); }

bool Future::checkForResult() const {
  return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

const Node &Future::operator*() { return *get(); }

const Node *Future::operator->() { return &operator*(); }

const CID &Future::getCID() { return get().getCID(); }

void Future::freeNode() { get().freeNode(); }

Future::Future(std::shared_future<Link> &&future) : future(std::move(future)) {}

namespace {
class ThreadPoolEvaluator : public Evaluator {
public:
  ThreadPoolEvaluator(std::unique_ptr<Store> store, unsigned num_threads = 0);
  ~ThreadPoolEvaluator() override;
  Store &getStore() override;
  Link evaluate(const Call &call, bool work_while_waiting = true) override;
  Future evaluateAsync(const Call &call,
                       bool work_while_waiting = true) override;
  void registerFunc(
      llvm::StringRef name,
      std::function<NodeOrCID(Evaluator &, const Call &)> func) override;

private:
  std::unique_ptr<Store> store;
  llvm::StringMap<std::function<NodeOrCID(Evaluator &, const Call &)>> funcs;

  std::vector<std::thread> threads;
  std::mutex work_mutex;
  std::condition_variable work_cv;
  std::queue<std::shared_future<Link>> work_queue;
  bool work_done = false;

  // These counters only increase, never decrease.
  std::atomic<unsigned> num_queued = 0, num_started = 0, num_finished = 0;
  std::mutex stderr_mutex;

  void workerThreadImpl();

  Link evaluateDeferred(const Call &call);

  void printProgress();
};
} // end anonymous namespace

ThreadPoolEvaluator::ThreadPoolEvaluator(std::unique_ptr<Store> store,
                                         unsigned num_threads)
    : store(std::move(store)) {
  threads.reserve(num_threads);
  for (unsigned i = 0; i < num_threads; ++i)
    threads.emplace_back(&ThreadPoolEvaluator::workerThreadImpl, this);
}

ThreadPoolEvaluator::~ThreadPoolEvaluator() {
  {
    std::lock_guard<std::mutex> lock(work_mutex);
    work_done = true;
  }
  work_cv.notify_all();
  for (auto &thread : threads)
    thread.join();
}

Store &ThreadPoolEvaluator::getStore() { return *store; }

Link ThreadPoolEvaluator::evaluate(const Call &call, bool work_while_waiting) {
  auto cid_or_null = getStore().resolveOptional(call);
  if (cid_or_null)
    return Link(getStore(), *cid_or_null);
  const auto func_iter = funcs.find(call.Name);
  if (func_iter == funcs.end())
    llvm::report_fatal_error("No implementation of " + Twine(call.Name) +
                             " available");
  PrettyStackTraceCall pretty_stack_trace(call);
  auto result = Link(getStore(), func_iter->getValue()(*this, call));
  getStore().set(call, result.getCID());
  return result;
}

Future ThreadPoolEvaluator::evaluateAsync(const Call &call,
                                          bool work_while_waiting) {
  num_queued++;
  std::shared_future<Link> future =
      std::async(std::launch::deferred, &ThreadPoolEvaluator::evaluateDeferred,
                 this, call);

  if (!threads.empty()) {
    {
      std::lock_guard<std::mutex> lock(work_mutex);
      work_queue.emplace(future);
    }
    work_cv.notify_one();
  }
  return makeFuture(std::move(future));
}

void ThreadPoolEvaluator::registerFunc(
    llvm::StringRef name,
    std::function<NodeOrCID(Evaluator &, const Call &)> func) {
  assert(!funcs.count(name) && "duplicate func");
  funcs[name] = std::move(func);
}

void ThreadPoolEvaluator::workerThreadImpl() {
  while (true) {
    std::unique_lock<std::mutex> lock(work_mutex);
    work_cv.wait(lock, [this] { return work_done || !work_queue.empty(); });
    if (work_done)
      break;
    std::shared_future<Link> future = std::move(work_queue.front());
    work_queue.pop();
    lock.unlock();
    future.get();
  }
}

Link ThreadPoolEvaluator::evaluateDeferred(const Call &call) {
  llvm::PrettyStackTraceString stack_printer("Worker thread (single process)");

  num_started++;

  // Use try_to_lock so that printing to stderr doesn't become a bottleneck. If
  // there are multiple threads, messages may be skipped, but if the thread
  // pool is empty and Evaluator is only used by one thread, all messages will
  // be printed.
  if (auto stderr_lock =
          std::unique_lock<std::mutex>(stderr_mutex, std::try_to_lock)) {
    printProgress();
    llvm::errs() << " starting " << call << "\n";
  }

  auto result = evaluate(call);
  num_finished++;

  if (auto stderr_lock =
          std::unique_lock<std::mutex>(stderr_mutex, std::try_to_lock)) {
    printProgress();
    llvm::errs() << " finished " << call << "\n";
  }

  return result;
}

void ThreadPoolEvaluator::printProgress() {
  // Load atomics in this order to avoid getting negative values.
  unsigned finished = num_finished;
  unsigned started = num_started;
  unsigned queued = num_queued;
  llvm::errs() << (queued - started) << " -> " << (started - finished) << " -> "
               << finished;
}

Evaluator::Evaluator() {}

Evaluator::~Evaluator() {}

Future Evaluator::makeFuture(std::shared_future<Link> &&future) {
  return Future(std::move(future));
}

std::unique_ptr<Evaluator> Evaluator::createLocal(std::unique_ptr<Store> store,
                                                  unsigned num_threads) {
  auto result =
      std::make_unique<ThreadPoolEvaluator>(std::move(store), num_threads);
  registerDefaultFuncs(*result);
  return result;
}

std::unique_ptr<Evaluator> Evaluator::create(llvm::StringRef uri,
                                             unsigned num_threads) {
  std::unique_ptr<Evaluator> result;
  if (uri.startswith("http:") || uri.startswith("https:") ||
      uri.startswith("tcp:") || uri.startswith("unix:")) {
    result = createClientEvaluator(uri, num_threads);
  } else {
    auto store = Store::open(uri);
    result =
        std::make_unique<ThreadPoolEvaluator>(std::move(store), num_threads);
  }
  registerDefaultFuncs(*result);
  return result;
}

PrettyStackTraceCall::PrettyStackTraceCall(const Call &call) : call(call) {
  llvm::get_thread_name(old_thread_name);
  // Linux only allows 16 characters in thread names (pthread_setname_np), so
  // there's no room for the arguments.
  llvm::set_thread_name(call.Name);
}

PrettyStackTraceCall::~PrettyStackTraceCall() {
  llvm::set_thread_name(old_thread_name);
}

void PrettyStackTraceCall::print(llvm::raw_ostream &os) const {
  os << "...which was evaluating " << call << "\n";
  os << "            to try again, run:\n";
  os << "              " << getArgv0() << " evaluate " << call << "\n";
  os << "            to save the inputs that caused the failure, run:\n";
  os << "              memodb export -o fail.car";
  for (const CID &arg : call.Args)
    os << " /cid/" << arg;
  os << "\n";
}
