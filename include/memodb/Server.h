#ifndef MEMODB_SERVER_H
#define MEMODB_SERVER_H

#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <optional>

#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>

#include "Store.h"

namespace memodb {

class PendingCall;
class Request;
class WorkerGroup;

// Keeps track of all calls of a single function we need to evaluate. A
// CallGroup is never deleted, and has a fixed location in memory. All member
// variables and functions are protected by CallGroup::mutex.
class CallGroup {
public:
  std::mutex mutex;

  // The queue of calls we have been requested to evaluate that have not yet
  // been assigned to a worker.
  std::deque<PendingCall *> unstarted_calls;

  // The actual PendingCalls.
  std::map<Call, PendingCall> calls;

  // Delete all PendingCalls from the start of unstarted_calls that have
  // already completed. (These are calls for which a client has already
  // submitted a result to us, even though we didn't assign them to a worker.)
  void deleteSomeUnstartedCalls();
};

// Keeps track of a single call we need to evaluate. A PendingCall can be
// deleted, and has a fixed location in memory. All member variables and
// functions, except read access to call_group, are protected by
// call_group->mutex.
class PendingCall {
public:
  CallGroup *call_group;
  Call call;

  // Whether the evaluation has been assigned to a worker.
  bool started = false;

  std::chrono::time_point<std::chrono::steady_clock> start_time;
  unsigned minutes_to_report;

  // Whether the evaluation has been completed.
  bool finished = false;

  PendingCall(CallGroup *call_group, const Call &call)
      : call_group(call_group), call(call) {}

  // Delete this PendingCall from the parent CallGroup if possible.
  void deleteIfPossible();
};

// Keeps track of all workers with a single worker information CID. A
// WorkerGroup is never deleted, and has a fixed location in memory. All member
// variables and functions are protected by WorkerGroup::mutex, except
// call_groups (which is read-only after creation).
class WorkerGroup {
public:
  std::mutex mutex;

  // All the CallGroups for funcs that these workers can handle.
  llvm::SmallVector<CallGroup *, 0> call_groups;
};

class Server {
public:
  Server(Store &store);

  // This function will always send a response to the request. Thread-safe.
  void handleRequest(Request &request);

private:
  void handleNewRequest(Request &request);
  void handleRequestCID(Request &request,
                        std::optional<llvm::StringRef> cid_str);
  void handleRequestHead(Request &request,
                         std::optional<llvm::StringRef> head_str);
  void handleRequestCall(Request &request,
                         std::optional<llvm::StringRef> func_str,
                         std::optional<llvm::StringRef> args_str,
                         std::optional<llvm::StringRef> sub_str);
  void handleRequestWorker(Request &request);
  void handleEvaluateCall(Request &request, Call call, unsigned timeout);
  void handleCallResult(const Call &call, Link result);
  void sendCallToWorker(PendingCall &pending_call, Request &worker);

  Store &store;

  // All variables below are protected by the mutex.
  std::mutex mutex;
  llvm::StringMap<CallGroup> call_groups;
  llvm::StringMap<WorkerGroup> worker_groups;
};

} // end namespace memodb

#endif // MEMODB_SERVER_H
