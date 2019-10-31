#ifndef BCDB_MERGE_H
#define BCDB_MERGE_H

#include "bcdb/BCDB.h"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/IR/GlobalValue.h>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace llvm {
class Module;
class StringRef;
class Value;
} // end namespace llvm

namespace bcdb {

using namespace llvm;

struct ResolvedReference;

class MergerGlobalGraph;

class Merger {
public:
  Merger(BCDB &bcdb);
  virtual ~Merger() {}
  void AddModule(StringRef Name);
  void RenameEverything();
  std::unique_ptr<Module> Finish();

  std::string ReserveName(StringRef Prefix);
  virtual ResolvedReference Resolve(StringRef ModuleName, StringRef Name);
  StringRef GetNewName(const ResolvedReference &Ref);
  void ReplaceGlobal(Module &M, StringRef Name, GlobalValue *New);
  void ApplyNewNames(Module &M,
                     const std::map<std::string, ResolvedReference> &Refs);

  struct GlobalItem {
    std::string ModuleName;
    std::string Name;
    std::string PartID;
    std::string NewName;
    std::string NewDefName;
    std::map<std::string, ResolvedReference> Refs;
    SmallVector<GlobalItem *, 8> RefItems;
  };

protected:
  StringSet<> LoadPartRefs(StringRef ID);
  GlobalValue *LoadPartDefinition(Module &MergedModule, GlobalItem &GI);
  virtual void AddPartStub(Module &MergedModule, GlobalItem &GI,
                           GlobalValue *Def, GlobalValue *Decl);
  virtual void LoadRemainder(Module &MergedModule, std::unique_ptr<Module> M,
                             std::vector<GlobalItem *> &GIs);

  BCDB &bcdb;
  StringMap<std::unique_ptr<Module>> ModRemainders;
  DenseMap<GlobalValue *, GlobalItem> GlobalItems;
  DenseMap<GlobalValue *, GlobalValue::LinkageTypes> LinkageMap;
  StringSet<> ReservedNames;

  friend class MergerGlobalGraph;
};

struct ResolvedReference {
  // static reference (fully resolved)
  ResolvedReference(Merger::GlobalItem *GI) : GI(GI), Name() {}
  // dynamic reference (will be resolved later)
  ResolvedReference(StringRef Name) : GI(), Name(Name) {}
  // unresolved
  ResolvedReference() : GI(), Name() {}
  Merger::GlobalItem *GI;
  std::string Name;
  bool operator<(const ResolvedReference &Other) const {
    return GI < Other.GI || (GI == Other.GI && Name < Other.Name);
  }
};

} // end namespace bcdb

#endif // BCDB_MERGE_H
