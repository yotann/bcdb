#include "outlining/LinearProgram.h"

#include <llvm/Support/raw_ostream.h>

#include "gtest/gtest.h"

using namespace bcdb;

namespace {

TEST(LinearProgramTest, Empty) {
  LinearProgram LP("Empty");
  LP.setObjective("COST", {});

  std::string Buffer;
  llvm::raw_string_ostream OS(Buffer);
  LP.writeFreeMPS(OS);
  EXPECT_EQ(OS.str(), "NAME Empty\n"
                      "ROWS\n"
                      " N COST\n"
                      "COLUMNS\n"
                      "RHS\n"
                      "BOUNDS\n"
                      "ENDATA\n");
}

TEST(LinearProgramTest, Trivial) {
  LinearProgram LP("Trivial");
  auto X = LP.makeRealVar("X", 2.0, 3.0);
  LP.setObjective("COST", X);

  std::string Buffer;
  llvm::raw_string_ostream OS(Buffer);
  LP.writeFreeMPS(OS);
  EXPECT_EQ(OS.str(), "NAME Trivial\n"
                      "ROWS\n"
                      " N COST\n"
                      "COLUMNS\n"
                      " X COST 1.000000e+00\n"
                      "RHS\n"
                      "BOUNDS\n"
                      " LO BND1 X 2.000000e+00\n"
                      " UP BND1 X 3.000000e+00\n"
                      "ENDATA\n");
}

TEST(LinearProgramTest, Simple) {
  // Taken from http://lpsolve.sourceforge.net/5.5/mps-format.htm
  LinearProgram LP("TESTPROB");
  auto X = LP.makeRealVar("X", 0.0, 4.0);
  auto Y = LP.makeRealVar("Y", -1.0, 1.0);
  auto Z = LP.makeRealVar("Z", 0.0, {});
  LP.addConstraint("LIM1", X + Y <= 5);
  LP.addConstraint("LIM2", X + Z >= 10);
  LP.addConstraint("MYEQN", Z - Y == 7);
  LP.setObjective("COST", Y + X + Y + 9 * Z + 2 * Y);

  std::string Buffer;
  llvm::raw_string_ostream OS(Buffer);
  LP.writeFreeMPS(OS);
  EXPECT_EQ(OS.str(), "NAME TESTPROB\n"
                      "ROWS\n"
                      " N COST\n"
                      " L LIM1\n"
                      " G LIM2\n"
                      " E MYEQN\n"
                      "COLUMNS\n"
                      " X COST 1.000000e+00\n"
                      " X LIM1 1.000000e+00\n"
                      " X LIM2 1.000000e+00\n"
                      " Y COST 4.000000e+00\n"
                      " Y LIM1 1.000000e+00\n"
                      " Y MYEQN -1.000000e+00\n"
                      " Z COST 9.000000e+00\n"
                      " Z LIM2 1.000000e+00\n"
                      " Z MYEQN 1.000000e+00\n"
                      "RHS\n"
                      " RHS1 LIM1 5.000000e+00\n"
                      " RHS1 LIM2 1.000000e+01\n"
                      " RHS1 MYEQN 7.000000e+00\n"
                      "BOUNDS\n"
                      " UP BND1 X 4.000000e+00\n"
                      " LO BND1 Y -1.000000e+00\n"
                      " UP BND1 Y 1.000000e+00\n"
                      "ENDATA\n");
}

} // end anonymous namespace
