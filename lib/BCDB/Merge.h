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

struct ResolvedReference {
  // static reference (fully resolved)
  ResolvedReference(StringRef Module, StringRef Name)
      : Module(Module), Name(Name) {}
  // dynamic reference (will be resolved later)
  ResolvedReference(StringRef Name) : Module(), Name(Name) {}
  // unresolved
  ResolvedReference() : Module(), Name() {}
  std::string Module, Name;
  bool operator<(const ResolvedReference &Other) const {
    return Module < Other.Module ||
           (Module == Other.Module && Name < Other.Name);
  }
};

class MergerGlobalGraph;

class Merger {
public:
  Merger(BCDB &bcdb);
  virtual ~Merger() {}
  void AddModule(StringRef Name);
  void RenameEverything();
  std::unique_ptr<Module>
  Finish(std::map<std::pair<std::string, std::string>, Value *> &Mapping);

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
  StringMap<StringSet<>> ModRefs;
  DenseMap<GlobalValue *, GlobalItem> GlobalItems;
  DenseMap<GlobalValue *, GlobalValue::LinkageTypes> LinkageMap;
  StringSet<> ReservedNames;

  friend class MergerGlobalGraph;
};

} // end namespace bcdb

#endif // BCDB_MERGE_H
