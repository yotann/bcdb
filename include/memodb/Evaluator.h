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

// A future returned by Evaluator.
class Future {
public:
  Future(const Future &) = delete;
  Future(Future &&) = default;
  Future &operator=(const Future &) = delete;
  Future &operator=(Future &&) = default;

  NodeRef &get();
  void wait();
  const Node &operator*();
  const Node *operator->();
  const CID &getCID();
  void freeNode();
  bool checkForResult() const;

private:
  friend class Evaluator;
  explicit Future(std::shared_future<NodeRef> &&future);

  std::shared_future<NodeRef> future;
};

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
    return evaluate(Call(name, {NodeRef(getStore(), args).getCID()...}));
  }

  // Evaluate a call, potentially using the thread pool. Returns a Future, so
  // the call will be evaluated either by a thread in the thread pool or by the
  // thread that access fields in the Future, whichever happens first.
  Future evaluateAsync(const Call &call);

  template <typename... Params>
  Future evaluateAsync(llvm::StringRef name, Params... args) {
    return evaluateAsync(Call(name, {NodeRef(getStore(), args).getCID()...}));
  }

  // Register a function that can be evaluated and cached.
  void registerFunc(llvm::StringRef name,
                    std::function<NodeOrCID(Evaluator &, const Call &)> func);

  template <typename... Params>
  void registerFunc(llvm::StringRef name,
                    NodeOrCID (*func)(Evaluator &, Params...)) {
    registerFunc(name,
                 funcImpl(name, func, std::index_sequence_for<Params...>{}));
  }

private:
  std::unique_ptr<Store> store;
  llvm::StringMap<std::function<NodeOrCID(Evaluator &, const Call &)>> funcs;

  std::vector<std::thread> threads;
  std::mutex work_mutex;
  std::condition_variable work_cv;
  std::queue<std::shared_future<NodeRef>> work_queue;
  bool work_done = false;

  // These counters only increase, never decrease.
  std::atomic<unsigned> num_queued = 0, num_started = 0, num_finished = 0;
  std::mutex stderr_mutex;

  template <typename... Params, std::size_t... indexes>
  std::function<NodeOrCID(Evaluator &, const Call &)>
  funcImpl(llvm::StringRef name, NodeOrCID (*func)(Evaluator &, Params...),
           std::index_sequence<indexes...>) {
    return [=](Evaluator &evaluator, const Call &call) -> NodeOrCID {
      if (call.Args.size() != sizeof...(Params))
        llvm::report_fatal_error("Incorrect number of arguments for " + name);
      return func(evaluator,
                  NodeRef(evaluator.getStore(), call.Args[indexes])...);
    };
  }

  void workerThreadImpl();

  NodeRef evaluateDeferred(const Call &call);

  void printProgress();
};

} // end namespace memodb

#endif // MEMODB_EVALUATOR_H
