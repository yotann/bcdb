#include <chrono>
#include <stdexcept>
#include <thread>

#include <llvm/Support/Error.h>

#include "memodb/Evaluator.h"
#include "memodb/Node.h"

using namespace memodb;

static NodeOrCID test_add(Evaluator &evaluator, Link lhs_node, Link rhs_node) {
  int64_t lhs = lhs_node->as<int64_t>();
  int64_t rhs = rhs_node->as<int64_t>();
  return Node(lhs + rhs);
}

static NodeOrCID test_ackermann(Evaluator &evaluator, Link lhs_node,
                                Link rhs_node) {
  // Calculate the Ackermann-Péter function. This is a good stress test for
  // evaluate().

  int64_t lhs = lhs_node->as<int64_t>();
  int64_t rhs = rhs_node->as<int64_t>();
  if (lhs <= 0)
    return Node(rhs + 1);
  if (rhs <= 0)
    rhs = 1;
  else
    rhs = evaluator.evaluate("test.ackermann", lhs_node, Node(rhs - 1))
              ->as<int64_t>();
  return evaluator.evaluate("test.ackermann", Node(lhs - 1), Node(rhs))
      .getCID();
}

static NodeOrCID test_nqueens(Evaluator &evaluator, Link size_node,
                              Link queens_node) {
  // A simple recursive solution for the N-queens problem.
  // - size_node is the integer N.
  // - queens_node is the list of queen positions already chosen.
  // - return value is the number of valid solutions.
  //
  // This is a good stress test for evaluateAsync():
  //  $ memodb evaluate /call/test.nqueens/uAXEAAQg,uAXEAAYA
  //  uAXEAAhhc

  unsigned size = size_node->as<unsigned>();
  Node queens = *queens_node;
  unsigned pos = queens.size();

  // Check whether the last queen is in a valid position.
  if (pos) {
    unsigned q1 = queens[pos - 1].as<unsigned>();
    for (unsigned i = 0; i < pos - 1; i++) {
      unsigned q0 = queens[i].as<unsigned>();
      if (q0 == q1 || (q1 > q0 ? q1 - q0 : q0 - q1) == pos - 1 - i)
        return Node(0);
    }
  }

  // The board is full!
  if (pos >= size)
    return Node(1);

  // Recursively try each possible position for the next queen.
  std::vector<Future> futures;
  queens.emplace_back(0);
  for (unsigned i = 0; i < size; i++) {
    queens[pos] = i;
    futures.emplace_back(
        evaluator.evaluateAsync("test.nqueens", size_node, queens));
  }
  unsigned result = 0;
  for (auto &future : futures)
    result += future->as<unsigned>();
  return Node(result);
}

static NodeOrCID test_sleep(Evaluator &evaluator, Link node) {
  // Sleep for the given number of milliseconds.
  //
  // This func is used to simulate long-running CPU jobs, which don't call
  // boost::fibers::yield(). So we make the whole thread sleep, instead of just
  // the fiber.

  using namespace std::chrono_literals;
  unsigned value = node->as<unsigned>();
  std::this_thread::sleep_for(value * 1ms);
  return Node();
}

static NodeOrCID test_fatal_error(Evaluator &evaluator, Link) {
  // Test reporting a fatal error.
  llvm::report_fatal_error("test.fatal_error evaluated");
}

static NodeOrCID test_throw_exception(Evaluator &evaluator, Link) {
  // Test throwing an exception.
  throw std::runtime_error("test.throw_exception evaluated");
}

void memodb::registerDefaultFuncs(Evaluator &evaluator) {
  evaluator.registerFunc("test.add", test_add);
  evaluator.registerFunc("test.ackermann", test_ackermann);
  evaluator.registerFunc("test.nqueens", test_nqueens);
  evaluator.registerFunc("test.sleep", test_sleep);
  evaluator.registerFunc("test.fatal_error", test_fatal_error);
  evaluator.registerFunc("test.throw_exception", test_throw_exception);
}
