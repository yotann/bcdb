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
 *  - The current solution: Call nng::aio::wait after cancelling the timeout in
 *    step 3, so the timeout callback finishes. We need to use two global
 *    mutexes: one used to ensure only one message is processed at once, and
 *    one to ensure only one thread accesses global structures at a time. The
 *    latter mutex is temporarily unlocked while calling nng::aio::wait so the
 *    timeout callback can lock it.
 *  - Use different callback functions or callback pointers depending on the
 *    current state of the object, so the timeout callback can figure out the
 *    object is no longer in the right state. Requires multiple nng::aio
 *    instances, and might not work in 100% of cases.
 *  - Check a clock in the timeout callback to determine whether the right
 *    timeout has passed. Could lead to shenanigans when the system clock
 *    changes, or if NNG's idea of time is slightly different from the clock's.
 */

#include <arpa/inet.h>
#include <deque>
#include <iostream>
#include <llvm/ADT/ilist_node.h>
#include <llvm/ADT/simple_ilist.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <map>
#include <mutex>
#include <nngpp/nngpp.h>
#include <nngpp/platform/platform.h>
#include <nngpp/protocol/rep0.h>
#include <queue>
#include <string>
#include <utility>

#include "memodb/memodb.h"

using namespace llvm;

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

namespace {
struct Context;
struct Worker;
} // end anonymous namespace

typedef size_t ServiceNumber;
typedef size_t ServiceSetNumber;

namespace {
struct Service {
  std::string Name;
  std::vector<ServiceSetNumber> Sets;
  simple_ilist<Context> WaitingClients;
  Worker *findPossibleWorker();
};
} // end anonymous namespace

namespace {
struct ServiceSet {
  std::vector<ServiceNumber> Services;
  simple_ilist<Worker> WaitingWorkers;
  Context *findPossibleClient();
};
} // end anonymous namespace

/*
 * Each Context can handle one request at a time.
 *
 * .Context states (AsciiDoc format)
 * |====
 * |State|Means...|Aio is...|We can be accessed by...|Valid fields
 *
 * |RECEIVING
 * |Called Ctx.recv()
 * |Waiting for new request
 * |Our callback
 * |Ctx, Aio
 *
 * |SENDING
 * |Called Ctx.send()
 * |Waiting for reply to be queued
 * |Our callback
 * |Ctx, Aio
 *
 * |JOB_QUEUED
 * |Received JOB, but no workers were available
 * |Sleeping until JOB times out and we give up on finding a worker
 * |Our callback or an arbitrary worker context's callback
 * |Ctx, Aio, JobService, JobPayload, JobTimeout
 *
 * |WORKER_WAITING
 * |Received request from worker, but no jobs were available
 * |--
 * |Our worker's timeout callback or an arbitrary client context's callback
 * |Ctx, Aio
 *
 * |JOB_PROCESSING
 * |Forwarded our JOB to a worker, waiting for RESULT
 * |--
 * |The worker context's callback or the worker's timeout callback
 * |Ctx, Aio
 *
 * |====
 */
namespace {
struct Context : ilist_node<Context> {
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
  void aioCallback();
  void changeState(StateEnum NewState);
  void disconnectWorker(const memodb_value &Id);
  Worker *findWorkerExpectedState(std::unique_lock<std::mutex> *Lock,
                                  const memodb_value &Id, int ExpectedState);
  void handleClientJob(std::unique_lock<std::mutex> Lock, ServiceNumber SN,
                       int64_t Timeout, ArrayRef<uint8_t> Payload);
  void handleMessage(std::unique_lock<std::mutex> Lock);
  void handleWorkerReady(std::unique_lock<std::mutex> Lock,
                         const memodb_value &ServiceNames);
  void invalidMessage();
  void reset();
  void send(const memodb_value &Header, ArrayRef<uint8_t> Payload = {});
};
} // end anonymous namespace

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
 * |State|Means...|Aio is...|We can be accessed by...|Valid fields
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
namespace {
struct Worker : ilist_node<Worker> {
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

  // Must never be deleted; this is accessed outside the global mutex.
  nng::aio Aio;

  // If State is WAITING_FOR_JOB, the Context that has an outstanding request
  // from the worker.
  Context *WorkerContext = nullptr;

  // If State is WAITING_FOR_RESULT, the Context connected to the client that
  // requested the job.
  Context *ClientContext = nullptr;

  Worker(ServiceSetNumber SSN, WorkerID ID);
  void aioCallback();
  void changeState(StateEnum NewState);
  memodb_value encodeID();
  void handleRequest(std::unique_lock<std::mutex> Lock, Context *Context);
  void handleResult(ArrayRef<uint8_t> Payload);
  void sendHeartbeat();
  void startJob(Context *Client);

  static Worker *getById(const memodb_value &ID);
};
} // end anonymous namespace

// We use a different random set of worker IDs for each broker process. We can
// check the worker ID in each request to determine if the worker actually got
// its job from a different broker process (which may have died and restarted).
static WorkerID FirstWorkerID = nng::random();

static std::mutex MessageMutex, GlobalMutex;

static nng::socket GlobalSocket;

// Must be deque so pointers are not invalidated. Contexts are never deleted.
static std::deque<Context> GlobalContexts;

// Must be deque so pointers are not invalidated. Workers are never deleted,
// even after they disconnect.
static std::deque<Worker> Workers;

static std::vector<Service> Services;
static std::vector<ServiceSet> ServiceSets;
static std::map<memodb_value, size_t> ServiceNumbers;
static std::map<memodb_value, size_t> ServiceSetNumbers;

static ServiceNumber lookupService(const memodb_value &Name) {
  auto Entry = ServiceNumbers.insert({Name, Services.size()});
  if (Entry.second)
    Services.push_back({Name.as_string()});
  return Entry.first->second;
}

static ServiceSetNumber lookupServiceSet(const memodb_value &Names) {
  auto Entry = ServiceSetNumbers.insert({Names, ServiceSets.size()});
  if (Entry.second) {
    ServiceSet NewSet;
    for (const auto &Name : Names.array_items()) {
      ServiceNumber SN = lookupService(Name);
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
  if (Aio.result() == nng::error::closed) {
    // This happens when main() exits and closes the socket.
    return;
  }

  if (Aio.result() != nng::error::success) {
    std::cerr << "context error: " << nng::to_string(Aio.result()) << "\n";
    // TODO: error handling
    return;
  }

  std::unique_lock<std::mutex> Lock(GlobalMutex);
  if (State == RECEIVING) {
    // We must unlock GlobalMutex to prevent deadlock with another thread that
    // has locked MessageMutex but not GlobalMutex. It's okay to unlock
    // GlobalMutex because, when State == RECEIVING, no other thread can
    // possibly access this Context.
    Lock.unlock();
    std::unique_lock<std::mutex> MessageLock(MessageMutex);
    Lock.lock();
    handleMessage(std::move(Lock));
  } else if (State == SENDING) {
    changeState(RECEIVING);
    Ctx.recv(Aio);
  } else if (State == JOB_QUEUED) {
    std::cerr << "job timed out in queue\n";
    reset();
  } else if (State == JOB_PROCESSING) {
    report_fatal_error("Aio should not be triggered in JOB_PROCESSING");
  } else if (State == WORKER_WAITING) {
    report_fatal_error("Aio should not be triggered in WORKER_WAITING");
  } else {
    llvm_unreachable("impossible Context state");
  }
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
    nng::sleep(JOB_QUEUE_TIMEOUT, Aio);
  }
}

void Context::disconnectWorker(const memodb_value &Id) {
  std::cerr << "disconnecting unknown worker " << Id << "\n";
  send(memodb_value::array({"memo01", 0x05, Id}));
}

Worker *Context::findWorkerExpectedState(std::unique_lock<std::mutex> *Lock,
                                         const memodb_value &Id,
                                         int ExpectedState) {
  Worker *Worker = Worker::getById(Id);
  if (!Worker)
    return nullptr;
  Lock->unlock();
  Worker->Aio.cancel();
  Worker->Aio.wait();
  Lock->lock();
  if (Worker->State != ExpectedState) {
    Worker->changeState(Worker::DISCONNECTED);
    return nullptr;
  }
  return Worker;
}

Worker *Service::findPossibleWorker() {
  for (ServiceSetNumber SSN : Sets)
    if (!ServiceSets[SSN].WaitingWorkers.empty())
      return &ServiceSets[SSN].WaitingWorkers.front();
  return nullptr;
}

Context *ServiceSet::findPossibleClient() {
  for (ServiceNumber SN : Services)
    if (!::Services[SN].WaitingClients.empty())
      return &::Services[SN].WaitingClients.front();
  return nullptr;
}

void Context::handleClientJob(std::unique_lock<std::mutex> Lock,
                              ServiceNumber SN, int64_t Timeout,
                              ArrayRef<uint8_t> Payload) {
  JobService = SN;
  JobPayload = Payload;
  JobTimeout = Timeout;

  while (true) {
    Worker *Worker = Services[SN].findPossibleWorker();
    if (!Worker)
      break;
    // XXX: Be careful not to use any references to volatile global structures,
    // like Services[SN], after releasing the lock.
    std::cerr << "worker " << Worker << " cancelling...\n";
    Lock.unlock();
    Worker->Aio.cancel();
    Worker->Aio.wait();
    Lock.lock();
    std::cerr << "worker " << Worker << " cancelled\n";
    if (Worker->State == Worker::WAITING_FOR_JOB)
      return Worker->startJob(this);
  }

  // No appropriate workers waiting.
  changeState(JOB_QUEUED);
}

void Context::handleMessage(std::unique_lock<std::mutex> Lock) {
  auto Msg = Aio.get_msg();
  ArrayRef<uint8_t> Data(reinterpret_cast<const uint8_t *>(Msg.body().data()),
                         Msg.body().size());
  // FIXME: handle CBOR errors using exceptions, don't just abort.
  memodb_value Header = memodb_value::load_cbor_from_sequence(Data);
  std::cerr << "received " << Header << ", " << Data.size()
            << " byte payload\n";

  if (Header.type() != memodb_value::ARRAY || Header.array_items().size() < 3 ||
      Header[0] != "memo01" || Header[1].type() != memodb_value::INTEGER ||
      Header[2].type() != memodb_value::BYTES)
    return invalidMessage();

  auto Operation = Header[1].as_integer();
  const auto &Id = Header[2].as_bytes();

  if (Operation == 0x01) { // worker READY
    if (Header.array_items().size() != 4 || !Id.empty() || !Data.empty())
      return invalidMessage();
    const auto &ServiceSet = Header[3];
    return handleWorkerReady(std::move(Lock), ServiceSet);

  } else if (Operation == 0x02) { // client JOB
    if (Header.array_items().size() != 5 || !Id.empty() ||
        Header[3].type() != memodb_value::STRING ||
        Header[4].type() != memodb_value::INTEGER)
      return invalidMessage();
    return handleClientJob(std::move(Lock), lookupService(Header[3]),
                           Header[4].as_integer(), Data);

  } else if (Operation == 0x03) { // worker RESULT
    if (Header.array_items().size() != 3 || Id.empty())
      return invalidMessage();
    Worker *Worker =
        findWorkerExpectedState(&Lock, Id, Worker::WAITING_FOR_RESULT);
    if (!Worker)
      return disconnectWorker(Id);
    Worker->handleResult(Data);
    return Worker->handleRequest(std::move(Lock), this);

  } else if (Operation == 0x04) { // worker HEARTBEAT
    if (Header.array_items().size() != 3 || Id.empty() || !Data.empty())
      return invalidMessage();
    Worker *Worker =
        findWorkerExpectedState(&Lock, Id, Worker::WAITING_FOR_HEARTBEAT);
    if (!Worker)
      return disconnectWorker(Id);
    return Worker->handleRequest(std::move(Lock), this);

  } else {
    std::cerr << "Unsupported operation " << Operation << "\n";
    return invalidMessage();
  }
}

void Context::handleWorkerReady(std::unique_lock<std::mutex> Lock,
                                const memodb_value &ServiceNames) {
  if (ServiceNames.type() != memodb_value::ARRAY)
    return invalidMessage();
  StringRef PrevService = "";
  for (const auto &Service : ServiceNames.array_items()) {
    if (Service.type() != memodb_value::STRING || Service.as_string().empty())
      return invalidMessage();
    // Check for correct order.
    if (Service.as_string().size() < PrevService.size())
      return invalidMessage();
    if (Service.as_string().size() == PrevService.size() &&
        Service.as_string() <= PrevService)
      return invalidMessage();
    PrevService = Service.as_string();
  }

  std::cerr << "Worker with set " << ServiceNames << "\n";
  ServiceSetNumber SSN = lookupServiceSet(ServiceNames);
  std::cerr << "Set number: " << SSN << "\n";
  Workers.emplace_back(SSN, FirstWorkerID + Workers.size());
  return Workers.back().handleRequest(std::move(Lock), this);
}

void Context::invalidMessage() {
  auto RemoteAddr = Aio.get_msg().get_pipe().get_opt_addr(
      nng::to_name(nng::option::remote_address));
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

void Context::send(const memodb_value &Header, ArrayRef<uint8_t> Payload) {
  std::vector<uint8_t> Bytes;
  Header.save_cbor(Bytes);
  std::cerr << "sending " << Header << ", " << Payload.size()
            << " byte payload\n";

  nng::msg Msg(Bytes.size() + Payload.size());
  memcpy(Msg.body().data(), Bytes.data(), Bytes.size());
  memcpy(reinterpret_cast<char *>(Msg.body().data()) + Bytes.size(),
         Payload.data(), Payload.size());
  Aio.set_msg(std::move(Msg));

  changeState(SENDING);
  Ctx.send(Aio);
}

static void workerAioCallback(void *Opaque) {
  reinterpret_cast<Worker *>(Opaque)->aioCallback();
}

Worker::Worker(ServiceSetNumber SSN, WorkerID ID)
    : ID(ID), SSN(SSN), Aio(::workerAioCallback, this) {}

void Worker::aioCallback() {
  std::cerr << "worker callback: " << nng::to_string(Aio.result()) << "\n";

  if (Aio.result() == nng::error::canceled)
    return;

  if (Aio.result() != nng::error::success) {
    std::cerr << "worker error: " << nng::to_string(Aio.result()) << "\n";
    // TODO: error handling
    return;
  }

  std::lock_guard<std::mutex> Guard(GlobalMutex);
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

  std::cerr << "worker " << this << " changing " << State << " -> " << NewState
            << "\n";
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
    nng::sleep(WORKER_TIMEOUT, Aio);
  if (State == WAITING_FOR_RESULT)
    nng::sleep(ClientContext->JobTimeout, Aio);
  if (State == WAITING_FOR_JOB) {
    std::cerr << "waiting WORKER_HEARTBEAT_TIME...\n";
    ServiceSets[SSN].WaitingWorkers.push_back(*this);
    nng::sleep(WORKER_HEARTBEAT_TIME, Aio);
  }
}

memodb_value Worker::encodeID() {
  return memodb_value::bytes(StringRef((const char *)&ID, sizeof(ID)));
}

Worker *Worker::getById(const memodb_value &ID) {
  if (ID.type() != memodb_value::BYTES)
    return nullptr;
  if (ID.as_bytes().size() != sizeof(WorkerID))
    return nullptr;
  WorkerID i;
  memcpy(&i, ID.as_bytes().data(), sizeof(WorkerID));
  i -= FirstWorkerID;
  if (i >= Workers.size())
    return nullptr;
  if (Workers[i].State == DISCONNECTED)
    return nullptr;
  return &Workers[i];
}

void Worker::handleRequest(std::unique_lock<std::mutex> Lock,
                           Context *Context) {
  // Called after any type of worker request that we can respond to with a job.
  assert(State != DISCONNECTED);
  WorkerContext = Context;
  WorkerContext->changeState(Context::WORKER_WAITING);

  while (true) {
    ::Context *Client = ServiceSets[SSN].findPossibleClient();
    if (!Client)
      break;
    // XXX: Be careful not to use any references to volatile global structures,
    // like ServiceSets[SSN], after releasing the lock.
    Lock.unlock();
    Client->Aio.cancel();
    Client->Aio.wait();
    Lock.lock();
    if (Client->State == Context::JOB_QUEUED)
      return startJob(Client);
  }

  // No appropriate jobs waiting.
  changeState(WAITING_FOR_JOB);
}

void Worker::handleResult(ArrayRef<uint8_t> Payload) {
  assert(ClientContext);
  assert(ClientContext->State == Context::JOB_PROCESSING);
  ClientContext->send(
      memodb_value::array({"memo01", 0x03, memodb_value::bytes()}), Payload);
  changeState(INIT);
}

void Worker::sendHeartbeat() {
  assert(State == WAITING_FOR_JOB);
  assert(WorkerContext);
  assert(WorkerContext->State == Context::WORKER_WAITING);
  WorkerContext->send(memodb_value::array({"memo01", 0x04, encodeID()}));
  changeState(WAITING_FOR_HEARTBEAT);
}

void Worker::startJob(Context *Client) {
  assert(WorkerContext);
  assert(WorkerContext->State == Context::WORKER_WAITING);

  WorkerContext->send(
      memodb_value::array(
          {"memo01", 0x02, encodeID(),
           memodb_value::string(Services[Client->JobService].Name),
           Client->JobTimeout}),
      Client->JobPayload);
  ClientContext = Client;
  ClientContext->changeState(Context::JOB_PROCESSING);
  changeState(WAITING_FOR_RESULT);
}

int main(int argc, char **argv) {
  PrettyStackTraceProgram StackPrinter(argc, argv);
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  cl::HideUnrelatedOptions(BrokerCategory);
  cl::ParseCommandLineOptions(argc, argv, "MemoDB Broker");

  GlobalSocket = nng::rep::v0::open();
  GlobalSocket.listen(ListenURL.c_str());
  GlobalContexts.resize(16);

  while (true) {
    nng::msleep(500);
    std::lock_guard<std::mutex> Guard(GlobalMutex);
    auto NumContexts = GlobalContexts.size();
    size_t NumBusyContexts = 0;
    for (Context &Context : GlobalContexts)
      if (Context.State != Context::RECEIVING)
        NumBusyContexts++;
    std::cerr << NumBusyContexts << " of " << NumContexts << " contexts busy\n";
    if (NumContexts - NumBusyContexts < 8)
      GlobalContexts.resize(NumContexts * 2);
  }

  return 0;
}
