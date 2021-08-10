#ifndef MEMODB_EVALUATOR_H
#define MEMODB_EVALUATOR_H

#include <functional>
#include <future>
#include <memory>
#include <utility>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>

#include "Store.h"

namespace memodb {

class Evaluator;

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
  explicit Future(Evaluator *evaluator, std::shared_future<NodeRef> &&future);

  Evaluator *evaluator;
  std::shared_future<NodeRef> future;
};

// Used to register and call MemoDB funcs. Depending on how the Evaluator is
// created, funcs may be evaluated by a local thread pool, by distributed
// workers connected to a server, or by the local thread that calls evaluate().
class Evaluator {
public:
  Evaluator();
  virtual ~Evaluator();

  // Create an evaluator that uses a thread pool. If num_threads is 0, no new
  // threads will be created, but all functions will still work correctly.
  static std::unique_ptr<Evaluator> createLocal(std::unique_ptr<Store> store,
                                                unsigned num_threads = 0);

  // Create an evaluator that uses a thread pool and potentially uses
  // distributed workers. If the URI refers to a remote server, jobs may be
  // submitted to the server for evaluation by distributed workers, and workers
  // in the local thread pool may evaluate jobs received from the server from
  // other clients.
  static std::unique_ptr<Evaluator> create(llvm::StringRef uri,
                                           unsigned num_threads = 0);

  virtual Store &getStore() = 0;

  // Evaluate a call and wait until evaluation is done.
  virtual NodeRef evaluate(const Call &call) = 0;

  template <typename... Params>
  NodeRef evaluate(llvm::StringRef name, Params... args) {
    return evaluate(Call(name, {NodeRef(getStore(), args).getCID()...}));
  }

  // Start evaluation of a call, returning a Future.
  virtual Future evaluateAsync(const Call &call) = 0;

  template <typename... Params>
  Future evaluateAsync(llvm::StringRef name, Params... args) {
    return evaluateAsync(Call(name, {NodeRef(getStore(), args).getCID()...}));
  }

  // Register a function that can be evaluated locally and cached. This
  // function is not thread-safe, and must be called before any calls of
  // evaluate() or evaluateAsync().
  virtual void
  registerFunc(llvm::StringRef name,
               std::function<NodeOrCID(Evaluator &, const Call &)> func) = 0;

  template <typename... Params>
  void registerFunc(llvm::StringRef name,
                    NodeOrCID (*func)(Evaluator &, Params...)) {
    registerFunc(name,
                 funcImpl(name, func, std::index_sequence_for<Params...>{}));
  }

  virtual void handleFutureStartsWaiting();
  virtual void handleFutureStopsWaiting();

protected:
  Future makeFuture(std::shared_future<NodeRef> &&future);

private:
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
};

} // end namespace memodb

#endif // MEMODB_EVALUATOR_H
