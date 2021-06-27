#ifndef MEMODB_STORE_H
#define MEMODB_STORE_H

#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <stddef.h>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>

#include "CID.h"
#include "Node.h"

namespace memodb {

struct Head {
  std::string Name;

  explicit Head(const char *Name) : Name(Name) {}
  explicit Head(std::string &&Name) : Name(Name) {}
  explicit Head(llvm::StringRef Name) : Name(Name) {}
};

std::ostream &operator<<(std::ostream &os, const Head &head);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const Head &head);

struct Call {
  std::string Name;
  std::vector<CID> Args;

  Call(llvm::StringRef Name, llvm::ArrayRef<CID> Args)
      : Name(Name), Args(Args) {}
};

std::ostream &operator<<(std::ostream &os, const Call &call);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const Call &call);

struct Name : public std::variant<CID, Head, Call> {
  typedef std::variant<CID, Head, Call> BaseType;

  constexpr Name(const CID &Ref) : variant(Ref) {}
  constexpr Name(const Head &Head) : variant(Head) {}
  constexpr Name(const Call &Call) : variant(Call) {}
  constexpr Name(CID &&Ref) : variant(Ref) {}
  constexpr Name(Head &&Head) : variant(Head) {}
  constexpr Name(Call &&Call) : variant(Call) {}

  template <class Visitor> constexpr void visit(Visitor &&vis) {
    BaseType &Base = *this;
    return std::visit(vis, Base);
  }

  template <class Visitor> constexpr void visit(Visitor &&vis) const {
    const BaseType &Base = *this;
    return std::visit(vis, Base);
  }
};

std::ostream &operator<<(std::ostream &os, const Name &name);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const Name &name);

using Path = std::pair<Name, std::vector<Node>>;

class Store {
public:
  static std::unique_ptr<Store> open(llvm::StringRef uri,
                                     bool create_if_missing = false);

  virtual ~Store() {}

  virtual llvm::Optional<Node> getOptional(const CID &CID) = 0;
  virtual llvm::Optional<CID> resolveOptional(const Name &Name) = 0;
  virtual CID put(const Node &value) = 0;
  virtual void set(const Name &Name, const CID &ref) = 0;
  virtual std::vector<Name> list_names_using(const CID &ref) = 0;
  virtual std::vector<std::string> list_funcs() = 0;
  // F should not modify the database. F can return true to stop iteration.
  virtual void eachHead(std::function<bool(const Head &)> F) = 0;
  virtual void eachCall(llvm::StringRef Func,
                        std::function<bool(const Call &)> F) = 0;
  virtual void head_delete(const Head &Head) = 0;
  virtual void call_invalidate(llvm::StringRef name) = 0;

  virtual bool has(const CID &CID) { return getOptional(CID).hasValue(); }
  virtual bool has(const Name &Name) {
    if (const CID *cid = std::get_if<CID>(&Name))
      return has(*cid);
    return resolveOptional(Name).hasValue();
  }

  Node get(const CID &CID) { return *getOptional(CID); }
  llvm::Optional<Node> getOptional(const Name &Name) {
    auto CID = resolveOptional(Name);
    return CID ? getOptional(*CID) : llvm::None;
  }
  Node get(const Name &Name) { return get(resolve(Name)); }
  CID resolve(const Name &Name) { return *resolveOptional(Name); }

  CID head_get(llvm::StringRef name) { return resolve(Head(name)); }

  void head_set(llvm::StringRef name, const CID &ref) { set(Head(name), ref); }

  void call_set(llvm::StringRef name, llvm::ArrayRef<CID> args,
                const CID &result) {
    set(Call(name, args), result);
  }

  std::vector<Head> list_heads() {
    std::vector<Head> Result;
    eachHead([&](const Head &Head) {
      Result.emplace_back(Head);
      return false;
    });
    return Result;
  }

  std::vector<Call> list_calls(llvm::StringRef Func) {
    std::vector<Call> Result;
    eachCall(Func, [&](const Call &Call) {
      Result.emplace_back(Call);
      return false;
    });
    return Result;
  }

  virtual std::vector<Path> list_paths_to(const CID &ref);

  template <typename F, typename... Targs>
  CID call_or_lookup_ref(llvm::StringRef name, F func, Targs... Fargs) {
    Call Call(name, {Fargs...});
    auto ref = resolveOptional(Call);
    if (!ref) {
      ref = put(func(*this, get(Fargs)...));
      call_set(name, {Fargs...}, ref);
    }
    return *ref;
  }

  template <typename F, typename... Targs>
  Node call_or_lookup_value(llvm::StringRef name, F func, Targs... Fargs) {
    Call Call(name, {Fargs...});
    Node value;
    if (auto ref = resolveOptional(Call)) {
      value = get(*ref);
    } else {
      value = func(*this, get(Fargs)...);
      call_set(name, {Fargs...}, put(value));
    }
    return value;
  }
};

} // end namespace memodb

#endif // MEMODB_MEMODB_H
