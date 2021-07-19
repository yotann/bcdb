#ifndef MEMODB_EVALUATOR_H
#define MEMODB_EVALUATOR_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>

#include "Store.h"

namespace memodb {

// Used to register and call MemoDB functions, with an optional thread pool.
class Evaluator {
public:
  // If num_threads is 0, no threads will be created. Even with no thread pool,
  // the evaluateAsync() functions will still work correctly.
  Evaluator(std::unique_ptr<Store> store, unsigned num_threads = 0);
  ~Evaluator();

  Store &getStore() { return *store; }

  // Evaluate a call without using the thread pool.
  NodeRef evaluate(const Call &call);

  template <typename... Params>
  NodeRef evaluate(llvm::StringRef name, Params... args) {
    return evaluate(Call(name, {args...}));
  }

  // Evaluate a call, potentially using the thread pool. Returns a
  // std::shared_future containing a deferred function, so it will be evaluated
  // either by a thread in the thread pool or by the thread that calls
  // std::shared_future::wait() or std::shared_future::get(), whichever happens
  // first. Note that if something calls std::shared_future::wait_for() or
  // std::shared_future::wait_until() they won't actually wait; they'll just
  // return std::future_status::deferred.
  std::shared_future<NodeRef> evaluateAsync(const Call &call);

  template <typename... Params>
  std::shared_future<NodeRef> evaluateAsync(llvm::StringRef name,
                                            Params... args) {
    return evaluateAsync(Call(name, {args...}));
  }

  // Register a function that can be evaluated and cached.
  void registerFunc(llvm::StringRef name,
                    std::function<Node(Evaluator &, const Call &)> func);

  template <typename... Params>
  void registerFunc(llvm::StringRef name,
                    Node (*func)(Evaluator &, Params...)) {
    registerFunc(name,
                 funcImpl(name, func, std::index_sequence_for<Params...>{}));
  }

private:
  std::unique_ptr<Store> store;
  llvm::StringMap<std::function<Node(Evaluator &, const Call &)>> funcs;

  std::vector<std::thread> threads;
  std::mutex work_mutex;
  std::condition_variable work_cv;
  std::queue<std::shared_future<NodeRef>> work_queue;
  std::atomic<bool> work_done = false;

  // These counters only increase, never decrease.
  std::atomic<unsigned> num_queued = 0, num_started = 0, num_finished = 0;

  template <typename... Params, std::size_t... indexes>
  std::function<Node(Evaluator &, const Call &)>
  funcImpl(llvm::StringRef name, Node (*func)(Evaluator &, Params...),
           std::index_sequence<indexes...>) {
    return [=](Evaluator &evaluator, const Call &call) -> Node {
      if (call.Args.size() != sizeof...(Params))
        llvm::report_fatal_error("Incorrect number of arguments for " + name);
      return func(evaluator, evaluator.getStore().get(call.Args[indexes])...);
    };
  }

  void workerThreadImpl();
  void statusThreadImpl();

  NodeRef evaluateDeferred(const Call &call);
};

} // end namespace memodb

#endif // MEMODB_EVALUATOR_H
