#ifndef MEMODB_MEMODB_H
#define MEMODB_MEMODB_H

#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <stddef.h>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>

#include "CID.h"
#include "Node.h"

struct ParsedURI {
public:
  ParsedURI(llvm::StringRef URI);

  // If input is "x:/foo%2Fbar", Path will be "/foo/bar" and PathSegments will
  // be ["", "foo%2Fbar"].
  std::string Scheme, Authority, Path, Query, Fragment;
  std::vector<std::string> PathSegments;
};

struct memodb_head {
  std::string Name;

  explicit memodb_head(const char *Name) : Name(Name) {}
  explicit memodb_head(std::string &&Name) : Name(Name) {}
  explicit memodb_head(llvm::StringRef Name) : Name(Name) {}
};

std::ostream &operator<<(std::ostream &os, const memodb_head &head);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_head &head);

struct memodb_call {
  std::string Name;
  std::vector<memodb::CID> Args;

  memodb_call(llvm::StringRef Name, llvm::ArrayRef<memodb::CID> Args)
      : Name(Name), Args(Args) {}
};

std::ostream &operator<<(std::ostream &os, const memodb_call &call);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_call &call);

struct memodb_name
    : public std::variant<memodb::CID, memodb_head, memodb_call> {
  typedef std::variant<memodb::CID, memodb_head, memodb_call> BaseType;

  constexpr memodb_name(const memodb::CID &Ref) : variant(Ref) {}
  constexpr memodb_name(const memodb_head &Head) : variant(Head) {}
  constexpr memodb_name(const memodb_call &Call) : variant(Call) {}
  constexpr memodb_name(memodb::CID &&Ref) : variant(Ref) {}
  constexpr memodb_name(memodb_head &&Head) : variant(Head) {}
  constexpr memodb_name(memodb_call &&Call) : variant(Call) {}

  template <class Visitor> constexpr void visit(Visitor &&vis) {
    BaseType &Base = *this;
    return std::visit(vis, Base);
  }

  template <class Visitor> constexpr void visit(Visitor &&vis) const {
    const BaseType &Base = *this;
    return std::visit(vis, Base);
  }
};

std::ostream &operator<<(std::ostream &os, const memodb_name &name);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_name &name);

using memodb_path = std::pair<memodb_name, std::vector<memodb::Node>>;

class memodb_db {
public:
  virtual ~memodb_db() {}

  virtual llvm::Optional<memodb::Node> getOptional(const memodb::CID &CID) = 0;
  virtual llvm::Optional<memodb::CID>
  resolveOptional(const memodb_name &Name) = 0;
  virtual memodb::CID put(const memodb::Node &value) = 0;
  virtual void set(const memodb_name &Name, const memodb::CID &ref) = 0;
  virtual std::vector<memodb_name> list_names_using(const memodb::CID &ref) = 0;
  virtual std::vector<std::string> list_funcs() = 0;
  // F should not modify the database. F can return true to stop iteration.
  virtual void eachHead(std::function<bool(const memodb_head &)> F) = 0;
  virtual void eachCall(llvm::StringRef Func,
                        std::function<bool(const memodb_call &)> F) = 0;
  virtual void head_delete(const memodb_head &Head) = 0;
  virtual void call_invalidate(llvm::StringRef name) = 0;

  virtual bool has(const memodb::CID &CID) {
    return getOptional(CID).hasValue();
  }
  virtual bool has(const memodb_name &Name) {
    if (const memodb::CID *CID = std::get_if<memodb::CID>(&Name))
      return has(*CID);
    return resolveOptional(Name).hasValue();
  }

  memodb::Node get(const memodb::CID &CID) { return *getOptional(CID); }
  llvm::Optional<memodb::Node> getOptional(const memodb_name &Name) {
    auto CID = resolveOptional(Name);
    return CID ? getOptional(*CID) : llvm::None;
  }
  memodb::Node get(const memodb_name &Name) { return get(resolve(Name)); }
  memodb::CID resolve(const memodb_name &Name) {
    return *resolveOptional(Name);
  }

  memodb::CID head_get(llvm::StringRef name) {
    return resolve(memodb_head(name));
  }

  void head_set(llvm::StringRef name, const memodb::CID &ref) {
    set(memodb_head(name), ref);
  }

  void call_set(llvm::StringRef name, llvm::ArrayRef<memodb::CID> args,
                const memodb::CID &result) {
    set(memodb_call(name, args), result);
  }

  std::vector<memodb_head> list_heads() {
    std::vector<memodb_head> Result;
    eachHead([&](const memodb_head &Head) {
      Result.emplace_back(Head);
      return false;
    });
    return Result;
  }

  std::vector<memodb_call> list_calls(llvm::StringRef Func) {
    std::vector<memodb_call> Result;
    eachCall(Func, [&](const memodb_call &Call) {
      Result.emplace_back(Call);
      return false;
    });
    return Result;
  }

  virtual std::vector<memodb_path> list_paths_to(const memodb::CID &ref);

  template <typename F, typename... Targs>
  memodb::CID call_or_lookup_ref(llvm::StringRef name, F func, Targs... Fargs) {
    memodb_call Call(name, {Fargs...});
    auto ref = resolveOptional(Call);
    if (!ref) {
      ref = put(func(*this, get(Fargs)...));
      call_set(name, {Fargs...}, ref);
    }
    return *ref;
  }

  template <typename F, typename... Targs>
  memodb::Node call_or_lookup_value(llvm::StringRef name, F func,
                                    Targs... Fargs) {
    memodb_call Call(name, {Fargs...});
    memodb::Node value;
    if (auto ref = resolveOptional(Call)) {
      value = get(*ref);
    } else {
      value = func(*this, get(Fargs)...);
      call_set(name, {Fargs...}, put(value));
    }
    return value;
  }
};

std::unique_ptr<memodb_db> memodb_db_open(llvm::StringRef uri,
                                          bool create_if_missing = false);

#endif // MEMODB_MEMODB_H
