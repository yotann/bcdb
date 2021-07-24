#include "outlining/LinearProgram.h"

#include <llvm/Support/raw_ostream.h>

#include "gtest/gtest.h"

using namespace bcdb;

namespace {

TEST(LinearProgramTest, Empty) {
  LinearProgram LP("EMPTY");
  LP.setObjective("COST", {});

  std::string Buffer;
  llvm::raw_string_ostream OS(Buffer);
  LP.writeFixedMPS(OS);
  EXPECT_EQ(OS.str(), R"(NAME          EMPTY
ROWS
 N  COST
COLUMNS
RHS
BOUNDS
ENDATA
)");
}

TEST(LinearProgramTest, Trivial) {
  LinearProgram LP("TRIVIAL");
  auto X = LP.makeRealVar("X", -2.00001, 1.3e-10);
  LP.setObjective("COST", X);

  std::string Buffer;
  llvm::raw_string_ostream OS(Buffer);
  LP.writeFixedMPS(OS);
  EXPECT_EQ(OS.str(), R"(NAME          TRIVIAL
ROWS
 N  COST
COLUMNS
    X         COST       1.00000E+00
RHS
BOUNDS
 LO BND1      X         -2.00001E+00
 UP BND1      X          1.30000E-10
ENDATA
)");
}

TEST(LinearProgramTest, Simple) {
  // Taken from http://lpsolve.sourceforge.net/5.5/mps-format.htm
  LinearProgram LP("TESTPROB");
  auto X = LP.makeRealVar("XONE", 0.0, 4.0);
  auto Y = LP.makeRealVar("YTWO", -1.0, 1.0);
  auto Z = LP.makeRealVar("ZTHREE", 0.0, {});
  LP.addConstraint("LIM1", X + Y <= 5);
  LP.addConstraint("LIM2", X + Z >= 10);
  LP.addConstraint("MYEQN", Z - Y == 7);
  LP.setObjective("COST", Y + X + Y + 9 * Z + 2 * Y);

  std::string Buffer;
  llvm::raw_string_ostream OS(Buffer);
  LP.writeFixedMPS(OS);
  EXPECT_EQ(OS.str(), R"(NAME          TESTPROB
ROWS
 N  COST
 L  LIM1
 G  LIM2
 E  MYEQN
COLUMNS
    XONE      COST       1.00000E+00
    XONE      LIM1       1.00000E+00
    XONE      LIM2       1.00000E+00
    YTWO      COST       4.00000E+00
    YTWO      LIM1       1.00000E+00
    YTWO      MYEQN     -1.00000E+00
    ZTHREE    COST       9.00000E+00
    ZTHREE    LIM2       1.00000E+00
    ZTHREE    MYEQN      1.00000E+00
RHS
    RHS1      LIM1       5.00000E+00
    RHS1      LIM2       1.00000E+01
    RHS1      MYEQN      7.00000E+00
BOUNDS
 UP BND1      XONE       4.00000E+00
 LO BND1      YTWO      -1.00000E+00
 UP BND1      YTWO       1.00000E+00
ENDATA
)");
}

} // end anonymous namespace
