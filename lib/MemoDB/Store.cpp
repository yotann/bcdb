#include "memodb/Store.h"

#include "memodb_internal.h"

#include <llvm/Support/ConvertUTF.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/raw_os_ostream.h>
#include <sstream>

using namespace memodb;

std::unique_ptr<Store> Store::open(llvm::StringRef uri,
                                   bool create_if_missing) {
  if (uri.startswith("sqlite:")) {
    return memodb_sqlite_open(uri.substr(7), create_if_missing);
  } else if (uri.startswith("car:")) {
    return memodb_car_open(uri, create_if_missing);
  } else if (uri.startswith("rocksdb:")) {
    return memodb_rocksdb_open(uri, create_if_missing);
  } else {
    llvm::report_fatal_error(llvm::Twine("unsupported URI ") + uri);
  }
}

std::ostream &memodb::operator<<(std::ostream &os, const Head &head) {
  return os << head.Name;
}

llvm::raw_ostream &memodb::operator<<(llvm::raw_ostream &os, const Head &head) {
  return os << head.Name;
}

std::ostream &memodb::operator<<(std::ostream &os, const Call &call) {
  os << "call:" << call.Name;
  for (const CID &Arg : call.Args)
    os << "/" << Arg;
  return os;
}

llvm::raw_ostream &memodb::operator<<(llvm::raw_ostream &os, const Call &call) {
  os << "call:" << call.Name;
  for (const CID &Arg : call.Args)
    os << "/" << Arg;
  return os;
}

std::ostream &memodb::operator<<(std::ostream &os, const Name &name) {
  if (const Head *head = std::get_if<Head>(&name)) {
    os << "heads[" << Node(utf8_string_arg, head->Name) << "]";
  } else {
    name.visit([&](auto X) { os << X; });
  }
  return os;
}

llvm::raw_ostream &memodb::operator<<(llvm::raw_ostream &os, const Name &name) {
  if (const Head *head = std::get_if<Head>(&name)) {
    os << "heads[" << Node(utf8_string_arg, head->Name) << "]";
  } else {
    name.visit([&](auto X) { os << X; });
  }
  return os;
}

std::vector<Path> Store::list_paths_to(const CID &ref) {
  using memodb::Node;
  auto listPathsWithin = [](const Node &Value,
                            const CID &Ref) -> std::vector<std::vector<Node>> {
    std::vector<std::vector<Node>> Result;
    std::vector<Node> CurPath;
    std::function<void(const Node &)> recurse = [&](const Node &Value) {
      if (Value.kind() == Kind::Link) {
        if (Value.as_link() == Ref)
          Result.push_back(CurPath);
      } else if (Value.kind() == Kind::List) {
        for (size_t i = 0; i < Value.size(); i++) {
          CurPath.push_back(i);
          recurse(Value[i]);
          CurPath.pop_back();
        }
      } else if (Value.kind() == Kind::Map) {
        for (const auto &item : Value.map_range()) {
          CurPath.emplace_back(utf8_string_arg, item.key());
          recurse(item.value());
          CurPath.pop_back();
        }
      }
    };
    recurse(Value);
    return Result;
  };

  std::vector<Path> Result;
  std::vector<Node> BackwardsPath;
  std::function<void(const CID &)> recurse = [&](const CID &Ref) {
    for (const auto &Parent : list_names_using(Ref)) {
      if (const CID *ParentRef = std::get_if<CID>(&Parent)) {
        const Node Node = get(*ParentRef);
        for (const auto &Subpath : listPathsWithin(Node, Ref)) {
          BackwardsPath.insert(BackwardsPath.end(), Subpath.rbegin(),
                               Subpath.rend());
          recurse(*ParentRef);
          BackwardsPath.erase(BackwardsPath.end() - Subpath.size(),
                              BackwardsPath.end());
        }
      } else {
        Result.emplace_back(Parent, std::vector<Node>(BackwardsPath.rbegin(),
                                                      BackwardsPath.rend()));
      }
    }
  };
  recurse(ref);
  return Result;
}
