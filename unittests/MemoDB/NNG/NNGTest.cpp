#include "memodb/NNG.h"

#include <llvm/Testing/Support/Error.h>
#include <optional>

#include "gtest/gtest.h"

using namespace memodb;

llvm::detail::ErrorHolder llvm::detail::TakeError(llvm::Error Err) {
  std::vector<std::shared_ptr<ErrorInfoBase>> Infos;
  handleAllErrors(std::move(Err),
                  [&Infos](std::unique_ptr<ErrorInfoBase> Info) {
                    Infos.emplace_back(std::move(Info));
                  });
  return {std::move(Infos)};
}

namespace {

TEST(URL, Parse) {
  auto url = nng::URL::parse("scheme://hostname/path?query#fragment");
  EXPECT_FALSE(!url);
  EXPECT_EQ(url->getRawURL(), "scheme://hostname/path?query#fragment");
  EXPECT_EQ(url->getScheme(), "scheme");
  EXPECT_EQ(url->getUserInfo(), std::nullopt);
  EXPECT_EQ(url->getHost(), "hostname");
  EXPECT_EQ(url->getHostName(), "hostname");
  EXPECT_EQ(url->getPort(), "");
  EXPECT_EQ(url->getPath(), "/path");
  EXPECT_EQ(url->getQuery(), "query");
  EXPECT_EQ(url->getFragment(), "fragment");
  EXPECT_EQ(url->getReqURI(), "/path?query#fragment");
}

} // namespace
