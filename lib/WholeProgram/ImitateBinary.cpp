#include "bcdb/WholeProgram.h"

#include <algorithm>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Object/ELFObjectFile.h>
#include <llvm/Support/Error.h>

#include "bcdb/AlignBitcode.h"

using namespace bcdb;
using namespace llvm;
using namespace llvm::object;

// TODO: support other binary formats
// TODO: update symbol linkage types to match the binary
// TODO: detect additional symbols in the binary that came from assembly files

// Extract a module from an ELF file created with "clang -fembed-bitcode".
template <class ELFT>
static std::unique_ptr<Module>
ExtractModuleFromELF(LLVMContext &Context, const ELFObjectFile<ELFT> &ELF) {
  ExitOnError Err("ExtractModuleFromELF: ");
  std::unique_ptr<Module> M =
      std::make_unique<Module>(ELF.getFileName(), Context);
  Linker Linker(*M);

  for (auto &Section : ELF.sections()) {
    auto Name = Err(Section.getName());
    if (Name.startswith(".llvmbc")) {
      StringRef Contents = Err(Section.getContents());
      // When ELF files containing .llvmbc sections are linked, the .llvmbc
      // sections are concatenated, possibly with padding bytes between them.
      while (!Contents.empty()) {
        size_t Size = GetBitcodeSize(MemoryBufferRef(Contents, ".llvmbc"));
        MemoryBufferRef Buffer(Contents.substr(0, Size), ".llvmbc");
        Contents = Contents.substr(Size).ltrim('\x00');

        std::unique_ptr<Module> MPart = Err(parseBitcodeFile(Buffer, Context));
        Linker.linkInModule(std::move(MPart));
      }
    }
  }
  return M;
}

template <class ELFT>
static bool AnnotateModuleWithELF(Module &M, const ELFObjectFile<ELFT> &ELF) {
  ExitOnError Err("AnnotateModuleWithELF: ");

  auto DynamicEntries = Err(ELF.getELFFile()->dynamicEntries());
  auto toMappedAddr = [&](uint64_t VAddr) -> const uint8_t * {
    return Err(ELF.getELFFile()->toMappedAddr(VAddr));
  };

  const char *StringTableBegin = nullptr;
  uint64_t StringTableSize = 0;
  for (auto &Dyn : DynamicEntries) {
    if (Dyn.d_tag == ELF::DT_STRTAB)
      StringTableBegin =
          reinterpret_cast<const char *>(toMappedAddr(Dyn.getPtr()));
    if (Dyn.d_tag == ELF::DT_STRSZ)
      StringTableSize = Dyn.getVal();
  }
  if (!StringTableBegin)
    return false;

  M.addModuleFlag(Module::Warning, "bcdb.elf.type",
                  ELF.getELFFile()->getHeader()->e_type);

  SmallVector<Metadata *, 8> Needed;
  for (auto &Dyn : DynamicEntries) {
    auto getDynamicString = [&]() -> StringRef {
      uint64_t Value = Dyn.getVal();
      if (StringTableBegin && Value < StringTableSize)
        return StringTableBegin + Value;
      return {};
    };
    auto splitRunpath = [&](StringRef Name) {
      SmallVector<StringRef, 8> Dirs;
      getDynamicString().split(Dirs, ':', /* MaxSplit */ -1,
                               /* KeepEmpty */ false);
      if (!Dirs.empty()) {
        SmallVector<Metadata *, 8> MDs;
        for (StringRef Dir : Dirs)
          MDs.push_back(MDString::get(M.getContext(), Dir));
        M.addModuleFlag(Module::AppendUnique, Name,
                        MDTuple::get(M.getContext(), MDs));
      }
    };

    switch (Dyn.d_tag) {
    case ELF::DT_NEEDED:
      Needed.push_back(MDString::get(M.getContext(), getDynamicString()));
      break;
    case ELF::DT_SONAME:
      M.addModuleFlag(Module::Warning, "bcdb.elf.soname",
                      MDString::get(M.getContext(), getDynamicString()));
      break;
    case ELF::DT_RPATH:
      splitRunpath("bcdb.elf.rpath");
      break;
    case ELF::DT_RUNPATH:
      splitRunpath("bcdb.elf.runpath");
      break;
    case ELF::DT_FLAGS:
      M.addModuleFlag(Module::Error, "bcdb.elf.flags", Dyn.getVal());
      break;
    case ELF::DT_FLAGS_1:
      M.addModuleFlag(Module::Error, "bcdb.elf.flags_1", Dyn.getVal());
      break;
    case ELF::DT_AUXILIARY:
      M.addModuleFlag(Module::Error, "bcdb.elf.auxiliary",
                      MDString::get(M.getContext(), getDynamicString()));
      break;
    case ELF::DT_FILTER:
      M.addModuleFlag(Module::Error, "bcdb.elf.filter",
                      MDString::get(M.getContext(), getDynamicString()));
      break;
    }
  }

  if (!Needed.empty())
    M.addModuleFlag(Module::AppendUnique, "bcdb.elf.needed",
                    MDTuple::get(M.getContext(), Needed));

  return true;
}

std::unique_ptr<Module> bcdb::ExtractModuleFromBinary(LLVMContext &Context,
                                                      Binary &B) {
  if (auto *ELF = dyn_cast<ELF64LEObjectFile>(&B)) {
    return ExtractModuleFromELF(Context, *ELF);
  } else if (auto *ELF = dyn_cast<ELF64BEObjectFile>(&B)) {
    return ExtractModuleFromELF(Context, *ELF);
  } else if (auto *ELF = dyn_cast<ELF32LEObjectFile>(&B)) {
    return ExtractModuleFromELF(Context, *ELF);
  } else if (auto *ELF = dyn_cast<ELF32BEObjectFile>(&B)) {
    return ExtractModuleFromELF(Context, *ELF);
  }
  return nullptr;
}

bool bcdb::AnnotateModuleWithBinary(Module &M, Binary &B) {
  if (auto *ELF = dyn_cast<ELF64LEObjectFile>(&B)) {
    return AnnotateModuleWithELF(M, *ELF);
  } else if (auto *ELF = dyn_cast<ELF64BEObjectFile>(&B)) {
    return AnnotateModuleWithELF(M, *ELF);
  } else if (auto *ELF = dyn_cast<ELF32LEObjectFile>(&B)) {
    return AnnotateModuleWithELF(M, *ELF);
  } else if (auto *ELF = dyn_cast<ELF32BEObjectFile>(&B)) {
    return AnnotateModuleWithELF(M, *ELF);
  }
  return false;
}

std::vector<std::string> bcdb::ImitateClangArgs(Module &M) {
  auto getIntegerOr0 = [&](StringRef Key) -> uint32_t {
    auto *CI = mdconst::extract_or_null<ConstantInt>(M.getModuleFlag(Key));
    return CI ? CI->getZExtValue() : 0;
  };

  auto getString = [&](StringRef Key) -> StringRef {
    auto *MDS = dyn_cast_or_null<MDString>(M.getModuleFlag(Key));
    return MDS ? MDS->getString() : "";
  };

  std::vector<std::string> Args, LinkerArgs;

  switch (getIntegerOr0("bcdb.elf.type")) {
  case ELF::ET_REL:
    Args.emplace_back("-c");
    break;
  case ELF::ET_EXEC:
    break;
  case ELF::ET_DYN:
    Args.emplace_back("-shared");
    break;
  default:
    report_fatal_error("unsupported ELF type");
  }

  switch (getIntegerOr0("PIC Level")) {
  case 0:
    break;
  case 1:
    Args.emplace_back("-fpic");
    break;
  case 2:
    Args.emplace_back("-fPIC");
    break;
  default:
    report_fatal_error("unsupported PIC level");
  }
  switch (getIntegerOr0("PIE Level")) {
  case 0:
    break;
  case 1:
    Args.emplace_back("-fpie");
    break;
  case 2:
    Args.emplace_back("-fPIE");
    break;
  default:
    report_fatal_error("unsupported PIE level");
  }

  uint32_t Flags = getIntegerOr0("bcdb.elf.flags");
  uint32_t Flags1 = getIntegerOr0("bcdb.elf.flags_1");
  if ((Flags & ELF::DF_ORIGIN) || (Flags1 & ELF::DF_1_ORIGIN))
    LinkerArgs.emplace_back("-zorigin");
  if (Flags & ELF::DF_SYMBOLIC)
    LinkerArgs.emplace_back("-Bsymbolic");
  if ((Flags & ELF::DF_BIND_NOW) || (Flags1 & ELF::DF_1_NOW))
    LinkerArgs.emplace_back("-znow");
  if (Flags1 & ELF::DF_1_GROUP)
    LinkerArgs.emplace_back("-Bgroup");
  if (Flags1 & ELF::DF_1_NODELETE)
    LinkerArgs.emplace_back("-znodelete");
  if (Flags1 & ELF::DF_1_LOADFLTR)
    LinkerArgs.emplace_back("-zloadfltr");
  if (Flags1 & ELF::DF_1_INITFIRST)
    LinkerArgs.emplace_back("-zinitfirst");
  if (Flags1 & ELF::DF_1_NOOPEN)
    LinkerArgs.emplace_back("-znodlopen");
  if (Flags1 & ELF::DF_1_INTERPOSE)
    LinkerArgs.emplace_back("-zinterpose");
  if (Flags1 & ELF::DF_1_NODEFLIB)
    LinkerArgs.emplace_back("-znodefaultlib");
  if (Flags1 & ELF::DF_1_NODUMP)
    LinkerArgs.emplace_back("-znodump");

  StringRef Str = getString("bcdb.elf.soname");
  if (!Str.empty())
    LinkerArgs.emplace_back(("-soname=" + Str).str());

  Str = getString("bcdb.elf.auxiliary");
  if (!Str.empty())
    LinkerArgs.emplace_back(("--auxiliary=" + Str).str());

  Str = getString("bcdb.elf.filter");
  if (!Str.empty())
    LinkerArgs.emplace_back(("--filter=" + Str).str());

  SmallVector<StringRef, 8> Runpath;
  MDTuple *MDT = cast_or_null<MDTuple>(M.getModuleFlag("bcdb.elf.runpath"));
  if (MDT)
    for (Metadata *MD : MDT->operands())
      Runpath.push_back(cast<MDString>(MD)->getString());
  MDT = cast_or_null<MDTuple>(M.getModuleFlag("bcdb.elf.rpath"));
  if (MDT) {
    errs() << "warning: converting RPATH to RUNPATH\n";
    for (Metadata *MD : MDT->operands())
      Runpath.push_back(cast<MDString>(MD)->getString());
  }

  if (!Runpath.empty())
    LinkerArgs.emplace_back("-rpath=" + join(Runpath, ":"));
  for (StringRef Str : Runpath)
    Args.emplace_back(("-L" + Str).str());

  MDT = cast_or_null<MDTuple>(M.getModuleFlag("bcdb.elf.needed"));
  if (MDT) {
    for (Metadata *MD : MDT->operands()) {
      Str = cast<MDString>(MD)->getString();
      if (Str.startswith("/"))
        Args.emplace_back(Str);
      else if (Str.contains('/'))
        Args.emplace_back(("./" + Str).str());
      else
        Args.emplace_back(("-l:" + Str).str());
    }
  }

  for (std::string &LinkerArg : LinkerArgs)
    Args.insert(Args.end(), {"-Xlinker", LinkerArg});

  return Args;
}
