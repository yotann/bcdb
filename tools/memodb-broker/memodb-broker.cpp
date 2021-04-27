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
 * callbacks from multiple threads simultaneously. FIXME: figure out how to
 * make this work without deadlock.
 */

#include <arpa/inet.h>
#include <deque>
#include <iostream>
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
  // TODO: timed out clients aren't removed from this queue until a worker
  // tries to use them, so the queue can grow indefinitely.
  std::queue<Context *> WaitingClients;
};
} // end anonymous namespace

namespace {
struct ServiceSet {
  std::vector<ServiceNumber> Services;
  // TODO: workers can be added multiple times to this queue, once each
  // heartbeat. They aren't removed until a client tries to use them, so the
  // queue can grow indefinitely.
  std::queue<Worker *> WaitingWorkers;
};
} // end anonymous namespace

/*
 * Each Context can handle one request at a time.
 *
 * States:
 * - RECEIVING: we have called nng::ctx::recv. Aio is waiting for a new
 *   request to arrive.
 * - SENDING: we have called nng::ctx::send. Aio is waiting for the outgoing
 *   reply to be queued into the socket.
 * - JOB_QUEUED: we have received a JOB request, but there were no available
 *   workers. We're waiting for a worker to become available. Aio is waiting
 *   until the request times out and we give up on finding a worker.
 * - JOB_PROCESSING: we have received a JOB request and forwarded it to the
 *   worker, and we're waiting for the worker to finish. Aio is idle.
 * - WORKER_WAITING: we have received a worker request, but there were no
 *   queued jobs. We're waiting for a job to arrive. Aio is idle.
 */
namespace {
struct Context {
  enum { RECEIVING, SENDING, JOB_QUEUED, JOB_PROCESSING, WORKER_WAITING } State;
  nng::ctx Ctx;
  nng::aio Aio;
  ServiceNumber ServiceNeeded;      // only if State == JOB_QUEUED
  ArrayRef<uint8_t> WaitingPayload; // only if State == JOB_QUEUED
  int64_t JobTimeout;               // only if State == JOB_QUEUED

  Context();
  void aioCallback();
  void disconnectWorker(const memodb_value &Id);
  void handleClientJob(ServiceNumber SN, int64_t Timeout,
                       ArrayRef<uint8_t> Payload);
  void handleMessage();
  void handleWorkerReady(const memodb_value &ServiceNames);
  void handleWorkerRequest(Worker *Worker);
  void invalidMessage();
  void reset();
  void send(const memodb_value &Header, ArrayRef<uint8_t> Payload = {});
};
} // end anonymous namespace

namespace {
struct Worker {
  enum {
    WAITING_FOR_JOB,
    WAITING_FOR_RESULT,
    WAITING_FOR_HEARTBEAT,
    DISCONNECTED
  } State = WAITING_FOR_JOB;

  WorkerID ID;

  // Which services are handled by this worker.
  ServiceSetNumber SSN;

  // If State is WAITING_FOR_JOB, the Context that has an outstanding request
  // from the worker.
  Context *WorkerContext = nullptr;

  // If State is WAITING_FOR_RESULT, the Context connected to the client that
  // requested the job.
  Context *ClientContext = nullptr;

  // If State is WAITING_FOR_JOB, sleeps until we need to reply to the worker
  // with HEARTBEAT. If State is WAITING_FOR_RESULT or WAITING_FOR_HEARTBEAT,
  // sleeps until the worker times out and we assume it's disconnected.
  nng::aio Aio;

  Worker(ServiceSetNumber SSN, WorkerID ID);
  void aioCallback();
  memodb_value encodeID();
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

// NNG may call our callback from multiple threads at once, but we only allow
// one thread to access the global structures at a time.
// FIXME: there may be deadlock if NNG calls two callbacks at once, and the one
// that gets the lock calls Aio.wait() to wait for the other callback to
// complete.
static std::mutex GlobalMutex;

static nng::socket GlobalSocket;

// Must be deque so pointers are not invalidated. Contexts are never deleted.
static std::deque<Context> GlobalContexts;
static size_t NumBusyContexts = 0; // Number of contexts not in state RECEIVING.

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
  // This happens when main() exits and closes the socket.
  if (Aio.result() == nng::error::closed)
    return;

  if (Aio.result() != nng::error::success) {
    std::cerr << "context error: " << nng::to_string(Aio.result()) << "\n";
    return;
  }

  std::lock_guard<std::mutex> Guard(GlobalMutex);
  if (State == RECEIVING) {
    handleMessage();
  } else if (State == SENDING) {
    NumBusyContexts--;
    State = RECEIVING;
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

void Context::disconnectWorker(const memodb_value &Id) {
  std::cerr << "disconnecting unknown worker " << Id << "\n";
  send(memodb_value::array({"memo01", 0x05, Id}));
}

void Context::handleClientJob(ServiceNumber SN, int64_t Timeout,
                              ArrayRef<uint8_t> Payload) {
  ServiceNeeded = SN;
  WaitingPayload = Payload;
  JobTimeout = Timeout;

  Service &Service = Services[SN];
  for (ServiceSetNumber SSN : Service.Sets) {
    while (!ServiceSets[SSN].WaitingWorkers.empty()) {
      Worker *Worker = ServiceSets[SSN].WaitingWorkers.back();
      ServiceSets[SSN].WaitingWorkers.pop();
      if (Worker->State == Worker::WAITING_FOR_JOB) {
        assert(Worker->SSN == SSN);
        Worker->Aio.cancel();
        Worker->Aio.wait();
        if (Worker->State == Worker::WAITING_FOR_JOB)
          return Worker->startJob(this);
      }
    }
  }

  // No appropriate workers waiting.
  Service.WaitingClients.push(this);
  NumBusyContexts++;
  State = JOB_QUEUED;
  std::cerr << "waiting JOB_QUEUE_TIMEOUT...\n";
  nng::sleep(JOB_QUEUE_TIMEOUT, Aio);
}

void Context::handleMessage() {
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
    return handleWorkerReady(ServiceSet);

  } else if (Operation == 0x02) { // client JOB
    if (Header.array_items().size() != 5 || !Id.empty() ||
        Header[3].type() != memodb_value::STRING ||
        Header[4].type() != memodb_value::INTEGER)
      return invalidMessage();
    return handleClientJob(lookupService(Header[3]), Header[4].as_integer(),
                           Data);

  } else if (Operation == 0x03) { // worker RESULT
    if (Header.array_items().size() != 3 || Id.empty())
      return invalidMessage();
    Worker *Worker = Worker::getById(Id);
    if (!Worker || Worker->State != Worker::WAITING_FOR_RESULT)
      return disconnectWorker(Id);
    Worker->handleResult(Data);
    if (Worker->State != Worker::DISCONNECTED)
      return handleWorkerRequest(Worker);

  } else if (Operation == 0x04) { // worker HEARTBEAT
    if (Header.array_items().size() != 3 || Id.empty() || !Data.empty())
      return invalidMessage();
    Worker *Worker = Worker::getById(Id);
    if (!Worker || Worker->State != Worker::WAITING_FOR_HEARTBEAT)
      return disconnectWorker(Id);
    return handleWorkerRequest(Worker);

  } else {
    std::cerr << "Unsupported operation " << Operation << "\n";
    return invalidMessage();
  }
}

void Context::handleWorkerReady(const memodb_value &ServiceNames) {
  if (ServiceNames.type() != memodb_value::ARRAY)
    return invalidMessage();
  for (const auto &Service : ServiceNames.array_items())
    if (Service.type() != memodb_value::STRING || Service.as_string().empty())
      return invalidMessage();
  // TODO: make sure items are in the correct order.

  std::cerr << "Worker with set " << ServiceNames << "\n";
  ServiceSetNumber SSN = lookupServiceSet(ServiceNames);
  std::cerr << "Set number: " << SSN << "\n";
  Workers.emplace_back(SSN, FirstWorkerID + Workers.size());
  return handleWorkerRequest(&Workers.back());
}

void Context::handleWorkerRequest(Worker *Worker) {
  // Called after any type of worker request that we can respond to with a job.

  // Cancel the worker-disconnected timeout.
  Worker->Aio.cancel();
  Worker->Aio.wait();
  if (Worker->State == Worker::DISCONNECTED)
    return;

  NumBusyContexts++;
  State = WORKER_WAITING;
  Worker->State = Worker::WAITING_FOR_JOB;
  Worker->WorkerContext = this;
  Aio.release_msg();

  ServiceSet &ServiceSet = ServiceSets[Worker->SSN];
  for (ServiceNumber SN : ServiceSet.Services) {
    while (!Services[SN].WaitingClients.empty()) {
      Context *Client = Services[SN].WaitingClients.back();
      Services[SN].WaitingClients.pop();
      if (Client->State == JOB_QUEUED && Client->ServiceNeeded == SN) {
        Client->Aio.cancel();
        Client->Aio.wait();
        if (Client->State == JOB_QUEUED)
          return Worker->startJob(Client);
      }
    }
  }

  // No appropriate jobs waiting.
  ServiceSet.WaitingWorkers.push(Worker);
  std::cerr << "waiting WORKER_HEARTBEAT_TIME...\n";
  nng::sleep(WORKER_HEARTBEAT_TIME, Worker->Aio);
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
  if (State != RECEIVING)
    NumBusyContexts--;
  Ctx = nng::ctx(GlobalSocket);
  State = RECEIVING;
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

  if (State == RECEIVING)
    NumBusyContexts++;
  State = SENDING;
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
    return;
  }

  std::lock_guard<std::mutex> Guard(GlobalMutex);
  if (State == WAITING_FOR_JOB) {
    sendHeartbeat();
  } else if (State == WAITING_FOR_RESULT) {
    assert(ClientContext);
    ClientContext->reset();
    std::cerr << "worker timed out\n";
    State = DISCONNECTED;
  } else if (State == WAITING_FOR_HEARTBEAT) {
    std::cerr << "worker timed out\n";
    State = DISCONNECTED;
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

void Worker::handleResult(ArrayRef<uint8_t> Payload) {
  Aio.cancel();
  Aio.wait();
  if (State != WAITING_FOR_RESULT)
    return;
  assert(ClientContext);
  assert(ClientContext->State == Context::JOB_PROCESSING);
  ClientContext->send(
      memodb_value::array({"memo01", 0x03, memodb_value::bytes()}), Payload);
  ClientContext = nullptr;
  State = WAITING_FOR_JOB;
}

void Worker::sendHeartbeat() {
  assert(State == WAITING_FOR_JOB);
  assert(WorkerContext);
  assert(WorkerContext->State == Context::WORKER_WAITING);

  WorkerContext->send(memodb_value::array({"memo01", 0x04, encodeID()}));
  WorkerContext = nullptr;
  State = WAITING_FOR_HEARTBEAT;
  // sendHeartbeat() is only called from the Aio callback, so we don't need to
  // cancel Aio.
  nng::sleep(WORKER_TIMEOUT, Aio);
}

void Worker::startJob(Context *Client) {
  assert(State == WAITING_FOR_JOB);
  assert(WorkerContext);
  assert(WorkerContext->State == Context::WORKER_WAITING);

  WorkerContext->send(
      memodb_value::array(
          {"memo01", 0x02, encodeID(),
           memodb_value::string(Services[Client->ServiceNeeded].Name),
           Client->JobTimeout}),
      Client->WaitingPayload);
  WorkerContext = nullptr;
  ClientContext = Client;
  if (ClientContext->State == Context::RECEIVING)
    NumBusyContexts++;
  ClientContext->State = Context::JOB_PROCESSING;
  ClientContext->Aio.release_msg(); // invalidates WaitingPayload
  State = WAITING_FOR_RESULT;
  nng::sleep(Client->JobTimeout, Aio);
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
    std::cerr << NumBusyContexts << " of " << NumContexts << " contexts busy\n";
    if (NumContexts - NumBusyContexts < 8)
      GlobalContexts.resize(NumContexts * 9 / 8);
  }

  return 0;
}
