/*
 * # MemoDB Broker
 *
 * The broker is a server that clients and workers can connect to. The broker
 * accepts jobs from clients that need to be processed, and forwards them to
 * workers; when a worker has finished a job and produced a result, the broker
 * forwards the result back to the client.
 *
 * For a detailed description of the protocol, see docs/memodb/protocol.md.
 *
 * ## Synchronization within the broker
 *
 * We don't explicitly create any threads, but NNG does, and it may call
 * callbacks from multiple threads simultaneously. For simplicity, we avoid
 * problems by protecting just about everything with a global mutex.
 *
 * There's still one race condition we need to prevent:
 *
 *  1. NNG calls a timeout callback and a message-received callback
 *     simultaneously that affect the same object.
 *  2. The message-received callback locks the global mutex first.
 *  3. The message-received callback attempts to cancel the timeout, but
 *     nothing happens because the timeout is already in progress.
 *  4. The message-received callback sets a new timeout on the object and then
 *     returns.
 *  5. The timeout callback now locks the global mutex, and incorrectly decides
 *     that the new timeout (which just started) has expired, rather than the
 *     old one.
 *
 * Several possible solutions:
 *
 *  - **The current solution:** Use a different callback pointer each time the
 *    timeout is set, pointing to a struct that includes the expected timeout
 *    number. If that number matches the current timeout number in the object
 *    itself, the correct timeout has expired. Requires allocation of a new
 *    struct and a new nng::aio instance every time a timeout is set.
 *
 *  - **TODO: explore this option.** Instead of using nng::aio, manage our own
 *    timeout queue, which uses the global mutex and allows deleting timeouts
 *    that we don't need anymore.
 *
 *  - Have the timeout callback check whether the current time matches the
 *    current intended timeout time. Could lead to shenanigans when the system
 *    clock changes, or if NNG's idea of time is slightly different from the
 *    clock's. BROKEN because of a race condition in nng::aio::cancel().
 *
 *  - Call nng::aio::wait after cancelling the timeout in step 3, so the
 *    timeout callback finishes. We need to use two global mutexes: one used to
 *    ensure only one message is processed at once, and one to ensure only one
 *    thread accesses global structures at a time. The latter mutex is
 *    temporarily unlocked while calling nng::aio::wait so the timeout callback
 *    can lock it.
 *
 *    Unfortunately, this can still deadlock. nng::aio::cancel doesn't call the
 *    callback with nng::error::canceled immediately; it adds a task that does
 *    so to the task queue. And nng::aio::wait blocks until that task
 *    completes. If all threads are blocked on nng::aio::wait or the message
 *    mutex, nothing from the task queue can be executed, so the whole process
 *    deadlocks.
 */

#include <arpa/inet.h>
#include <deque>
#include <iostream>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/ilist_node.h>
#include <llvm/ADT/simple_ilist.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <utility>

#include "memodb/Node.h"
#include "memodb/ToolSupport.h"
#include "memodb/nngmm.h"

using namespace llvm;
using namespace memodb;

static cl::OptionCategory BrokerCategory("MemoDB Broker options");

static cl::opt<std::string> ListenURL(cl::Positional, cl::Required,
                                      cl::desc("<broker port>"),
                                      cl::value_desc("port"),
                                      cl::cat(BrokerCategory));

// Must be an unsigned integer.
typedef uint32_t WorkerID;

// If a worker has been waiting this long for a job, we send it a HEARTBEAT.
static const nng_duration WORKER_HEARTBEAT_TIME = 10000;

// If a worker doesn't respond to this amount of time (or longer if the job has
// a longer timeout), we assume it's disconnected.
static const nng_duration WORKER_TIMEOUT = 10000;

// If a job waits this long with no available workers, we give up and
// disconnect from the client that submitted the job.
static const nng_duration JOB_QUEUE_TIMEOUT = 10000;

static std::mutex GlobalMutex;

namespace {
struct Context;
struct Worker;
} // end anonymous namespace

typedef size_t ServiceNumber;
typedef size_t ServiceSetNumber;

namespace {
struct Service {
  std::string Name;
  std::vector<ServiceSetNumber> Sets = {};
  simple_ilist<Context> WaitingClients = {};
};
} // end anonymous namespace

namespace {
struct ServiceSet {
  std::vector<ServiceNumber> Services = {};
  simple_ilist<Worker> WaitingWorkers = {};
};
} // end anonymous namespace

namespace {
struct Object {
  struct PendingTimeout {
    nng::aio Aio;
    Object *Parent = nullptr;
    size_t ExpectedIndex = 0;
    bool Busy = false;

    PendingTimeout() : Aio(aioCallback, this) {}

    static void aioCallback(void *Opaque) {
      PendingTimeout *PT = static_cast<PendingTimeout *>(Opaque);
      if (PT->Aio.result() != nng::error::success) {
        std::cerr << "timeout error: " << nng::to_string(PT->Aio.result())
                  << "\n";
        // TODO: error handling
      } else {
        std::lock_guard<std::mutex> Lock(GlobalMutex);
        if (PT->ExpectedIndex == PT->Parent->TimeoutIndex)
          PT->Parent->timeout();
        PT->Busy = false;
      }
    }

    void start(Object *Parent, nng_duration Duration) {
      assert(!Busy);
      this->Parent = Parent;
      ExpectedIndex = Parent->TimeoutIndex;
      Busy = true;
      nng::sleep(Duration, Aio);
    }
  };

  static std::deque<PendingTimeout> PendingTimeouts;

  size_t TimeoutIndex = 0;

  Object() {}
  virtual ~Object() {}

  void startTimeout(nng_duration Duration) {
    TimeoutIndex++;
    for (PendingTimeout &PT : PendingTimeouts) {
      if (!PT.Busy) {
        PT.start(this, Duration);
        return;
      }
    }
    PendingTimeouts.emplace_back();
    PendingTimeouts.back().start(this, Duration);
  }

  void cancelTimeout() { TimeoutIndex++; }

  virtual void timeout() = 0;
};
} // end anonymous namespace

std::deque<Object::PendingTimeout> Object::PendingTimeouts;

// clang-format off
/*
 * Each Context can handle one request at a time.
 *
 * .Context states (AsciiDoc format)
 * |====
 * |State|Means...|Aio is...|Active timeout|We can be accessed by...|Valid fields
 *
 * |RECEIVING
 * |Called Ctx.recv()
 * |Waiting for new request
 * |--
 * |Our callback
 * |Ctx, Aio
 *
 * |SENDING
 * |Called Ctx.send()
 * |Waiting for reply to be queued
 * |--
 * |Our callback
 * |Ctx, Aio
 *
 * |JOB_QUEUED
 * |Received JOB, but no workers were available
 * |--
 * |Sleeping until JOB times out and we give up on finding a worker
 * |Our callback or an arbitrary worker context's callback
 * |Ctx, Aio, JobService, JobPayload, JobTimeout
 *
 * |WORKER_WAITING
 * |Received request from worker, but no jobs were available
 * |--
 * |--
 * |Our worker's timeout callback or an arbitrary client context's callback
 * |Ctx, Aio
 *
 * |JOB_PROCESSING
 * |Forwarded our JOB to a worker, waiting for RESULT
 * |--
 * |--
 * |The worker context's callback or the worker's timeout callback
 * |Ctx, Aio
 *
 * |====
 */
// clang-format on
namespace {
struct Context : Object, ilist_node<Context> {
  enum StateEnum {
    RECEIVING,
    SENDING,
    JOB_QUEUED,
    WORKER_WAITING,
    JOB_PROCESSING
  } State;
  nng::ctx Ctx;
  nng::aio Aio;
  ServiceNumber JobService;     // only if State == JOB_QUEUED
  ArrayRef<uint8_t> JobPayload; // only if State == JOB_QUEUED
  int64_t JobTimeout;           // only if State == JOB_QUEUED

  Context();
  ~Context() {}
  void aioCallback();
  void changeState(StateEnum NewState);
  void disconnectWorker(const Node &Id);
  Worker *findWorkerExpectedState(const Node &Id, int ExpectedState);
  void handleClientJob(ServiceNumber SN, int64_t Timeout,
                       ArrayRef<uint8_t> Payload);
  void handleMessage();
  void handleWorkerReady(const Node &ServiceNames);
  void invalidMessage();
  void reset();
  void send(const Node &Header, ArrayRef<uint8_t> Payload = {});
  void timeout() override;
};
} // end anonymous namespace

// clang-format off
/*
 * Each Worker can handle one job at a time. Note that each request from the
 * worker may arrive at a different Context.
 *
 * NOTE: in addition to a valid request, it's also possible that we receive two
 * simultaneous requests using the same worker ID, which is invalid. The global
 * mutex ensures that only one request will be handled at a time, and the
 * second request will detect the error and disconnect the worker.
 *
 * .Worker states (AsciiDoc format)
 * |====
 * |State|Means...|Active timeout|We can be accessed by...|Valid fields
 *
 * |WAITING_FOR_JOB
 * |Received request from worker, but no jobs were available
 * |Sleeping until request times out and we send HEARTBEAT
 * |Our timeout callback, or an arbitrary client context's callback
 * |ID, SSN, Aio, WorkerContext
 *
 * |WAITING_FOR_RESULT
 * |Sent JOB to worker, waiting for RESULT
 * |Sleeping until JOB times out and we disconnect the worker
 * |Our timeout callback, or the Context getting the worker's next request
 * |ID, SSN, Aio, ClientContext
 *
 * |WAITING_FOR_HEARTBEAT
 * |Sent HEARTBEAT to worker, waiting for HEARTBEAT
 * |Sleeping until HEARTBEAT times out and we disconnect the worker
 * |Our timeout callback, or the Context getting the worker's next request
 * |ID, SSN, Aio
 *
 * |DISCONNECTED
 * |Worker no longer validly connected
 * |--
 * |--
 * |--
 *
 * |====
 */
// clang-format on
namespace {
struct Worker : Object, ilist_node<Worker> {
  enum StateEnum {
    INIT,
    WAITING_FOR_JOB,
    WAITING_FOR_RESULT,
    WAITING_FOR_HEARTBEAT,
    DISCONNECTED
  } State = INIT;

  WorkerID ID;

  // Which services are handled by this worker.
  ServiceSetNumber SSN;

  // If State is WAITING_FOR_JOB, the Context that has an outstanding request
  // from the worker.
  Context *WorkerContext = nullptr;

  // If State is WAITING_FOR_RESULT, the Context connected to the client that
  // requested the job.
  Context *ClientContext = nullptr;

  Worker(ServiceSetNumber SSN, WorkerID ID);
  ~Worker() {}
  void changeState(StateEnum NewState);
  Node encodeID();
  void handleRequest(Context *Context);
  void handleResult(ArrayRef<uint8_t> Payload);
  void sendHeartbeat();
  void startJob(Context *Client);
  void timeout() override;

  static Worker *getById(const Node &ID);
};
} // end anonymous namespace

// We use a different random set of worker IDs for each broker process. We can
// check the worker ID in each request to determine if the worker actually got
// its job from a different broker process (which may have died and restarted).
static WorkerID FirstWorkerID = nng::random();

static nng::socket GlobalSocket;

// Must be deque so pointers are not invalidated. Contexts are never deleted.
static std::deque<Context> GlobalContexts;

// Must be deque so pointers are not invalidated. Workers are never deleted,
// even after they disconnect.
static std::deque<Worker> Workers;

static std::vector<Service> Services;
static std::vector<ServiceSet> ServiceSets;
static llvm::StringMap<size_t> ServiceNumbers;
static std::map<std::vector<size_t>, size_t> ServiceSetNumbers;

static ServiceNumber lookupService(const Node &Name) {
  auto Entry =
      ServiceNumbers.try_emplace(Name.as<StringRef>(), Services.size());
  if (Entry.second) {
    std::cerr << "New service: " << Name << "\n";
    Services.push_back({Name.as<std::string>()});
  }
  return Entry.first->second;
}

static ServiceSetNumber lookupServiceSet(const Node &Names) {
  std::vector<size_t> Numbers;
  for (const Node &Item : Names.list_range())
    Numbers.emplace_back(lookupService(Item));

  auto Entry = ServiceSetNumbers.try_emplace(Numbers, ServiceSets.size());
  if (Entry.second) {
    std::cerr << "New service set: " << Names << "\n";
    ServiceSet NewSet;
    for (size_t SN : Numbers) {
      NewSet.Services.push_back(SN);
      Services[SN].Sets.push_back(Entry.first->second);
    }
    ServiceSets.push_back(std::move(NewSet));
  }
  return Entry.first->second;
}

static void contextAioCallback(void *opaque) {
  reinterpret_cast<Context *>(opaque)->aioCallback();
}

Context::Context()
    : State(RECEIVING), Ctx(GlobalSocket), Aio(contextAioCallback, this) {
  Ctx.recv(Aio);
}

void Context::aioCallback() {
  if (Aio.result() == nng::error::canceled)
    return;

  if (Aio.result() == nng::error::closed) {
    // This happens when main() exits and closes the socket.
    return;
  }

  if (Aio.result() != nng::error::success) {
    std::cerr << "context error: " << nng::to_string(Aio.result()) << "\n";
    // TODO: error handling
    return;
  }

  std::lock_guard<std::mutex> Lock(GlobalMutex);
  if (State == RECEIVING) {
    handleMessage();
  } else if (State == SENDING) {
    changeState(RECEIVING);
    Ctx.recv(Aio);
  } else if (State == JOB_QUEUED) {
    report_fatal_error("Aio should not be triggered in JOB_QUEUED");
  } else if (State == JOB_PROCESSING) {
    report_fatal_error("Aio should not be triggered in JOB_PROCESSING");
  } else if (State == WORKER_WAITING) {
    report_fatal_error("Aio should not be triggered in WORKER_WAITING");
  } else {
    llvm_unreachable("impossible Context state");
  }
}

void Context::timeout() {
  assert(State == JOB_QUEUED);
  std::cerr << "job timed out in queue\n";
  reset();
}

void Context::changeState(StateEnum NewState) {
  if (NewState == State)
    return;

  if (State == JOB_QUEUED)
    Services[JobService].WaitingClients.remove(*this);

  State = NewState;

  if (State != JOB_QUEUED && State != SENDING)
    Aio.release_msg(); // invalidates JobPayload
  if (State == JOB_QUEUED) {
    Services[JobService].WaitingClients.push_back(*this);
    // FIXME: What if this deletes the message that JobPayload points to?
    startTimeout(JOB_QUEUE_TIMEOUT);
  }
}

void Context::disconnectWorker(const Node &Id) {
  std::cerr << "disconnecting unknown worker " << Id << "\n";
  send(Node(node_list_arg, {"memo01", 0x05, Id}));
}

Worker *Context::findWorkerExpectedState(const Node &Id, int ExpectedState) {
  Worker *Worker = Worker::getById(Id);
  if (!Worker)
    return nullptr;
  Worker->cancelTimeout();
  if (Worker->State != ExpectedState) {
    Worker->changeState(Worker::DISCONNECTED);
    return nullptr;
  }
  return Worker;
}

void Context::handleClientJob(ServiceNumber SN, int64_t Timeout,
                              ArrayRef<uint8_t> Payload) {
  JobService = SN;
  JobPayload = Payload;
  JobTimeout = Timeout;

  for (ServiceSetNumber SSN : Services[SN].Sets) {
    if (!ServiceSets[SSN].WaitingWorkers.empty()) {
      Worker *Worker = &ServiceSets[SSN].WaitingWorkers.front();
      if (Worker->State == Worker::WAITING_FOR_JOB) {
        Worker->cancelTimeout();
        return Worker->startJob(this);
      }
    }
  }

  // No appropriate workers waiting.
  changeState(JOB_QUEUED);
}

void Context::handleMessage() {
  auto Msg = Aio.get_msg();
  ArrayRef<uint8_t> Data(reinterpret_cast<const uint8_t *>(Msg.body().data()),
                         Msg.body().size());
  // FIXME: handle CBOR errors using exceptions, don't just abort.
  Node Header = Node::load_cbor_from_sequence(Data);

  if (Header.kind() != Kind::List || Header.size() < 3 ||
      Header[0] != "memo01" || !Header[1].is<std::uint8_t>() ||
      Header[2].kind() != Kind::Bytes)
    return invalidMessage();

  auto Operation = Header[1].as<std::uint8_t>();
  const auto &Id = Header[2];

  if (Operation == 0x01) { // worker READY
    if (Header.size() != 4 || !Id.empty() || !Data.empty())
      return invalidMessage();
    const auto &ServiceSet = Header[3];
    return handleWorkerReady(ServiceSet);

  } else if (Operation == 0x02) { // client JOB
    if (Header.size() != 5 || !Id.empty() || Header[3].kind() != Kind::String ||
        !Header[4].is<std::int64_t>())
      return invalidMessage();
    return handleClientJob(lookupService(Header[3]),
                           Header[4].as<std::int64_t>(), Data);

  } else if (Operation == 0x03) { // worker RESULT
    auto NumItems = Header.size();
    if (Id.empty() || NumItems < 3 || NumItems > 4)
      return invalidMessage();
    if (NumItems >= 4 && Header[3].kind() != Kind::Boolean)
      return invalidMessage();
    bool Disconnecting = NumItems >= 4 ? Header[3].as<bool>() : false;
    Worker *Worker = findWorkerExpectedState(Id, Worker::WAITING_FOR_RESULT);
    if (!Worker)
      return disconnectWorker(Id);
    Worker->handleResult(Data);
    if (Disconnecting) {
      send(Node(node_list_arg, {"memo01", 0x05, Id}));
      return Worker->changeState(Worker::DISCONNECTED);
    } else {
      return Worker->handleRequest(this);
    }

  } else if (Operation == 0x04) { // worker HEARTBEAT
    if (Header.size() != 3 || Id.empty() || !Data.empty())
      return invalidMessage();
    Worker *Worker = findWorkerExpectedState(Id, Worker::WAITING_FOR_HEARTBEAT);
    if (!Worker)
      return disconnectWorker(Id);
    return Worker->handleRequest(this);

  } else {
    std::cerr << "Unsupported operation " << Operation << "\n";
    return invalidMessage();
  }
}

void Context::handleWorkerReady(const Node &ServiceNames) {
  if (ServiceNames.kind() != Kind::List)
    return invalidMessage();
  Node PrevService = "";
  for (const auto &Service : ServiceNames.list_range()) {
    if (Service.kind() != Kind::String || Service.empty())
      return invalidMessage();
    // Check for correct order.
    if (Service.size() < PrevService.size())
      return invalidMessage();
    if (Service.size() == PrevService.size() && !(PrevService < Service))
      return invalidMessage();
    PrevService = Service;
  }

  ServiceSetNumber SSN = lookupServiceSet(ServiceNames);
  Workers.emplace_back(SSN, FirstWorkerID + Workers.size());
  return Workers.back().handleRequest(this);
}

void Context::invalidMessage() {
  auto RemoteAddr = Aio.get_msg().get_pipe().get_opt_addr(NNG_OPT_REMADDR);
  std::cerr << "invalid message received from ";
  if (RemoteAddr.s_family == NNG_AF_IPC) {
    std::cerr << "ipc://" << RemoteAddr.s_ipc.sa_path;
  } else if (RemoteAddr.s_family == NNG_AF_INET) {
    struct in_addr src = {RemoteAddr.s_in.sa_addr};
    char buffer[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &src, buffer, sizeof(buffer));
    buffer[sizeof(buffer) - 1] = 0;
    std::cerr << "tcp://" << buffer << ":" << RemoteAddr.s_in.sa_port;
  } else if (RemoteAddr.s_family == NNG_AF_INET6) {
    struct in6_addr src;
    for (int i = 0; i < 16; i++)
      src.s6_addr[i] = RemoteAddr.s_in6.sa_addr[i];
    char buffer[INET6_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET6, &src, buffer, sizeof(buffer));
    buffer[sizeof(buffer) - 1] = 0;
    std::cerr << "tcp://[" << buffer << "]:" << RemoteAddr.s_in6.sa_port;
  } else {
    std::cerr << "unknown socket type";
  }
  std::cerr << "\n";

  // Ignore the message and reset Ctx.
  reset();
}

void Context::reset() {
  changeState(RECEIVING);
  Ctx = nng::ctx(GlobalSocket);
  Ctx.recv(Aio);
}

void Context::send(const Node &Header, ArrayRef<uint8_t> Payload) {
  std::vector<uint8_t> Bytes;
  Header.save_cbor(Bytes);

  nng::msg Msg(Bytes.size() + Payload.size());
  memcpy(Msg.body().data(), Bytes.data(), Bytes.size());
  memcpy(reinterpret_cast<char *>(Msg.body().data()) + Bytes.size(),
         Payload.data(), Payload.size());
  Aio.set_msg(std::move(Msg));

  changeState(SENDING);
  Ctx.send(Aio);
}

Worker::Worker(ServiceSetNumber SSN, WorkerID ID) : ID(ID), SSN(SSN) {}

void Worker::timeout() {
  if (State == WAITING_FOR_JOB) {
    sendHeartbeat();
  } else if (State == WAITING_FOR_RESULT) {
    std::cerr << "worker timed out\n";
    changeState(DISCONNECTED);
  } else if (State == WAITING_FOR_HEARTBEAT) {
    std::cerr << "worker timed out\n";
    changeState(DISCONNECTED);
  }
}

void Worker::changeState(StateEnum NewState) {
  if (NewState == State)
    return;

  if (State == WAITING_FOR_JOB) {
    ServiceSets[SSN].WaitingWorkers.remove(*this);
  }
  if (State == WAITING_FOR_RESULT) {
    assert(ClientContext);
    if (ClientContext->State == Context::JOB_PROCESSING) {
      ClientContext->reset();
      ClientContext = nullptr;
    }
  }

  State = NewState;

  if (State == WAITING_FOR_RESULT)
    assert(ClientContext);
  else
    ClientContext = nullptr;

  if (State == WAITING_FOR_JOB)
    assert(WorkerContext);
  else
    WorkerContext = nullptr;

  if (State == WAITING_FOR_HEARTBEAT)
    startTimeout(WORKER_TIMEOUT);
  if (State == WAITING_FOR_RESULT)
    startTimeout(ClientContext->JobTimeout);
  if (State == WAITING_FOR_JOB) {
    ServiceSets[SSN].WaitingWorkers.push_back(*this);
    startTimeout(WORKER_HEARTBEAT_TIME);
  }
}

Node Worker::encodeID() {
  return Node(byte_string_arg, StringRef((const char *)&ID, sizeof(ID)));
}

Worker *Worker::getById(const Node &ID) {
  if (ID.kind() != Kind::Bytes)
    return nullptr;
  auto IDBytes = ID.as<llvm::ArrayRef<std::uint8_t>>(byte_string_arg);
  if (IDBytes.size() != sizeof(WorkerID))
    return nullptr;
  WorkerID i;
  memcpy(&i, IDBytes.data(), sizeof(WorkerID));
  i -= FirstWorkerID;
  if (i >= Workers.size())
    return nullptr;
  if (Workers[i].State == DISCONNECTED)
    return nullptr;
  return &Workers[i];
}

void Worker::handleRequest(Context *Context) {
  // Called after any type of worker request that we can respond to with a job.
  assert(State != DISCONNECTED);
  WorkerContext = Context;
  WorkerContext->changeState(Context::WORKER_WAITING);

  for (ServiceNumber SN : ServiceSets[SSN].Services) {
    if (!::Services[SN].WaitingClients.empty()) {
      ::Context *Client = &::Services[SN].WaitingClients.front();
      if (Client->State == Context::JOB_QUEUED) {
        Client->cancelTimeout();
        return startJob(Client);
      }
    }
  }

  // No appropriate jobs waiting.
  changeState(WAITING_FOR_JOB);
}

void Worker::handleResult(ArrayRef<uint8_t> Payload) {
  assert(ClientContext);
  assert(ClientContext->State == Context::JOB_PROCESSING);
  ClientContext->send(
      Node(node_list_arg,
           {"memo01", 0x03, Node(byte_string_arg, StringRef(""))}),
      Payload);
  changeState(INIT);
}

void Worker::sendHeartbeat() {
  assert(State == WAITING_FOR_JOB);
  assert(WorkerContext);
  assert(WorkerContext->State == Context::WORKER_WAITING);
  WorkerContext->send(Node(node_list_arg, {"memo01", 0x04, encodeID()}));
  changeState(WAITING_FOR_HEARTBEAT);
}

void Worker::startJob(Context *Client) {
  assert(WorkerContext);
  assert(WorkerContext->State == Context::WORKER_WAITING);

  WorkerContext->send(
      Node(node_list_arg,
           {"memo01", 0x02, encodeID(),
            Node(utf8_string_arg, Services[Client->JobService].Name),
            Client->JobTimeout}),
      Client->JobPayload);
  ClientContext = Client;
  ClientContext->changeState(Context::JOB_PROCESSING);
  changeState(WAITING_FOR_RESULT);
}

int main(int argc, char **argv) {
  InitTool X(argc, argv);

  cl::HideUnrelatedOptions(BrokerCategory);
  cl::ParseCommandLineOptions(argc, argv, "MemoDB Broker");

  GlobalSocket = nng::rep::v0::open();
  GlobalSocket.listen(ListenURL.c_str());
  GlobalContexts.resize(16);

  while (true) {
    nng::msleep(1000);
    std::lock_guard<std::mutex> Lock(GlobalMutex);
    auto NumContexts = GlobalContexts.size();
    size_t NumReceiving = 0, NumSending = 0, NumJobQueued = 0,
           NumWorkerWaiting = 0, NumJobProcessing = 0;
    for (Context &Context : GlobalContexts) {
      switch (Context.State) {
      case Context::RECEIVING:
        NumReceiving++;
        break;
      case Context::SENDING:
        NumSending++;
        break;
      case Context::JOB_QUEUED:
        NumJobQueued++;
        break;
      case Context::WORKER_WAITING:
        NumWorkerWaiting++;
        break;
      case Context::JOB_PROCESSING:
        NumJobProcessing++;
        break;
      }
    }
    std::cerr << NumContexts << " contexts: " << NumReceiving << " idle, "
              << NumSending << " sending, " << NumJobQueued << " queued jobs, "
              << NumWorkerWaiting << " waiting workers, " << NumJobProcessing
              << " jobs being processed\n";
    if (NumReceiving < 8)
      GlobalContexts.resize(NumContexts * 2);
  }

  return 0;
}
