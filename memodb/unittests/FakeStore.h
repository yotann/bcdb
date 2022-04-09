#ifndef MEMODB_FAKESTORE_H
#define MEMODB_FAKESTORE_H

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <llvm/ADT/Optional.h>

#include "memodb/CID.h"
#include "memodb/Node.h"
#include "memodb/Store.h"
#include "gtest/gtest.h"

namespace memodb {

class FakeStore : public Store {
public:
  llvm::Optional<Node> getOptional(const CID &CID) override {
    if (!nodes.count(CID))
      return {};
    auto nodeOrErr = Node::loadFromIPLD(*this, CID, nodes[CID]);
    EXPECT_TRUE(static_cast<bool>(nodeOrErr));
    return *nodeOrErr;
  }

  llvm::Optional<CID> resolveOptional(const Name &Name) override {
    if (const CID *ref = std::get_if<CID>(&Name)) {
      return *ref;
    } else if (const Head *head = std::get_if<Head>(&Name)) {
      if (heads.count(*head))
        return heads.at(*head);
    } else if (const Call *call = std::get_if<Call>(&Name)) {
      if (calls.count(call->Name))
        if (calls[call->Name].count(*call))
          return calls[call->Name].at(*call);
    }
    return {};
  }

  CID put(const Node &value) override {
    auto ipld = value.saveAsIPLD();
    nodes[ipld.first] = std::move(ipld.second);
    return ipld.first;
  }

  void set(const Name &Name, const CID &ref) override {
    if (const Head *head = std::get_if<Head>(&Name))
      heads.emplace(*head, ref);
    else if (const Call *call = std::get_if<Call>(&Name))
      calls[call->Name].emplace(*call, ref);
    else
      ASSERT_TRUE(false) << "can't set a CID";
  }

  std::vector<Name> list_names_using(const CID &ref) override { return {}; }

  std::vector<std::string> list_funcs() override {
    std::vector<std::string> result;
    for (const auto &item : calls)
      result.emplace_back(item.first);
    return result;
  }

  void eachHead(std::function<bool(const Head &)> F) override {
    for (const auto &item : heads)
      if (F(item.first))
        return;
  }

  void eachCall(llvm::StringRef Func,
                std::function<bool(const Call &)> F) override {
    for (const auto &item : calls[Func.str()])
      if (F(item.first))
        return;
  }

  void head_delete(const Head &Head) override { heads.erase(Head); }

  void call_invalidate(llvm::StringRef name) override {
    calls.erase(name.str());
  }

private:
  std::map<CID, std::vector<std::uint8_t>> nodes;
  std::map<Head, CID> heads;
  std::map<std::string, std::map<Call, CID>> calls;
};

} // namespace memodb

#endif // MEMODB_FAKESTORE_H
