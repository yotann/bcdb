#ifndef MEMODB_MOCKSTORE_H
#define MEMODB_MOCKSTORE_H

#include "memodb/Store.h"

#include <functional>
#include <string>
#include <vector>

#include <llvm/ADT/Optional.h>
#include <llvm/ADT/StringRef.h>

#include "memodb/Node.h"
#include "gmock/gmock.h"

namespace memodb {

class MockStore : public Store {
public:
  MOCK_METHOD(llvm::Optional<Node>, getOptional, (const CID &CID), (override));
  MOCK_METHOD(llvm::Optional<CID>, resolveOptional, (const Name &Name),
              (override));
  MOCK_METHOD(CID, put, (const Node &value), (override));
  MOCK_METHOD(void, set, (const Name &Name, const CID &ref), (override));
  MOCK_METHOD(std::vector<Name>, list_names_using, (const CID &ref),
              (override));
  MOCK_METHOD(std::vector<std::string>, list_funcs, (), (override));
  MOCK_METHOD(void, eachHead, (std::function<bool(const Head &)> F),
              (override));
  MOCK_METHOD(void, eachCall,
              (llvm::StringRef Func, std::function<bool(const Call &)> F),
              (override));
  MOCK_METHOD(void, head_delete, (const Head &Head), (override));
  MOCK_METHOD(void, call_invalidate, (llvm::StringRef name), (override));
  MOCK_METHOD(bool, has, (const CID &CID), (override));
  MOCK_METHOD(bool, has, (const Name &Name), (override));
};

} // namespace memodb

#endif // MEMODB_MOCKSTORE_H
