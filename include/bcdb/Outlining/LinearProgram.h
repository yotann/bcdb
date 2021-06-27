#ifndef BCDB_LINEARPROGRAM_H
#define BCDB_LINEARPROGRAM_H

#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
class raw_ostream;
}

namespace bcdb {

class LinearProgram {
public:
  class Var {
    size_t ID;
    Var() = delete;
    Var(size_t ID) : ID(ID) {}

    friend class LinearProgram;
  };

  class Expr {
    friend class LinearProgram;

    std::vector<std::pair<Var, double>> Items = {};
    double Constant = 0;

  public:
    Expr() {}
    Expr(double Constant) : Constant(Constant) {}
    Expr(Var X) : Items{{X, 1.0}} {}

    Expr &operator+=(const Expr &Other);
    Expr &operator-=(const Expr &Other);
    Expr &operator*=(double Other);
  };

  class Constraint {
    friend class LinearProgram;
    friend LinearProgram::Constraint operator<=(LinearProgram::Expr &&A,
                                                const LinearProgram::Expr &B);
    friend LinearProgram::Constraint operator>=(LinearProgram::Expr &&A,
                                                const LinearProgram::Expr &B);
    friend LinearProgram::Constraint operator==(LinearProgram::Expr &&A,
                                                const LinearProgram::Expr &B);

    Expr LHS;
    enum { LE, GE, EQ } Type;
  };

  LinearProgram(llvm::StringRef Name);
  void writeFreeMPS(llvm::raw_ostream &OS);

  void addConstraint(llvm::StringRef Name, Constraint &&Constraint) {
    ConstraintNames.emplace_back(Name);
    Constraints.emplace_back(std::move(Constraint));
  }

  void setObjective(llvm::StringRef Name, Expr &&Objective) {
    ObjectiveName = Name;
    this->Objective = std::move(Objective);
  }

  Var makeBoolVar(llvm::StringRef Name);
  Var makeIntVar(llvm::StringRef Name, std::optional<int> LowerBound,
                 std::optional<int> UpperBound);
  Var makeRealVar(llvm::StringRef Name, std::optional<double> LowerBound,
                  std::optional<double> UpperBound);

private:
  struct VarInfo {
    std::string Name;
    bool IsInteger;
    std::optional<double> LowerBound, UpperBound;
  };

  std::string Name;
  std::vector<VarInfo> Vars;
  std::string ObjectiveName;
  Expr Objective;
  std::vector<std::string> ConstraintNames;
  std::vector<Constraint> Constraints;
};

inline LinearProgram::Expr operator+(LinearProgram::Expr &&A,
                                     const LinearProgram::Expr &B) {
  A += B;
  return std::move(A);
}

inline LinearProgram::Expr operator-(LinearProgram::Expr &&A,
                                     const LinearProgram::Expr &B) {
  A -= B;
  return std::move(A);
}

inline LinearProgram::Expr operator*(LinearProgram::Expr &&A, double B) {
  A *= B;
  return std::move(A);
}

inline LinearProgram::Expr operator*(double A, LinearProgram::Expr &&B) {
  B *= A;
  return std::move(B);
}

inline LinearProgram::Constraint operator<=(LinearProgram::Expr &&A,
                                            const LinearProgram::Expr &B) {
  LinearProgram::Constraint Result;
  Result.Type = LinearProgram::Constraint::LE;
  Result.LHS = std::move(A) - B;
  return Result;
}

inline LinearProgram::Constraint operator>=(LinearProgram::Expr &&A,
                                            const LinearProgram::Expr &B) {
  LinearProgram::Constraint Result;
  Result.Type = LinearProgram::Constraint::GE;
  Result.LHS = std::move(A) - B;
  return Result;
}

inline LinearProgram::Constraint operator==(LinearProgram::Expr &&A,
                                            const LinearProgram::Expr &B) {
  LinearProgram::Constraint Result;
  Result.Type = LinearProgram::Constraint::EQ;
  Result.LHS = std::move(A) - B;
  return Result;
}

inline LinearProgram::Constraint operator<=(const LinearProgram::Expr &A,
                                            const LinearProgram::Expr &B) {
  LinearProgram::Expr LHS = A;
  return std::move(LHS) <= B;
}

inline LinearProgram::Constraint operator>=(const LinearProgram::Expr &A,
                                            const LinearProgram::Expr &B) {
  LinearProgram::Expr LHS = A;
  return std::move(LHS) >= B;
}

inline LinearProgram::Constraint operator==(const LinearProgram::Expr &A,
                                            const LinearProgram::Expr &B) {
  LinearProgram::Expr LHS = A;
  return std::move(LHS) == B;
}

} // namespace bcdb

#endif // BCDB_LINEARPROGRAM_H
