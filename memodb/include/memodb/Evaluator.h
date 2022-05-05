#ifndef MEMODB_EVALUATOR_H
#define MEMODB_EVALUATOR_H

#include <functional>
#include <future>
#include <memory>
#include <utility>

#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/PrettyStackTrace.h>

#include "Store.h"

namespace memodb {

class Evaluator;

/// A future returned by Evaluator. This represents a Call that has been
/// submitted to an Evaluator, but may not have been fully evaluated yet. Most
/// of the member functions will not return until the Call is finished
/// evaluating.
class Future {
public:
  Future(const Future &) = delete;
  Future(Future &&) = default;
  Future &operator=(const Future &) = delete;
  Future &operator=(Future &&) = default;

  /// Wait for the result and return it as a Link.
  const Link &get();

  /// Wait for the result.
  void wait();

  /// Wait for the result and return a reference to the result Node.
  const Node &operator*();

  /// Wait for the result and access a member of the result Node.
  const Node *operator->();

  /// Wait for the result and get the result's CID.
  const CID &getCID();

  /// Wait for the result and then free the stored Node, if any. Useful to
  /// reduce memory usage.
  void freeNode();

  /// Check whether the result is already available, without waiting. In some
  /// cases, the Future will not finish evaluation until one of the other
  /// member functions is called.
  bool checkForResult() const;

private:
  friend class Evaluator;
  explicit Future(std::shared_future<Link> &&future);

  std::shared_future<Link> future;
};

/// Used to register and call MemoDB funcs. Depending on how the Evaluator is
/// created, funcs may be evaluated by a local thread pool, by distributed
/// workers connected to a server, or by the local thread that calls
/// Evaluator::evaluate().
class Evaluator {
public:
  Evaluator();
  virtual ~Evaluator();

  /// Create an evaluator that uses a thread pool. If \p num_threads is 0, no
  /// new threads will be created, but all functions will still work correctly.
  static std::unique_ptr<Evaluator> createLocal(std::unique_ptr<Store> store,
                                                unsigned num_threads = 0);

  /// Create an evaluator that uses a thread pool and potentially uses
  /// distributed workers. If the URI refers to a remote server, jobs may be
  /// submitted to the server for evaluation by distributed workers, and
  /// workers in the local thread pool may evaluate jobs received from the
  /// server from other clients. If \p num_threads is 0, no local thread pool
  /// will be created.
  static std::unique_ptr<Evaluator> create(llvm::StringRef uri,
                                           unsigned num_threads = 0);

  /// Get the Store this Evaluator is connected to.
  virtual Store &getStore() = 0;

  /// Evaluate a call and wait until evaluation is done.
  virtual Link evaluate(const Call &call) = 0;

  /// Evaluate a call and wait until evaluation is done. \p args can be Node or
  /// CID values.
  template <typename... Params>
  Link evaluate(llvm::StringRef name, Params... args) {
    return evaluate(Call(name, {Link(getStore(), args).getCID()...}));
  }

  /// Start evaluation of a call, returning a Future.
  virtual Future evaluateAsync(const Call &call) = 0;

  /// Start evaluation of a call, returning a Future. \p args can be Node or
  /// CID values.
  template <typename... Params>
  Future evaluateAsync(llvm::StringRef name, Params... args) {
    return evaluateAsync(Call(name, {Link(getStore(), args).getCID()...}));
  }

  /// Register a function that can be evaluated locally and cached. This
  /// function is not thread-safe, and must be called before any calls of
  /// evaluate() or evaluateAsync().
  virtual void
  registerFunc(llvm::StringRef name,
               std::function<NodeOrCID(Evaluator &, const Call &)> func) = 0;

  /// Register a function that can be evaluated locally and cached. This
  /// function is not thread-safe, and must be called before any calls of
  /// evaluate() or evaluateAsync(). The passed function must return a
  /// NodeOrCID, and must take an Evaluator & as the first argument and Link
  /// as the remaining arguments.
  template <typename... Params>
  void registerFunc(llvm::StringRef name,
                    NodeOrCID (*func)(Evaluator &, Params...)) {
    registerFunc(name,
                 funcImpl(name, func, std::index_sequence_for<Params...>{}));
  }

protected:
  friend class Future;

  Future makeFuture(std::shared_future<Link> &&future);

private:
  template <typename... Params, std::size_t... indexes>
  std::function<NodeOrCID(Evaluator &, const Call &)>
  funcImpl(llvm::StringRef name, NodeOrCID (*func)(Evaluator &, Params...),
           std::index_sequence<indexes...>) {
    return [=](Evaluator &evaluator, const Call &call) -> NodeOrCID {
      if (call.Args.size() != sizeof...(Params))
        llvm::report_fatal_error("Incorrect number of arguments for " + name);
      return func(evaluator, Link(evaluator.getStore(), call.Args[indexes])...);
    };
  }
};

// This is called by Evaluator::create().
void registerDefaultFuncs(Evaluator &evaluator);

class PrettyStackTraceCall : public llvm::PrettyStackTraceEntry {
  const Call &call;
  llvm::SmallVector<char, 16> old_thread_name;

public:
  PrettyStackTraceCall(const Call &call);
  ~PrettyStackTraceCall();
  void print(llvm::raw_ostream &os) const override;
};

} // end namespace memodb

#endif // MEMODB_EVALUATOR_H
