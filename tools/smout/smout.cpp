#include <cstdlib>
#include <llvm/ADT/StringRef.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/CodeGen/MachineModuleInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/InitializePasses.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/FileUtilities.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/SystemUtils.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <memory>
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

static cl::SubCommand Alive2Command("alive2", "Check equivalence with Alive2");

static cl::SubCommand CandidatesCommand("candidates",
                                        "Generate outlineable candidates");

static cl::SubCommand CollateCommand("collate", "Organize candidates by type");

static cl::SubCommand MeasureCommand("measure",
                                     "Measure code size of candidates");

static cl::opt<std::string>
    ModuleName("name", cl::desc("Name of the head to work on"),
               cl::sub(Alive2Command), cl::sub(CandidatesCommand),
               cl::sub(CollateCommand), cl::sub(MeasureCommand));

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

static int Alive2() {
  ExitOnError Err("smout alive2: ");
  std::unique_ptr<BCDB> bcdb = Err(BCDB::Open(GetUri()));
  auto &memodb = bcdb->get_db();
  memodb_ref Root = memodb.head_get(ModuleName);
  memodb_value Collated = memodb.get(memodb.call_get("smout.collated", {Root}));

  std::string aliveTvPath =
      Err(errorOrToExpected(sys::findProgramByName("alive-tv")));

  int srcFDs[2];
  SmallVector<char, 32> srcPath, dstPath, stdoutPath, stderrPath;
  Err(errorCodeToError(
      sys::fs::createTemporaryFile("smout-src", "bc", srcFDs[0], srcPath)));
  // FileRemover srcRemover(srcPath);
  Err(errorCodeToError(
      sys::fs::createTemporaryFile("smout-dst", "bc", srcFDs[1], dstPath)));
  // FileRemover dstRemover(dstPath);
  Err(errorCodeToError(
      sys::fs::createTemporaryFile("smout-stdout", "txt", stdoutPath)));
  // FileRemover dstRemover(stdoutPath);
  Err(errorCodeToError(
      sys::fs::createTemporaryFile("smout-stderr", "txt", stderrPath)));
  // FileRemover dstRemover(stderrPath);

  StringRef srcPathStrs[2] = {
      StringRef(srcPath.data(), srcPath.size()),
      StringRef(dstPath.data(), dstPath.size()),
  };
  StringRef stdoutPathStr(stdoutPath.data(), stdoutPath.size());
  StringRef stderrPathStr(stderrPath.data(), stderrPath.size());

  errs() << "Using files " << srcPath << " and " << dstPath << "\n";
  errs() << "Saving output to " << stdoutPath << " and " << stderrPath << "\n";
  raw_fd_ostream srcStreams[2] = {
      {srcFDs[0], /* shouldClose */ true, /* unbuffered */ true},
      {srcFDs[1], /* shouldClose */ true, /* unbuffered */ true},
  };

  unsigned NumValid = 0, NumInvalid = 0, NumUnsupported = 0, NumCrash = 0,
           NumTimeout = 0, NumApprox = 0, NumTypeMismatch = 0;

  for (auto &GroupPair : Collated.map_items()) {
    auto &Group = GroupPair.second;
    for (unsigned i = 1; i < Group.array_items().size(); i++) {
      for (unsigned j = 0; j < i; j++) {
        memodb_ref SrcRefs[2] = {Group[i].as_ref(), Group[j].as_ref()};
        if (SrcRefs[1] < SrcRefs[0])
          std::swap(SrcRefs[0], SrcRefs[1]);
        memodb_ref RefinesRefs[2] = {
            memodb.call_get("refines", {SrcRefs[0], SrcRefs[1]}),
            memodb.call_get("refines", {SrcRefs[1], SrcRefs[0]}),
        };
        memodb_ref AliveRefs[2] = {
            memodb.call_get("refines.alive2", {SrcRefs[0], SrcRefs[1]}),
            memodb.call_get("refines.alive2", {SrcRefs[1], SrcRefs[0]}),
        };

        if (false) {
          // If we've already attempted each direction, or solved it with
          // something other than Alive2, skip this pair.
          if ((RefinesRefs[0] || AliveRefs[0]) &&
              (RefinesRefs[1] || AliveRefs[1]))
            continue;
        }

        errs() << "comparing " << Group[i] << " with " << Group[j] << "\n";

        for (int k = 0; k < 2; k++) {
          memodb_value Blob = memodb.get(SrcRefs[k]);
          srcStreams[k].seek(0);
          Err(errorCodeToError(
              sys::fs::resize_file(srcFDs[k], Blob.as_bytes().size())));
          srcStreams[k].write(
              reinterpret_cast<const char *>(Blob.as_bytes().data()),
              Blob.as_bytes().size());
          Err(errorCodeToError(srcStreams[k].error()));
        }

        for (int k = 0; k < 2; k++) {
          memodb_value AliveValue;
          int rc;
          StringRef stdoutString, stderrString;
          if (AliveRefs[k]) {
            AliveValue = memodb.get(AliveRefs[k]);
            rc = AliveValue["rc"].as_integer();
          } else {
            // TODO: measure execution time.
            Err(errorCodeToError(sys::fs::remove(stdoutPathStr)));
            Err(errorCodeToError(sys::fs::remove(stderrPathStr)));
            rc = sys::ExecuteAndWait(
                aliveTvPath,
                {"alive-tv", "--succinct", srcPathStrs[k], srcPathStrs[1 - k]},
                None, {None, stdoutPathStr, stderrPathStr});
            auto stdoutBuffer =
                Err(errorOrToExpected(MemoryBuffer::getFile(stdoutPathStr)));
            auto stderrBuffer =
                Err(errorOrToExpected(MemoryBuffer::getFile(stderrPathStr)));
            AliveValue = memodb_value::map(
                {{"rc", rc},
                 {"stdout", memodb_value::bytes(stdoutBuffer->getBuffer())},
                 {"stderr", memodb_value::bytes(stderrBuffer->getBuffer())}});
            memodb.call_set("refines.alive2", {SrcRefs[k], SrcRefs[1 - k]},
                            memodb.put(AliveValue));
          }
          stdoutString = AliveValue["stdout"].as_bytestring();
          stderrString = AliveValue["stderr"].as_bytestring();

          memodb_value RefinesValue;
          if (rc == 0 &&
              stdoutString.contains("Transformation seems to be correct!")) {
            RefinesValue = true;
            NumValid++;
          } else if (rc == 0 &&
                     stdoutString.contains("Transformation doesn't verify!")) {
            RefinesValue = false;
            NumInvalid++;
          } else if (rc == 1 &&
                     stderrString.contains("ERROR: Unsupported instruction:")) {
            NumUnsupported++;
          } else if (rc == 1 && stderrString.contains("ERROR: Timeout")) {
            NumTimeout++;
          } else if (rc == 1 &&
                     stderrString.contains(
                         "ERROR: Couldn't prove the correctness of the "
                         "transformation\nAlive2 approximated the semantics")) {
            NumApprox++;
          } else if (rc == 1 && stderrString.contains(
                                    "ERROR: program doesn't type check!")) {
            RefinesValue = false;
            NumTypeMismatch++;
          } else {
            errs() << AliveValue << "\n";
            errs() << "unknown result from alive-tv!\n";
            return 1;
          }
          if (RefinesValue != memodb_value{} && !RefinesRefs[k])
            memodb.call_set("refines", {SrcRefs[k], SrcRefs[1 - k]},
                            memodb.put(RefinesValue));
        }

        outs() << NumValid << " valid, " << NumInvalid << " invalid, "
               << NumTimeout << " timeouts, " << NumUnsupported
               << " unsupported, " << NumCrash << " crashes, " << NumApprox
               << " approximated, " << NumTypeMismatch << " don't type check\n";
      }
    }
  }

  return 0;
}

// smout candidates

static int Candidates() {
  ExitOnError Err("smout candidates: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));
  for (auto &FuncId : Err(db->ListFunctionsInModule(ModuleName))) {
    auto M = Err(db->GetFunctionById(FuncId));
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

    Err(db->Add("__outlined", std::move(M)));

    memodb_value Module = db->get_db().get(db->get_db().head_get("__outlined"));
    memodb_value Functions = Module.map_items()["functions"];

    memodb_value Result = memodb_value::array();
    for (size_t i = 0; i < NodesValues.size(); i++) {
      Result.array_items().push_back(memodb_value::map({
          {"nodes", std::move(NodesValues[i])},
          {"callee",
           std::move(
               Functions.map_items()[memodb_value::bytes(CalleeNames[i])])},
          {"caller",
           std::move(
               Functions.map_items()[memodb_value::bytes(CallerNames[i])])},
      }));
    }

    db->get_db().call_set("smout.candidates", {memodb_ref{FuncId}},
                          db->get_db().put(Result));
  }
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

static int Collate() {
  ExitOnError Err("smout collate: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));
  std::set<memodb_ref> all_candidates;
  size_t total_candidates = 0;
  for (auto &FuncId : Err(db->ListFunctionsInModule(ModuleName))) {
    memodb_ref candidates =
        db->get_db().call_get("smout.candidates", {memodb_ref{FuncId}});
    memodb_value candidates_value = db->get_db().get(candidates);
    for (const auto &item : candidates_value.array_items()) {
      all_candidates.insert(item.map_items().at("callee").as_ref());
      total_candidates++;
    }
  }
  outs() << "Number of unique candidates: " << all_candidates.size() << "\n";
  outs() << "Number of total candidates: " << total_candidates << "\n";

  memodb_value result = memodb_value::map();
  for (const memodb_ref &ref : all_candidates) {
    auto M = Err(db->GetFunctionById(StringRef(ref)));
    Function &Def = getSoleDefinition(*M);
    memodb_value key = memodb_value::array(
        {GroupForType(Def.getFunctionType()), GroupForGlobals(*M)});
    memodb_value &value = result[key];
    if (value.type() != memodb_value::ARRAY)
      value = memodb_value::array({ref});
    else
      value.array_items().push_back(ref);
  }

  // Erase groups with only a single element.
  size_t candidatesRemaining = 0, largestGroup = 0;
  for (auto it = result.map_items().begin(); it != result.map_items().end();) {
    if (it->second.array_items().size() < 2) {
      it = result.map_items().erase(it);
    } else {
      candidatesRemaining += it->second.array_items().size();
      largestGroup = std::max(largestGroup, it->second.array_items().size());
      it++;
    }
  }

  outs() << "Number of groups: " << result.map_items().size() << "\n";
  outs() << "Number of candidates that belong to a group: "
         << candidatesRemaining << "\n";
  outs() << "Largest group: " << largestGroup << "\n";
  db->get_db().call_set("smout.collated", {db->get_db().head_get(ModuleName)},
                        db->get_db().put(result));
  return 0;
}

// smout measure

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

  std::set<memodb_ref> all_funcs;
  for (auto &FuncId : Err(db->ListFunctionsInModule(ModuleName))) {
    all_funcs.insert(memodb_ref{FuncId});
    memodb_ref candidates =
        memodb.call_get("smout.candidates", {memodb_ref{FuncId}});
    memodb_value candidates_value = memodb.get(candidates);
    for (const auto &item : candidates_value.array_items()) {
      all_funcs.insert(item.map_items().at("callee").as_ref());
      all_funcs.insert(item.map_items().at("caller").as_ref());
    }
  }
  outs() << "Number of unique original functions, outlined callees, and "
            "outlined callers: "
         << all_funcs.size() << "\n";

  for (memodb_ref FuncId : all_funcs) {
    memodb_ref CompiledRef = memodb.call_get("compiled", {FuncId});
    if (CompiledRef)
      continue;

    auto M = Err(db->GetFunctionById(StringRef(FuncId)));

    std::string Error;
    const Target *TheTarget =
        TargetRegistry::lookupTarget(M->getTargetTriple(), Error);
    if (!TheTarget) {
      errs() << Error;
      return 1;
    }

    TargetOptions Options;
    std::unique_ptr<TargetMachine> Target(
        TheTarget->createTargetMachine(M->getTargetTriple(), "", "", Options,
                                       None, None, CodeGenOpt::Default));

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

    outs() << StringRef(FuncId) << " compiled to " << Buffer.size()
           << " bytes\n";
    memodb_value CompiledValue = memodb_value::bytes(Buffer);
    memodb.call_set("compiled", {FuncId}, memodb.put(CompiledValue));
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
  } else if (MeasureCommand) {
    return Measure();
  } else {
    cl::PrintHelpMessage(false, true);
    return 0;
  }
}
