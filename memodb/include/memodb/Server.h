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

  // The queue of calls we have assigned to workers that have timed out without
  // receiving a response.
  std::deque<PendingCall *> calls_to_retry;

  // The actual PendingCalls.
  std::map<Call, PendingCall> calls;

  // Delete all PendingCalls from the start of unstarted_calls and
  // calls_to_retry that have already been completed. (Either by a worker we
  // previously assigned the call to and then timed out, or an unrelated worker
  // that happens to have completed the call on its own.)
  void deleteSomeFinishedCalls();
};

// Keeps track of a single call we need to evaluate. A PendingCall can be
// deleted (by a thread that holds call_group->mutex), and has a fixed location
// in memory. All member variables and functions, except read access to
// call_group, are protected by call_group->mutex.
class PendingCall {
public:
  CallGroup *call_group;
  Call call;

  // Whether the evaluation is currently assigned to a worker. If false, this
  // call is necessarily queued in CallGroup::unstarted_calls or
  // CallGroup::calls_to_retry; if true, this call isn't in either queue.
  bool assigned = false;

  // The time when this job was assigned to a worker. Only valid if
  // this->assigned is true.
  std::chrono::time_point<std::chrono::steady_clock> start_time;

  // The number of minutes to wait after assigning this job to a worker before
  // timing out and requeuing it. Only used if this->assigned is true.
  unsigned timeout_minutes;

  // Whether the evaluation has been completed.
  bool finished = false;

  PendingCall(CallGroup *call_group, const Call &call);

  // Delete this PendingCall from the parent CallGroup if possible.
  void deleteIfPossible();
};

// Keeps track of all workers with a single worker information CID. A
// WorkerGroup is never deleted, and has a fixed location in memory. There's no
// mutex, which is okay because all fields are currently read-only after
// creation.
class WorkerGroup {
public:
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
                        std::optional<llvm::StringRef> cid_str,
                        std::optional<llvm::StringRef> sub_str);
  void handleRequestHead(Request &request,
                         std::optional<llvm::StringRef> head_str);
  void handleRequestCall(Request &request,
                         std::optional<llvm::StringRef> func_str,
                         std::optional<llvm::StringRef> args_str,
                         std::optional<llvm::StringRef> sub_str);
  void handleRequestWorker(Request &request);
  void handleEvaluateCall(Request &request, Call call);
  void handleCallResult(const Call &call, Link result);
  void sendCallToWorker(PendingCall &pending_call, Request &worker,
                        std::unique_lock<std::mutex> call_group_lock);

  Store &store;

  // All variables below are protected by the mutex.
  std::mutex mutex;
  llvm::StringMap<CallGroup> call_groups;
  llvm::StringMap<WorkerGroup> worker_groups;
};

} // end namespace memodb

#endif // MEMODB_SERVER_H
