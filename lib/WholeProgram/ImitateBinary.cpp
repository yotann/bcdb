#include "bcdb/WholeProgram.h"

#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/Object/ELFObjectFile.h>
#include <llvm/Support/Error.h>

using namespace bcdb;
using namespace llvm;
using namespace llvm::object;

// TODO: support other binary formats
// TODO: update symbol linkage types to match the binary
// TODO: detect additional symbols in the binary that came from assembly files

template <class ELFT>
static bool ImitateELF(Module &M, const ELFObjectFile<ELFT> &ELF) {
  ExitOnError Err("ImitateELF: ");
  auto DynamicEntries = Err(ELF.getELFFile()->dynamicEntries());
  const char *strtab = nullptr;
  ssize_t rpath = -1;
  SmallVector<size_t, 8> needed;
  for (auto &Dyn : DynamicEntries) {
    if (Dyn.d_tag == ELF::DT_STRTAB) {
      auto MappedAddr = Err(ELF.getELFFile()->toMappedAddr(Dyn.getPtr()));
      strtab = reinterpret_cast<const char *>(MappedAddr);
    } else if (Dyn.d_tag == ELF::DT_NEEDED) {
      needed.push_back(Dyn.d_un.d_val);
    } else if (Dyn.d_tag == ELF::DT_RPATH || Dyn.d_tag == ELF::DT_RUNPATH) {
      rpath = Dyn.d_un.d_val;
    }
  }

  if (!strtab)
    return false;

  if (rpath >= 0) {
    NamedMDNode *NMD = M.getOrInsertNamedMetadata("bcdb.elf.rpath");
    StringRef Name(strtab + rpath);
    NMD->addOperand(
        MDTuple::get(M.getContext(), {MDString::get(M.getContext(), Name)}));
  }

  if (!needed.empty()) {
    NamedMDNode *NMD = M.getOrInsertNamedMetadata("bcdb.elf.needed");
    for (size_t i : needed) {
      StringRef Name(strtab + i);
      NMD->addOperand(
          MDTuple::get(M.getContext(), {MDString::get(M.getContext(), Name)}));
    }
  }

  return true;
}

bool bcdb::ImitateBinary(Module &M, Binary &B) {
  if (auto *ELF = dyn_cast<ELF64LEObjectFile>(&B)) {
    return ImitateELF(M, *ELF);
  } else if (auto *ELF = dyn_cast<ELF64BEObjectFile>(&B)) {
    return ImitateELF(M, *ELF);
  } else if (auto *ELF = dyn_cast<ELF32LEObjectFile>(&B)) {
    return ImitateELF(M, *ELF);
  } else if (auto *ELF = dyn_cast<ELF32BEObjectFile>(&B)) {
    return ImitateELF(M, *ELF);
  }
  return false;
}
