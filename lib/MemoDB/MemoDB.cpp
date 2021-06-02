#include "memodb/memodb.h"

#include "memodb_internal.h"

#include <llvm/Support/ConvertUTF.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/raw_os_ostream.h>
#include <sstream>

using namespace memodb;

ParsedURI::ParsedURI(llvm::StringRef URI) {
  llvm::StringRef AuthorityRef, PathRef, QueryRef, FragmentRef;

  if (URI.contains(':'))
    std::tie(Scheme, URI) = URI.split(':');
  if (URI.startswith("//")) {
    size_t i = URI.find_first_of("/?#", 2);
    if (i == llvm::StringRef::npos) {
      AuthorityRef = URI.substr(2);
      URI = "";
    } else {
      AuthorityRef = URI.substr(2, i - 2);
      URI = URI.substr(i);
    }
  }
  std::tie(URI, FragmentRef) = URI.split('#');
  std::tie(PathRef, QueryRef) = URI.split('?');

  auto percentDecode = [](llvm::StringRef Str) -> std::string {
    if (!Str.contains('%'))
      return Str.str();
    std::string Result;
    while (!Str.empty()) {
      size_t i = Str.find('%');
      Result.append(Str.take_front(i));
      Str = Str.substr(i);
      if (Str.empty())
        break;
      unsigned Code;
      if (Str.size() >= 3 && !Str.substr(1, 2).getAsInteger(16, Code)) {
        Result.push_back((char)Code);
        Str = Str.substr(3);
      } else {
        llvm::report_fatal_error("invalid percent encoding in URI");
      }
    }
    return Result;
  };

  Authority = percentDecode(AuthorityRef);
  Path = percentDecode(PathRef);
  Query = percentDecode(QueryRef);
  Fragment = percentDecode(FragmentRef);

  llvm::SmallVector<llvm::StringRef, 8> Segments;
  PathRef.split(Segments, '/');
  for (const auto &Segment : Segments)
    PathSegments.emplace_back(percentDecode(Segment));
}

std::unique_ptr<memodb_db> memodb_db_open(llvm::StringRef uri,
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

std::ostream &operator<<(std::ostream &os, const memodb_head &head) {
  return os << head.Name;
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_head &head) {
  return os << head.Name;
}

std::ostream &operator<<(std::ostream &os, const memodb_call &call) {
  os << "call:" << call.Name;
  for (const CID &Arg : call.Args)
    os << "/" << Arg;
  return os;
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_call &call) {
  os << "call:" << call.Name;
  for (const CID &Arg : call.Args)
    os << "/" << Arg;
  return os;
}

std::ostream &operator<<(std::ostream &os, const memodb_name &name) {
  if (const memodb_head *Head = std::get_if<memodb_head>(&name)) {
    os << "heads[" << Node(Head->Name) << "]";
  } else {
    name.visit([&](auto X) { os << X; });
  }
  return os;
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_name &name) {
  if (const memodb_head *Head = std::get_if<memodb_head>(&name)) {
    os << "heads[" << Node(Head->Name) << "]";
  } else {
    name.visit([&](auto X) { os << X; });
  }
  return os;
}

std::vector<memodb_path> memodb_db::list_paths_to(const CID &ref) {
  using memodb::Node;
  auto listPathsWithin = [](const Node &Value,
                            const CID &Ref) -> std::vector<std::vector<Node>> {
    std::vector<std::vector<Node>> Result;
    std::vector<Node> CurPath;
    std::function<void(const Node &)> recurse = [&](const Node &Value) {
      if (Value.type() == Node::REF) {
        if (Value.as_ref() == Ref)
          Result.push_back(CurPath);
      } else if (Value.type() == Node::ARRAY) {
        for (size_t i = 0; i < Value.array_items().size(); i++) {
          CurPath.push_back(i);
          recurse(Value[i]);
          CurPath.pop_back();
        }
      } else if (Value.type() == Node::MAP) {
        for (const auto &item : Value.map_items()) {
          CurPath.push_back(item.first);
          recurse(item.second);
          CurPath.pop_back();
        }
      }
    };
    recurse(Value);
    return Result;
  };

  std::vector<memodb_path> Result;
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
