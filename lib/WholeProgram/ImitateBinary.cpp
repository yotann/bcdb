#include "bcdb/WholeProgram.h"

#include <algorithm>
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

#if LLVM_VERSION_MAJOR >= 7
  auto DynamicEntries = Err(ELF.getELFFile()->dynamicEntries());
  auto toMappedAddr = [&](uint64_t VAddr) -> const uint8_t * {
    return Err(ELF.getELFFile()->toMappedAddr(VAddr));
  };
#else
  // based on old versions of llvm/tools/llvm-readobj/ELFDumper.cpp
  using Elf_Dyn = typename ELFFile<ELFT>::Elf_Dyn;
  using Elf_Phdr = typename ELFFile<ELFT>::Elf_Phdr;
  ArrayRef<Elf_Dyn> DynamicEntries;
  SmallVector<const Elf_Phdr *, 4> LoadSegments;
  for (const Elf_Phdr &Phdr : Err(ELF.getELFFile()->program_headers())) {
    if (Phdr.p_type == ELF::PT_DYNAMIC) {
      // TODO: check for errors
      const Elf_Dyn *start = reinterpret_cast<const Elf_Dyn *>(
          ELF.getELFFile()->base() + Phdr.p_offset);
      const Elf_Dyn *end = start + Phdr.p_filesz / sizeof(Elf_Dyn);
      DynamicEntries = ArrayRef<Elf_Dyn>(start, end);
    } else if (Phdr.p_type == ELF::PT_LOAD && Phdr.p_filesz) {
      LoadSegments.push_back(&Phdr);
    }
  }
  auto toMappedAddr = [&](uint64_t VAddr) -> const uint8_t * {
    const Elf_Phdr *const *I =
        std::upper_bound(LoadSegments.begin(), LoadSegments.end(), VAddr,
                         [](uint64_t VAddr, const Elf_Phdr_Impl<ELFT> *Phdr) {
                           return VAddr < Phdr->p_vaddr;
                         });
    if (I == LoadSegments.begin())
      report_fatal_error("Virtual address is not in any segment");
    --I;
    const Elf_Phdr &Phdr = **I;
    uint64_t Delta = VAddr - Phdr.p_vaddr;
    if (Delta >= Phdr.p_filesz)
      report_fatal_error("Virtual address is not in any segment");
    return ELF.getELFFile()->base() + Phdr.p_offset + Delta;
  };
#endif

  const char *strtab = nullptr;
  ssize_t rpath = -1;
  SmallVector<size_t, 8> needed;
  for (auto &Dyn : DynamicEntries) {
    if (Dyn.d_tag == ELF::DT_STRTAB) {
      strtab = reinterpret_cast<const char *>(toMappedAddr(Dyn.getPtr()));
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
