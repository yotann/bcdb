#ifndef BCDB_MERGE_H
#define BCDB_MERGE_H

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/Linker/IRMover.h>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
class Module;
class StringRef;
class Value;
} // end namespace llvm

namespace bcdb {

using namespace llvm;

class BCDB;
class MergerGlobalGraph;
struct ResolvedReference;

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
    // Name of the module that contained the original definition.
    std::string ModuleName;

    // Name of the original definition.
    std::string Name;

    // Value ID of the function module (if any).
    std::string PartID;

    // New name to resolve references to (may be a stub).
    std::string NewName;

    // New name to use for the actual definition (may be shared with other
    // GlobalItems).
    std::string NewDefName;

    // Other globals that this GlobalItem refers to.
    std::map<std::string, ResolvedReference> Refs;

    // Other GlobalItems that this GlobalItem refers to (used for the
    // dependency graph).
    SmallVector<GlobalItem *, 8> RefItems;

    // If true, don't create a stub named NewName.
    bool SkipStub = false;

    // For GL only, whether we should put the NewName definition in the
    // merged module.
    bool DefineInMergedModule = true;

    // For GL only, whether we should put an available_externally NewName
    // definition in the merged module. Applies only if DefineInMergedModule is
    // false.
    bool AvailableExternallyInMergedModule = false;

    // For GL only, whether we should put an available_externally NewName
    // definition in the stub module. Applies only if DefineInMergedModule is
    // true.
    bool AvailableExternallyInWrapperModule = false;

    // For GL only, whether we need a declaration of NewName in the stub
    // module. Applies only if DefineInMergedModule is true.
    bool NeededInWrapperModule = false;

    // For GL only, whether we need a declaration of NewName in the merged
    // module. Applies only if DefineInMergedModule is false.
    bool NeededInMergedModule = false;

    // For GL only, whether the body needs to be defined in the stub module.
    bool BodyInWrapperModule = false;

    // For GL only, whether the body refers to declarations in local scope.
    bool RefersToPluginScope = false;
  };

protected:
  StringSet<> LoadPartRefs(StringRef ID, StringRef SelfName);
  virtual void FixupPartDefinition(GlobalItem &GI, Function &Body) {}
  virtual GlobalValue *LoadPartDefinition(GlobalItem &GI, Module *M = nullptr);
  virtual void AddPartStub(Module &MergedModule, GlobalItem &GI,
                           GlobalValue *Def, GlobalValue *Decl,
                           StringRef NewName = "");
  virtual void LoadRemainder(std::unique_ptr<Module> M,
                             std::vector<GlobalItem *> &GIs);

  BCDB &bcdb;
  std::unique_ptr<Module> MergedModule;
  std::unique_ptr<IRMover> MergedModuleMover;
  StringMap<std::unique_ptr<Module>> ModRemainders;
  std::map<GlobalValue *, GlobalItem> GlobalItems;
  StringMap<std::pair<std::string, GlobalValue::LinkageTypes>> AliasMap;
  DenseMap<GlobalValue *, GlobalValue::LinkageTypes> LinkageMap;
  StringSet<> ReservedNames;

  bool EnableMustTail = false;
  bool EnableNameReuse = true;

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
  bool operator==(const ResolvedReference &Other) const {
    if (GI)
      return Other.GI && GI->NewName == Other.GI->NewName;
    else
      return !Other.GI && Name == Other.Name;
  }
  bool operator!=(const ResolvedReference &Other) const {
    return !(*this == Other);
  }
  bool operator<(const ResolvedReference &Other) const {
    return GI && Other.GI ? GI->NewName < Other.GI->NewName : Name < Other.Name;
  }
};
inline raw_ostream &operator<<(raw_ostream &OS, const ResolvedReference &Ref) {
  if (Ref.GI)
    return OS << "module(" << Ref.GI->ModuleName << ").symbol(" << Ref.GI->Name
              << ").renamed(" << Ref.GI->NewName << ")";
  else
    return OS << "dynamic(" << Ref.Name << ")";
}

} // end namespace bcdb

#endif // BCDB_MERGE_H
