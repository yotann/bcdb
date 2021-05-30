#include <cstdlib>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/CodeGen/MachineModuleInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/InitializePasses.h>
#include <llvm/Object/Binary.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/FileUtilities.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Parallel.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/SystemUtils.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/ThreadPool.h>
#include <llvm/Support/Threading.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <memory>
#include <mutex>
#include <nngpp/nngpp.h>
#include <nngpp/protocol/req0.h>
#include <set>
#include <string>

#include "bcdb/BCDB.h"
#include "bcdb/LLVMCompat.h"
#include "bcdb/Outlining/Candidates.h"
#include "bcdb/Outlining/Dependence.h"
#include "bcdb/Outlining/Extractor.h"
#include "bcdb/Split.h"
#include "memodb/memodb.h"

using namespace bcdb;
using namespace llvm;
using namespace memodb;

static cl::SubCommand Alive2Command("alive2", "Check equivalence with Alive2");

static cl::SubCommand CandidatesCommand("candidates",
                                        "Generate outlineable candidates");

static cl::SubCommand CollateCommand("collate", "Organize candidates by type");

static cl::SubCommand EstimateCommand("estimate",
                                      "Estimate benefit of outlining");

static cl::SubCommand MeasureCommand("measure",
                                     "Measure code size of candidates");

static cl::SubCommand ShowGroupsCommand("show-groups",
                                        "Print group information");

static cl::opt<std::string>
    Threads("j", cl::desc("Number of threads, or \"all\""),
            cl::sub(Alive2Command), cl::sub(CandidatesCommand),
            cl::sub(CollateCommand), cl::sub(MeasureCommand));

static cl::opt<unsigned> Timeout("timeout", cl::init(10000),
                                 cl::desc("Timeout for Alive2 jobs (ms)"),
                                 cl::sub(Alive2Command));

static cl::opt<std::string>
    BrokerURL("broker-url", cl::Required,
              cl::desc("URL of MemoDB broker to connect to"),
              cl::sub(Alive2Command));

static cl::opt<bool>
    IgnoreEquivalence("ignore-equivalence",
                      cl::desc("Ignore equivalence information"),
                      cl::sub(EstimateCommand));

static cl::opt<std::string>
    ModuleName("name", cl::desc("Name of the head to work on"),
               cl::sub(Alive2Command), cl::sub(CandidatesCommand),
               cl::sub(CollateCommand), cl::sub(EstimateCommand),
               cl::sub(MeasureCommand), cl::sub(ShowGroupsCommand));

static cl::opt<std::string> UriOrEmpty(
    "uri", cl::Optional, cl::desc("URI of the database"),
    cl::init(std::string(StringRef::withNullAsEmpty(std::getenv("BCDB_URI")))),
    cl::cat(BCDBCategory), cl::sub(*cl::AllSubCommands));

static StringRef GetUri() {
  if (UriOrEmpty.empty()) {
    report_fatal_error(
        "You must provide a database URI, such as sqlite:/tmp/example.bcdb, "
        "using the -uri option or the BCDB_URI environment variable.");
  }
  return UriOrEmpty;
}

// smout alive2

static nng::socket BrokerSocket;

static memodb_value evaluate_refines_alive2(memodb_db &db,
                                            const memodb_value &AliveSettings,
                                            const memodb_value &src,
                                            const memodb_value &tgt) {
  // Allow the job to take 10x the time of the SMT timeout, in case there are
  // many SMT queries.
  auto JobTimeout = AliveSettings["timeout"].as_integer() + 30000;
  memodb_value Header = memodb_value::array(
      {"memo01", 0x02, memodb_value::bytes(), "alive.tv", JobTimeout});

  memodb_value Job = AliveSettings;
  Job["src"] = src;
  Job["tgt"] = tgt;

  std::vector<uint8_t> Request;
  Header.save_cbor(Request);
  Job.save_cbor(Request);

  nng::ctx Ctx(BrokerSocket);
  nng::aio Aio(nullptr, nullptr);
  auto Msg = nng::make_msg(0);
  nng::req::v0::set_opt_resend_time(Ctx, JobTimeout + 30000);
  Msg.body().append({Request.data(), Request.size()});
  Aio.set_msg(std::move(Msg));

  Ctx.send(Aio);
  Aio.wait();
  if (Aio.result() != nng::error::success) {
    errs() << "broker send error: " << nng::to_string(Aio.result()) << "\n";
    report_fatal_error("broker send failed");
  }

  // TODO: measure execution time.

  Ctx.recv(Aio);
  Aio.wait();
  if (Aio.result() != nng::error::success) {
    errs() << "broker receive error: " << nng::to_string(Aio.result()) << "\n";
    report_fatal_error("broker receive failed");
  }

  Msg = Aio.release_msg();
  llvm::ArrayRef<uint8_t> Reply(Msg.body().data<uint8_t>(), Msg.body().size());

  Header = memodb_value::load_cbor_from_sequence(Reply);
  if (Header.type() != memodb_value::ARRAY || Header.array_items().size() < 3 ||
      Header[0] != "memo01" || Header[1] != 0x03) {
    errs() << "received invalid reply header: " << Header << "\n";
    report_fatal_error("invalid reply header");
  }

  memodb_value Result = memodb_value::load_cbor(Reply);
  if (Result.type() != memodb_value::ARRAY || Result.array_items().size() < 2 ||
      Result[0] != 0) {
    errs() << "received error from worker: " << Result << "\n";
    report_fatal_error("error from worker");
  }

  return Result[1][""]["forward"];
}

static int Alive2() {
  ExitOnError Err("smout alive2: ");
  std::unique_ptr<BCDB> bcdb = Err(BCDB::Open(GetUri()));
  auto &memodb = bcdb->get_db();
  CID Root = memodb.head_get(ModuleName);
  memodb_value Collated =
      memodb.get(memodb.get(memodb_call("smout.collated", {Root})).as_ref());

  BrokerSocket = nng::req::v0::open();
  BrokerSocket.dial(BrokerURL.c_str());

  CID AliveSettings = memodb.put(memodb_value::map({
      {"smt-to", 2 * (unsigned)Timeout},
      {"timeout", (unsigned)Timeout},
  }));

  std::vector<std::pair<CID, CID>> AllPairs;
  for (auto &GroupPair : Collated.map_items()) {
    auto &Group = GroupPair.second;
    for (unsigned i = 1; i < Group.array_items().size(); i++) {
      for (unsigned j = 0; j < i; j++) {
        AllPairs.emplace_back(Group[i].as_ref(), Group[j].as_ref());
        AllPairs.emplace_back(Group[j].as_ref(), Group[i].as_ref());
      }
    }
  }

  std::atomic<unsigned> NumValid = 0, NumInvalid = 0, NumUnsupported = 0,
                        NumCrash = 0, NumTimeout = 0, NumMemout = 0,
                        NumApprox = 0, NumTypeMismatch = 0, NumTotal = 0,
                        NumIdentical = 0, NumOldFailed = 0;
  std::atomic<unsigned> ProgressReported = 0;
  std::mutex PrintMutex;

  auto reportProgress = [&] {
    ProgressReported = NumTotal.load();
    outs() << "Progress: " << NumTotal << "/" << AllPairs.size() << ", ";
    outs() << NumValid << " valid, " << NumInvalid << " invalid, "
           << NumIdentical << " identical, " << NumTimeout << " timeouts, "
           << NumMemout << " memouts, " << NumUnsupported << " unsupported, "
           << NumCrash << " crashes, " << NumApprox << " approximated, "
           << NumTypeMismatch << " don't type check, " << NumOldFailed
           << " old failures\n";
  };

  auto Transform = [&](const std::pair<CID, CID> &Pair) {
    auto RefinesRef =
        memodb.getOptional(memodb_call("refines", {Pair.first, Pair.second}));
    if (RefinesRef) {
      if (memodb.get(RefinesRef->as_ref()).as_bool())
        NumValid++;
      else
        NumInvalid++;
      NumTotal++;
      return;
    }

    memodb_value AliveValue =
        memodb.call_or_lookup_value("refines.alive2", evaluate_refines_alive2,
                                    AliveSettings, Pair.first, Pair.second);
    if (AliveValue.map_items().count("rc")) {
      // An old result from when we were starting Alive2 as a separate process.
      NumOldFailed++;
      NumTotal++;
      return;
    }

    memodb_value RefinesValue = AliveValue["valid"];
    StringRef status = AliveValue["status"].as_string();
    StringRef stderrString;
    if (AliveValue.map_items().count("errs"))
      stderrString = AliveValue["errs"].as_string();

    if (RefinesValue == true && status == "CORRECT" && stderrString.empty()) {
      NumValid++;
    } else if (RefinesValue == true && status == "SYNTACTIC_EQ" &&
               stderrString.empty()) {
      NumIdentical++;
    } else if (RefinesValue == false && status == "UNSOUND") {
      NumInvalid++;
    } else if (RefinesValue == memodb_value{} &&
               status == "COULD_NOT_TRANSLATE") {
      NumUnsupported++;
    } else if (RefinesValue == memodb_value{} && status == "TIMEOUT") {
      NumTimeout++;
    } else if (RefinesValue == memodb_value{} && status == "FAILED_TO_PROVE" &&
               stderrString.contains("ERROR: Timeout")) {
      NumTimeout++;
    } else if (RefinesValue == memodb_value{} && status == "FAILED_TO_PROVE" &&
               stderrString.contains("ERROR: SMT Error: smt tactic failed to "
                                     "show goal to be sat/unsat memout")) {
      NumMemout++;
    } else if (RefinesValue == memodb_value{} && status == "FAILED_TO_PROVE" &&
               stderrString.contains(
                   "ERROR: Out of memory; skipping function.")) {
      NumMemout++;
    } else if (RefinesValue == memodb_value{} && status == "FAILED_TO_PROVE" &&
               stderrString.contains(
                   "ERROR: Couldn't prove the correctness of the "
                   "transformation\nAlive2 approximated the semantics")) {
      NumApprox++;
    } else if (RefinesValue == false && status == "TYPE_CHECKER_FAILED") {
      NumTypeMismatch++;
    } else if (RefinesValue == memodb_value{} && status == "CRASHED") {
      NumCrash++;
    } else if (RefinesValue == memodb_value{} && status == "FAILED_TO_PROVE" &&
               stderrString.contains(
                   "ERROR: SMT Error: interrupted from keyboard")) {
      NumCrash++;
    } else {
      errs() << "comparing " << StringRef(Pair.first) << " with "
             << StringRef(Pair.second) << "\n";
      errs() << AliveValue << "\n";
      report_fatal_error("unknown result from alive-tv!");
    }
    NumTotal++;

    if (RefinesValue != memodb_value{})
      memodb.call_set("refines", {Pair.first, Pair.second},
                      memodb.put(RefinesValue));

    if (NumTotal >= ProgressReported + 64) {
      const std::lock_guard<std::mutex> Lock(PrintMutex);
      if (NumTotal >= ProgressReported + 64)
        reportProgress();
    }
  };

  Optional<ThreadPoolStrategy> strategyOrNone =
      get_threadpool_strategy(Threads);
  if (!strategyOrNone) {
    report_fatal_error("invalid number of threads");
    return 1;
  }
  parallel::strategy = *strategyOrNone;
  parallelForEach(AllPairs, Transform);

  reportProgress();
  return 0;
}

// smout candidates

static memodb_value evaluate_candidates(memodb_db &db,
                                        const memodb_value &func) {
  ExitOnError Err("smout candidates evaluator: ");
  BCDB bcdb(db);

  auto M = Err(parseBitcodeFile(MemoryBufferRef(func.as_bytestring(), ""),
                                bcdb.GetContext()));
  getSoleDefinition(*M); // check that there's only one definition

  legacy::PassManager PM;
  PM.add(new OutliningExtractorWrapperPass());
  PM.run(*M);

  // Make sure all functions are named so we can track them after saving.
  nameUnamedGlobals(*M);

  SmallVector<memodb_value, 8> NodesValues;
  SmallVector<std::string, 8> CalleeNames;
  SmallVector<std::string, 8> CallerNames;

  NamedMDNode *NMD = M->getNamedMetadata("smout.extracted.functions");
  if (NMD && NMD->getNumOperands()) {
    for (auto &Candidate :
         cast<MDNode>(*NMD->getOperand(0)->getOperand(1)).operands()) {
      // [Nodes, callee, caller]
      MDNode &Node = cast<MDNode>(*Candidate);
      ConstantDataSequential &NodesArray = cast<ConstantDataSequential>(
          *cast<ConstantAsMetadata>(*Node.getOperand(0)).getValue());
      Constant *Callee =
          cast<ConstantAsMetadata>(*Node.getOperand(1)).getValue();
      Constant *Caller =
          cast<ConstantAsMetadata>(*Node.getOperand(2)).getValue();
      if (Callee->isNullValue() || Caller->isNullValue())
        continue; // ignore unsupported sequences

      CalleeNames.push_back(Callee->getName().str());
      CallerNames.push_back(Caller->getName().str());
      memodb_value NodesValue = memodb_value::array();
      for (size_t i = 0; i < NodesArray.getNumElements(); i++)
        NodesValue.array_items().push_back(NodesArray.getElementAsInteger(i));
      NodesValues.push_back(std::move(NodesValue));
    }
  }

  memodb_value Module = db.get(Err(bcdb.AddWithoutHead(std::move(M))));
  memodb_value Functions = Module.map_items()["functions"];

  memodb_value Result = memodb_value::array();
  for (size_t i = 0; i < NodesValues.size(); i++) {
    Result.array_items().push_back(memodb_value::map({
        {"nodes", std::move(NodesValues[i])},
        {"callee",
         std::move(Functions.map_items()[bytesToUTF8(CalleeNames[i])])},
        {"caller",
         std::move(Functions.map_items()[bytesToUTF8(CallerNames[i])])},
    }));
  }
  return Result;
}

static int Candidates() {
  ExitOnError Err("smout candidates: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));
  auto OriginalFunctions = Err(db->ListFunctionsInModule(ModuleName));

  size_t TotalInputs = OriginalFunctions.size();
  std::atomic<size_t> FinishedInputs = 0, TotalCandidates = 0;
  StringSet ActiveInputs;
  std::mutex PrintMutex;

  auto PrintJobs = [&] {
    size_t PrintActiveInputs = ActiveInputs.size(),
           PrintFinishedInputs = FinishedInputs;
    size_t PrintUnstartedInputs =
        TotalInputs - PrintActiveInputs - PrintFinishedInputs;
    errs() << PrintUnstartedInputs << "->" << PrintActiveInputs << "->"
           << PrintFinishedInputs << ':';
    for (const auto &ActiveInput : ActiveInputs)
      errs() << ' ' << ActiveInput.getKey();
    errs() << ' ';
  };

  auto Transform = [&](StringRef FuncId) {
    {
      const std::lock_guard<std::mutex> Lock(PrintMutex);
      ActiveInputs.insert(FuncId);
      PrintJobs();
      errs() << "starting " << FuncId << "\n";
    }

    memodb_value value = db->get_db().call_or_lookup_value(
        "smout.candidates", evaluate_candidates, *CID::parse(FuncId));
    size_t result = value.array_items().size();
    TotalCandidates += value.array_items().size();

    {
      const std::lock_guard<std::mutex> Lock(PrintMutex);
      PrintJobs();
      errs() << "finished " << FuncId << ": " << result << " candidates\n";
      FinishedInputs++;
      ActiveInputs.erase(FuncId);
    }
  };

  parallel::strategy = heavyweight_hardware_concurrency(Threads);
  parallelForEach(OriginalFunctions, Transform);
  errs() << "Total candidates: " << TotalCandidates << "\n";
  return 0;
}

// smout collate: organize candidates by type and globals

static memodb_value GroupForType(Type *T) {
  memodb_value result = memodb_value::array({(int)T->getTypeID()});
  if (T->isIntegerTy()) {
    result.array_items().push_back(T->getIntegerBitWidth());
  } else if (T->isArrayTy()) {
    result.array_items().push_back(T->getArrayNumElements());
  } else if (T->isVectorTy()) {
#if LLVM_VERSION_MAJOR >= 11
    result.array_items().push_back(isa<FixedVectorType>(T));
    if (FixedVectorType *FVT = dyn_cast<FixedVectorType>(T))
      result.array_items().push_back(FVT->getNumElements());
    else
      result.array_items().push_back(
          cast<ScalableVectorType>(T)->getMinNumElements());
#else
    result.array_items().push_back(T->getVectorNumElements());
#endif
  } else if (T->isStructTy()) {
    StructType *ST = cast<StructType>(T);
    result.array_items().push_back(ST->isOpaque());
    result.array_items().push_back(ST->isPacked());
    result.array_items().push_back(ST->isLiteral());
  } else if (T->isFunctionTy()) {
    result.array_items().push_back(T->isFunctionVarArg());
  } else if (T->isPointerTy()) {
    result.array_items().push_back(T->getPointerAddressSpace());
    // Ignore the type pointed to!
    // This prevents infinite recursion from recursive types.
    return result;
  }
  for (Type *Sub : T->subtypes())
    result.array_items().push_back(GroupForType(Sub));
  return result;
}

static memodb_value GroupForGlobals(Module &M) {
  memodb_value result = memodb_value::array();
  for (GlobalVariable &GV : M.globals())
    if (GV.hasName())
      result.array_items().push_back(memodb_value::bytes(GV.getName()));
  for (Function &F : M.functions())
    if (F.hasName() && !F.isIntrinsic() &&
        F.getName() != "__gxx_personality_v0")
      result.array_items().push_back(memodb_value::bytes(F.getName()));
  std::sort(result.array_items().begin(), result.array_items().end());
  return result;
}

template <class ContainerTy, class ResultTy, class ReduceFuncTy,
          class TransformFuncTy>
ResultTy parallel_transform_reduce(ContainerTy Container, ResultTy Init,
                                   ReduceFuncTy Reduce,
                                   TransformFuncTy Transform) {
  ResultTy Result = Init;
  std::mutex Mutex;
  Optional<ThreadPoolStrategy> strategyOrNone =
      get_threadpool_strategy(Threads);
  if (!strategyOrNone)
    report_fatal_error("invalid number of threads");
  ThreadPool pool(*strategyOrNone);
  size_t NumTasks = std::min<size_t>(4 * strategyOrNone->compute_thread_count(),
                                     Container.size());
  if (!NumTasks)
    return Init;
  size_t TaskSize = Container.size() / NumTasks;
  size_t RemainingInputs = Container.size() % NumTasks;
  size_t IBegin = 0;
  for (size_t i = 0; i < NumTasks; i++) {
    size_t IEnd = IBegin + TaskSize + (i < RemainingInputs ? 1 : 0);
    pool.async([IBegin, IEnd, &Container, &Mutex, &Result, &Init, &Transform,
                &Reduce] {
      ResultTy ThreadResult = Init;
      for (size_t I = IBegin; I != IEnd; I++)
        ThreadResult = Reduce(ThreadResult, Transform(Container[I]));
      const std::lock_guard<std::mutex> Lock(Mutex);
      Result = Reduce(Result, ThreadResult);
    });
    IBegin = IEnd;
  }
  assert(IBegin == Container.size());
  pool.wait();
  return Result;
}

// Exclude candidates that would make the caller larger; they can never be
// profitable.
static memodb_value evaluate_profitable(memodb_db &db,
                                        const memodb_value &func) {
  CID FuncId = db.put(func);
  memodb_value candidates =
      db.get(db.get(memodb_call("smout.candidates", {FuncId})).as_ref());
  memodb_value result = memodb_value::array();
  memodb_value orig_size =
      db.get(db.get(memodb_call("compiled.size", {FuncId})).as_ref());
  for (const auto &item : candidates.array_items()) {
    CID caller = item.map_items().at("caller").as_ref();
    memodb_value caller_size =
        db.get(db.get(memodb_call("compiled.size", {caller})).as_ref());
    if (caller_size < orig_size)
      result.array_items().push_back(item);
  }
  return result;
}

static int Collate() {
  ExitOnError Err("smout collate: ");
  std::unique_ptr<BCDB> bcdb = Err(BCDB::Open(GetUri()));
  memodb_db &db = bcdb->get_db();
  auto AllFunctions = Err(bcdb->ListFunctionsInModule(ModuleName));

  std::atomic<size_t> TotalCandidates = 0, TotalProfitable = 0;
  auto TransformProfitable = [&](StringRef FuncId) {
    memodb_value candidates =
        db.get(db.get(memodb_call("smout.candidates", {*CID::parse(FuncId)}))
                   .as_ref());
    memodb_value profitable =
        db.call_or_lookup_value("smout.candidates.profitable",
                                evaluate_profitable, *CID::parse(FuncId));
    TotalCandidates += candidates.array_items().size();
    TotalProfitable += profitable.array_items().size();
    StringSet Result;
    for (const auto &item : profitable.array_items()) {
      CID callee = item.map_items().at("callee").as_ref();
      Result.insert(StringRef(callee));
    }
    return Result;
  };
  auto ReduceProfitable = [](StringSet<> A, StringSet<> B) {
    for (auto &Item : B) {
      A.insert(Item.getKey());
    }
    return A;
  };
  StringSet UniqueCandidates = parallel_transform_reduce(
      AllFunctions, StringSet(), ReduceProfitable, TransformProfitable);
  outs() << "Number of total candidates: " << TotalCandidates << "\n";
  outs() << "Number of potentially profitable candidates: " << TotalProfitable
         << "\n";
  outs() << "Number of unique candidates: " << UniqueCandidates.size() << "\n";

  std::vector<CID> UniqueCandidatesVec;
  for (auto &Item : UniqueCandidates)
    UniqueCandidatesVec.push_back(*CID::parse(Item.getKey()));
  auto Reduce = [](memodb_value A, memodb_value B) {
    for (auto &Item : B.map_items()) {
      memodb_value &value = A[Item.first];
      if (value.type() != memodb_value::ARRAY)
        value = Item.second;
      else
        for (auto &X : Item.second.array_items())
          value.array_items().push_back(X);
    }
    return A;
  };
  auto Transform = [&db, &Err](const CID &ref) {
    LLVMContext Context;
    auto M = Err(parseBitcodeFile(
        MemoryBufferRef(db.get(ref).as_bytestring(), StringRef(ref)), Context));
    Function &Def = getSoleDefinition(*M);
    memodb_value Key = memodb_value::array(
        {GroupForType(Def.getFunctionType()), GroupForGlobals(*M)});
    std::vector<std::uint8_t> KeyBytes;
    Key.save_cbor(KeyBytes);
    return memodb_value::map(
        {{bytesToUTF8(KeyBytes), memodb_value::array({ref})}});
  };
  memodb_value Groups = parallel_transform_reduce(
      UniqueCandidatesVec, memodb_value::map(), Reduce, Transform);
  outs() << "Number of groups: " << Groups.map_items().size() << "\n";

  // Erase groups with only a single element.
  size_t candidatesRemaining = 0, largestGroup = 0;
  for (auto it = Groups.map_items().begin(); it != Groups.map_items().end();) {
    auto size = it->second.array_items().size();
    largestGroup = std::max(largestGroup, size);
    if (size < 2) {
      it = Groups.map_items().erase(it);
    } else {
      candidatesRemaining += size;
      it++;
    }
  }
  outs() << "Number of nontrivial groups: " << Groups.map_items().size()
         << "\n";
  outs() << "Number of candidates that belong to a nontrivial group: "
         << candidatesRemaining << "\n";
  outs() << "Largest group: " << largestGroup << "\n";

  db.call_set("smout.collated", {db.head_get(ModuleName)}, db.put(Groups));

  return 0;
}

// smout estimate

static int Estimate() {
  ExitOnError Err("smout estimate: ");

  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));
  auto &memodb = db->get_db();

  auto compiled_size = [&](CID ref) -> size_t {
    return memodb.get(memodb.get(memodb_call("compiled.size", {ref})).as_ref())
        .as_integer();
  };

  // Number of cases where the outlined caller is larger than the original
  // function.
  unsigned NeverProfitableCount = 0;

  // Number of cases where outlining is profitable even with only one caller.
  unsigned AlwaysProfitableCount = 0;

  // Number of normal cases where outlining will be profitable, but only if
  // enough callers use the same outlined function.
  unsigned SometimesProfitableCount = 0;

  // Total compiled size of the original functions.
  unsigned TotalOrigSize = 0;

  // The more copies of a function there are in the original, the more savings
  // we get by outlining code from it.
  StringMap<unsigned> FunctionUseCount;
  for (auto &FuncId : Err(db->ListFunctionsInModule(ModuleName))) {
    FunctionUseCount[FuncId]++;
  }

  /*
   * We create an ILP (integer linear programming) problem of the following
   * form:
   *
   * Variable X[i] is 1 if candidate i should be outlined, using the outlined
   * caller function i as a replacement for the corresponding original
   * function. The solution may outline multiple candidates from the same
   * original function.
   *
   * Variable Z[m] is 1 if outlined callee function m, specifically, should be
   * included in the output program.
   *
   * Conflicts constraint: if candidates i,j,k all outline the same instruction
   * from the same original function, they are mutually exclusive. So X[i] +
   * X[j] + X[k] <= 1.
   *
   * Needs constraint: if candidate i is outlined, and the corresponding callee
   * function is m, and m,n,o are the functions equivalent to m, then at least
   * one of m,n,o must be included. So Z[m] + Z[n] + Z[o] - X[i] >= 0.
   *
   * Goal: minimize the total size of the output program, considering the
   * benefits of each candidate outlined (each X[i]) and the cost of each
   * callee function that must be included (each Z[i]).
   */

  // Variable names for each caller/candidate i and callee m.
  std::vector<std::string> CallerVarNames;
  std::vector<std::string> CalleeVarNames;

  // Code size reduction by outlining each candidate i, and code size increase
  // by including each callee function m.
  std::vector<unsigned> CallerSavings;
  std::vector<unsigned> CalleeSizes;

  // Map from each caller/candidate i to the corresponding callee m.
  std::vector<unsigned> CallerToCallee;

  // Map from each callee m to the corresponding callee(s)/candidate(s) i.
  std::vector<SmallVector<unsigned, 4>> CalleeToCaller;

  // Map from each callee m to the other callees that it can substitute for.
  std::vector<SmallVector<unsigned, 4>> RefinedCallees;

  // Map from each caller/candidate i to a list of conflict groups it belongs
  // to. If two callers/candidates share a value in CallerNodeConflicts, they
  // are mutually exclusive.
  std::vector<SmallVector<unsigned, 4>> CallerNodeConflicts;
  unsigned NextNodeConflictIndex = 0;

  StringMap<unsigned> CalleeIndexMap;
  auto findOrAddCallee = [&](const CID &ref) {
    auto inserted = CalleeIndexMap.insert(
        std::make_pair(StringRef(ref), CalleeIndexMap.size()));
    unsigned i = inserted.first->second;
    if (inserted.second) {
      CalleeSizes.push_back(compiled_size(ref));
      RefinedCallees.push_back({i}); // callee i refines itself
      CalleeToCaller.push_back({});
      CalleeVarNames.push_back(std::string(StringRef(ref)));
    }
    return inserted.first->second;
  };

  for (const auto &FunctionUseEntry : FunctionUseCount) {
    auto FuncId = FunctionUseEntry.getKey();
    unsigned UseCount = FunctionUseEntry.second;

    auto OrigSizeRef =
        memodb.getOptional(memodb_call("compiled.size", {*CID::parse(FuncId)}));
    if (!OrigSizeRef)
      continue;
    auto CandidatesRef = memodb.getOptional(
        memodb_call("smout.candidates", {*CID::parse(FuncId)}));
    if (!CandidatesRef)
      continue;
    size_t OrigSize = memodb.get(OrigSizeRef->as_ref()).as_integer();
    TotalOrigSize += UseCount * OrigSize;
    memodb_value Candidates = memodb.get(CandidatesRef->as_ref());

    // For each instruction/node in the original function, a list of the
    // callers/candidates that would outline that node. Used to determine
    // conflicts.
    std::vector<SmallVector<unsigned, 4>> NodeUses;

    for (memodb_value &Candidate : Candidates.array_items()) {
      CID CalleeRef = Candidate["callee"].as_ref();
      CID CallerRef = Candidate["caller"].as_ref();
      auto CalleeSize = compiled_size(CalleeRef);
      auto CallerSize = compiled_size(CallerRef);
      if (CallerSize >= OrigSize) {
        NeverProfitableCount++;
        continue;
      } else if (UseCount * CallerSize + CalleeSize < UseCount * OrigSize) {
        AlwaysProfitableCount++;
      } else {
        SometimesProfitableCount++;
      }

      unsigned caller_i = CallerVarNames.size();
      CallerSavings.push_back(UseCount * (OrigSize - CallerSize));
      CallerNodeConflicts.push_back({});
      CallerVarNames.push_back(formatv("func{0}_caller{1}_callee{2}_i{3}",
                                       FuncId, CallerRef, CalleeRef, caller_i));
      unsigned callee_m = findOrAddCallee(CalleeRef);
      CallerToCallee.push_back(callee_m);
      CalleeToCaller[callee_m].push_back(caller_i);

      for (const memodb_value &Node : Candidate["nodes"].array_items()) {
        unsigned i = Node.as_integer();
        if (NodeUses.size() <= i)
          NodeUses.resize(i + 1);
        NodeUses[i].push_back(caller_i);
      }
    }

    // Create conflict groups from sets of candidates that outline the same
    // node.
    for (size_t NodeI = 0; NodeI < NodeUses.size(); NodeI++) {
      const auto &Uses = NodeUses[NodeI];
      if (Uses.size() <= 1)
        continue;
      if (NodeI > 0 && Uses == NodeUses[NodeI - 1])
        continue;
      unsigned I = NextNodeConflictIndex++;
      for (unsigned caller_i : Uses)
        CallerNodeConflicts[caller_i].push_back(I);
    }
  }

  // Callees up to NumCalleesUsedDirectly need both Y and Z variables, because
  // they are directly used by one of the candidates. Callees beyond that only
  // need Z variables, because they are only used as substitutes for another
  // callee.
  size_t NumCalleesUsedDirectly = CalleeToCaller.size();

  // Find callees that are equivalent to the ones we're already considering.
  if (!IgnoreEquivalence) {
    memodb_value Collated = memodb.get(
        memodb.get(memodb_call("smout.collated", {memodb.head_get(ModuleName)}))
            .as_ref());
    for (auto &GroupPair : Collated.map_items()) {
      auto &Group = GroupPair.second;
      for (const memodb_value &FirstValue : Group.array_items()) {
        CID FirstRef = FirstValue.as_ref();
        auto callee_it = CalleeIndexMap.find(StringRef(FirstRef));
        if (callee_it == CalleeIndexMap.end())
          continue;
        if (callee_it->second >= NumCalleesUsedDirectly)
          continue;
        unsigned callee_m = callee_it->second;
        for (const memodb_value &SecondValue : Group.array_items()) {
          if (FirstValue == SecondValue)
            continue;
          CID SecondRef = SecondValue.as_ref();
          auto RefinesRef =
              memodb.getOptional(memodb_call("refines", {FirstRef, SecondRef}));
          if (!RefinesRef)
            continue;
          if (memodb.get(RefinesRef->as_ref()) != memodb_value{true})
            continue;
          unsigned refined_m = findOrAddCallee(SecondRef);
          assert(callee_m != refined_m);
          RefinedCallees[refined_m].push_back(callee_m);
        }
      }
    }
  }

  // Determine which callees and callers could possibly be beneficial.
  std::vector<bool> CallerUseful(CallerSavings.size());
  std::vector<bool> CalleeUseful(CalleeSizes.size());
  std::vector<size_t> ConflictGroupUsage(NextNodeConflictIndex);
  size_t NumCallerUseful = 0, NumCalleeUseful = 0;
  for (size_t CalleeI = 0; CalleeI < CalleeSizes.size(); CalleeI++) {
    size_t Savings = 0;
    for (auto RefinedI : RefinedCallees[CalleeI])
      for (auto CallerI : CalleeToCaller[RefinedI])
        Savings += CallerSavings[CallerI];
    if (Savings > CalleeSizes[CalleeI]) {
      NumCalleeUseful++;
      CalleeUseful[CalleeI] = true;
      for (auto RefinedI : RefinedCallees[CalleeI]) {
        if (RefinedI >= NumCalleesUsedDirectly)
          continue;
        for (auto CallerI : CalleeToCaller[RefinedI]) {
          assert(CallerI < CallerUseful.size());
          assert(CallerI < CallerSavings.size());
          if (!CallerUseful[CallerI]) {
            NumCallerUseful++;
            CallerUseful[CallerI] = true;
            for (auto ConflictI : CallerNodeConflicts[CallerI])
              ConflictGroupUsage[ConflictI]++;
          }
        }
      }
    }
  }

  outs() << "* Original size: " << TotalOrigSize << "\n";
  outs() << "* Out of " << (NeverProfitableCount + CallerSavings.size())
         << " callers:\n";
  outs() << "* - " << NeverProfitableCount << " never profitable\n";
  outs() << "* - " << AlwaysProfitableCount << " always profitable\n";
  outs() << "* - " << (NumCallerUseful - AlwaysProfitableCount)
         << " profitable thanks to duplication\n";
  outs() << "* - " << (CallerSavings.size() - NumCallerUseful)
         << " unprofitable due to lack of duplication\n";
  outs() << "* " << NumCalleeUseful << " of " << CalleeSizes.size()
         << " considered callees potentially useful\n";

  // Print the ILP problem (as described above) in free MPS format. Free MPS
  // format (described in the GLPK manual) is supported by most ILP solvers,
  // such as Symphony, GLPK, and lp_solve. Symphony is recommended for fastest
  // solving.
  //
  // symphony -F out.mps -a 0 > out.sol
  // glpsol --pcost --cuts --min out.mps -o out.sol
  // lp_solve -B3 -Bc -R -min -fmps out.mps > out.sol

  outs() << "NAME SMOUT\n";
  outs() << "ROWS\n";
  outs() << " N SIZE\n";
  for (unsigned a = 0; a < NextNodeConflictIndex; a++)
    if (ConflictGroupUsage[a] >= 2)
      outs() << " L CONFLICTS" << a << "\n";
  for (unsigned i = 0; i < CallerVarNames.size(); i++)
    if (CallerUseful[i])
      outs() << " G Z_NEEDED_BY_X" << CallerVarNames[i] << "\n";
  outs() << "COLUMNS\n";
  for (unsigned i = 0; i < CallerVarNames.size(); i++) {
    if (!CallerUseful[i])
      continue;
    outs() << " X" << CallerVarNames[i] << " SIZE " << -(int)CallerSavings[i]
           << "\n";
    outs() << " X" << CallerVarNames[i] << " Z_NEEDED_BY_X" << CallerVarNames[i]
           << " -1\n";
    for (unsigned conflict_a : CallerNodeConflicts[i])
      if (ConflictGroupUsage[conflict_a] >= 2)
        outs() << " X" << CallerVarNames[i] << " CONFLICTS" << conflict_a
               << " 1\n";
  }
  for (unsigned m = 0; m < RefinedCallees.size(); m++) {
    if (!CalleeUseful[m])
      continue;
    outs() << " Z" << CalleeVarNames[m] << " SIZE " << CalleeSizes[m] << "\n";
    for (unsigned callee_m : RefinedCallees[m])
      if (callee_m < NumCalleesUsedDirectly)
        for (unsigned caller_i : CalleeToCaller[callee_m])
          if (CallerUseful[caller_i])
            outs() << " Z" << CalleeVarNames[m] << " Z_NEEDED_BY_X"
                   << CallerVarNames[caller_i] << " 1\n";
  }
  outs() << "RHS\n";
  for (unsigned a = 0; a < NextNodeConflictIndex; a++)
    if (ConflictGroupUsage[a] >= 2)
      outs() << " RHS1 CONFLICTS" << a << " 1\n";
  // All variables are boolean.
  outs() << "BOUNDS\n";
  for (unsigned i = 0; i < CallerVarNames.size(); i++)
    if (CallerUseful[i])
      outs() << " BV BND1 X" << CallerVarNames[i] << "\n";
  for (unsigned m = 0; m < CalleeVarNames.size(); m++)
    if (CalleeUseful[m])
      outs() << " BV BND1 Z" << CalleeVarNames[m] << "\n";
  outs() << "ENDATA\n";

  return 0;
}

// smout measure

static memodb_value evaluate_compiled(memodb_db &db, const memodb_value &func) {
  ExitOnError Err("smout compiled evaluator: ");
  LLVMContext Context;

  auto M =
      Err(parseBitcodeFile(MemoryBufferRef(func.as_bytestring(), ""), Context));

  std::string Error;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(M->getTargetTriple(), Error);
  if (!TheTarget) {
    errs() << Error;
    report_fatal_error("Can't lookup target triple.");
  }

  TargetOptions Options;
  std::unique_ptr<TargetMachine> Target(TheTarget->createTargetMachine(
      M->getTargetTriple(), "", "", Options, None, None, CodeGenOpt::Default));

  SmallVector<char, 0> Buffer;
  raw_svector_ostream OS(Buffer);

  legacy::PassManager PM;
  TargetLibraryInfoImpl TLII(Triple(M->getTargetTriple()));
  PM.add(new TargetLibraryInfoWrapperPass(TLII));
  LLVMTargetMachine &LLVMTM = static_cast<LLVMTargetMachine &>(*Target);
#if LLVM_VERSION_MAJOR >= 10
  MachineModuleInfoWrapperPass *MMIWP =
      new MachineModuleInfoWrapperPass(&LLVMTM);
  bool error = Target->addPassesToEmitFile(PM, OS, nullptr, CGFT_ObjectFile,
                                           true, MMIWP);
#else
  MachineModuleInfo *MMI = new MachineModuleInfo(&LLVMTM);
  bool error = Target->addPassesToEmitFile(
      PM, OS, nullptr, TargetMachine::CGFT_ObjectFile, true, MMI);
#endif
  if (error) {
    errs() << "can't compile to an object file\n";
    return 1;
  }

  PM.run(*M);
  assert(Buffer.size() > 0);
  return memodb_value::bytes(Buffer);
}

static memodb_value evaluate_compiled_size(memodb_db &db,
                                           const memodb_value &func) {
  using namespace object;
  ExitOnError Err("smout compiled size evaluator: ");
  CID FuncId = db.put(func);
  memodb_value Compiled =
      db.call_or_lookup_value("compiled", evaluate_compiled, FuncId);
  std::string FuncStr = FuncId;
  MemoryBufferRef MB(Compiled.as_bytestring(), FuncStr);
  auto Binary = Err(createBinary(MB));
  if (ObjectFile *Obj = dyn_cast<ObjectFile>(Binary.get())) {
    memodb_value::integer_t Size = 0;
    for (const SectionRef &Section : Obj->sections()) {
      if (Section.isText() || Section.isData() || Section.isBSS() ||
          Section.isBerkeleyText() || Section.isBerkeleyData())
        Size += Section.getSize();
    }
    return Size;
  }
  report_fatal_error("Invalid object file");
}

static int Measure() {
  ExitOnError Err("smout measure: ");

  // based on llvm/tools/llc/llc.cpp

  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));
  auto &memodb = db->get_db();

  // It's okay if there are duplicates in this list; the BCDB will cache the
  // compilation result.
  std::vector<CID> all_funcs;
  for (auto &FuncId : Err(db->ListFunctionsInModule(ModuleName))) {
    all_funcs.push_back(*CID::parse(FuncId));
    CID candidates =
        memodb.get(memodb_call("smout.candidates", {*CID::parse(FuncId)}))
            .as_ref();
    memodb_value candidates_value = memodb.get(candidates);
    for (const auto &item : candidates_value.array_items()) {
      all_funcs.push_back(item.map_items().at("callee").as_ref());
      all_funcs.push_back(item.map_items().at("caller").as_ref());
    }
  }
  outs() << "Number of unique original functions, outlined callees, and "
            "outlined callers: "
         << all_funcs.size() << "\n";

  std::atomic<size_t> InProgress = 0, FinishedInputs = 0;
  size_t TotalInputs = all_funcs.size();
  std::mutex OutputMutex;

  auto Transform = [&](CID FuncId) {
    InProgress++;
    memodb_value Size = db->get_db().call_or_lookup_value(
        "compiled.size", evaluate_compiled_size, FuncId);
    size_t Pending = TotalInputs - InProgress - FinishedInputs;
    std::unique_lock<std::mutex> OutputLock(OutputMutex, std::try_to_lock);
    if (OutputLock) {
      outs() << Pending << "->" << InProgress << "->" << FinishedInputs << ": "
             << FuncId << ": compiled to " << Size << " bytes\n";
    }
    FinishedInputs++;
    InProgress--;
  };

  Optional<ThreadPoolStrategy> strategyOrNone =
      get_threadpool_strategy(Threads);
  if (!strategyOrNone) {
    report_fatal_error("invalid number of threads");
    return 1;
  }
  parallel::strategy = *strategyOrNone;
  parallelForEach(all_funcs, Transform);

  return 0;
}

// show-groups

static int ShowGroups() {
  ExitOnError Err("smout show-groups: ");
  std::unique_ptr<BCDB> bcdb = Err(BCDB::Open(GetUri()));
  auto &memodb = bcdb->get_db();
  CID Root = memodb.head_get(ModuleName);
  memodb_value Collated =
      memodb.get(memodb.get(memodb_call("smout.collated", {Root})).as_ref());

  std::vector<std::pair<size_t, memodb_value>> GroupCounts;
  for (const auto &Item : Collated.map_items()) {
    GroupCounts.emplace_back(Item.second.array_items().size(), Item.first);
  }

  std::sort(GroupCounts.begin(), GroupCounts.end(),
            [](const auto &a, const auto &b) { return b < a; });
  for (const auto &Item : GroupCounts) {
    outs() << Item.first << " " << Item.second << "\n";
  }

  return 0;
}

// main

int main(int argc, char **argv) {
  PrettyStackTraceProgram StackPrinter(argc, argv);
  sys::PrintStackTraceOnErrorSignal(argv[0]);

  // Reorganize options into subcommands.
  ReorganizeOptions([](cl::Option *O) {
    if (OptionHasCategory(*O, BCDBCategory)) {
      O->addSubCommand(*cl::AllSubCommands);
    } else {
      // Hide LLVM's options, since they're mostly irrelevant.
      O->setHiddenFlag(cl::Hidden);
      O->addSubCommand(*cl::AllSubCommands);
    }
  });
  cl::ParseCommandLineOptions(argc, argv, "Semantic Outlining");

  if (Alive2Command) {
    return Alive2();
  } else if (CandidatesCommand) {
    return Candidates();
  } else if (CollateCommand) {
    return Collate();
  } else if (EstimateCommand) {
    return Estimate();
  } else if (MeasureCommand) {
    return Measure();
  } else if (ShowGroupsCommand) {
    return ShowGroups();
  } else {
    cl::PrintHelpMessage(false, true);
    return 0;
  }
}
