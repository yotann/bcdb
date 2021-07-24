#include "outlining/LinearProgram.h"

#include <algorithm>
#include <map>

#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/raw_ostream.h>

using namespace bcdb;
using namespace llvm;

LinearProgram::LinearProgram(llvm::StringRef Name) : Name(Name) {}

void LinearProgram::writeFixedMPS(llvm::raw_ostream &OS) {
  // http://lpsolve.sourceforge.net/5.5/mps-format.htm
  // We use fixed MPS because Symphony seems to have trouble parsing free MPS.

  OS << "NAME          " << Name << "\n";

  auto writeFour = [&OS](StringRef f0, StringRef f1, StringRef f2, double f3) {
    OS << formatv(" {0,-2} {1,-8}  {2,-8}  {3,12:E5}\n", f0, f1, f2, f3);
  };

  auto writeThree = [&writeFour](StringRef f1, StringRef f2, double f3) {
    writeFour("", f1, f2, f3);
  };

  OS << "ROWS\n";
  OS << " N  " << ObjectiveName << "\n";
  for (size_t i = 0; i < Constraints.size(); i++) {
    if (Constraints[i].Type == Constraint::LE)
      OS << " L  ";
    else if (Constraints[i].Type == Constraint::GE)
      OS << " G  ";
    else
      OS << " E  ";
    OS << ConstraintNames[i] << "\n";
  }

  std::map<size_t, std::vector<std::pair<llvm::StringRef, double>>> Columns;
  for (const auto &Item : Objective.Items)
    Columns[Item.first.ID].emplace_back(ObjectiveName, Item.second);
  for (size_t i = 0; i < Constraints.size(); i++)
    for (const auto &Item : Constraints[i].LHS.Items)
      Columns[Item.first.ID].emplace_back(ConstraintNames[i], Item.second);

  OS << "COLUMNS\n";
  for (const auto &Column : Columns)
    for (const auto &Item : Column.second)
      if (Item.second != 0)
        writeThree(Vars[Column.first].Name, Item.first, Item.second);

  OS << "RHS\n";
  if (Objective.Constant != 0)
    writeThree("RHS1", ObjectiveName, Objective.Constant);
  for (size_t i = 0; i < Constraints.size(); i++)
    if (Constraints[i].LHS.Constant != 0)
      writeThree("RHS1", ConstraintNames[i], -Constraints[i].LHS.Constant);

  OS << "BOUNDS\n";
  for (const auto &Info : Vars) {
    if (Info.IsInteger) {
      if (Info.LowerBound == 0 && Info.UpperBound == 1) {
        writeFour("BV", "BND1", Info.Name, 1.0);
      } else {
        if (Info.LowerBound != 0 || !Info.UpperBound)
          writeFour("LI", "BND1", Info.Name,
                    static_cast<int>(*Info.LowerBound));
        if (Info.UpperBound)
          writeFour("UI", "BND1", Info.Name,
                    static_cast<int>(*Info.UpperBound));
      }
    } else {
      if (!Info.LowerBound && !Info.UpperBound) {
        writeFour("FR", "BND1", Info.Name, 1.0);
      } else if (!Info.LowerBound) {
        writeFour("MI", "BND1", Info.Name, 1.0);
        if (Info.UpperBound != 0)
          writeFour("UP", "BND1", Info.Name, *Info.UpperBound);
      } else {
        if (Info.LowerBound != 0)
          writeFour("LO", "BND1", Info.Name, *Info.LowerBound);
        if (Info.UpperBound)
          writeFour("UP", "BND1", Info.Name, *Info.UpperBound);
      }
    }
  }

  OS << "ENDATA\n";
}

void LinearProgram::addConstraint(llvm::StringRef Name,
                                  Constraint &&Constraint) {
  assert(Name.size() <= 8 && "name too long for fixed MPS");
  Constraint.LHS.reduce();
  ConstraintNames.emplace_back(Name);
  Constraints.emplace_back(std::move(Constraint));
}

void LinearProgram::setObjective(llvm::StringRef Name, Expr &&Objective) {
  assert(Name.size() <= 8 && "name too long for fixed MPS");
  Objective.reduce();
  ObjectiveName = Name;
  this->Objective = std::move(Objective);
}

LinearProgram::Var LinearProgram::makeBoolVar(llvm::StringRef Name) {
  return makeIntVar(Name, 0, 1);
}

LinearProgram::Var LinearProgram::makeIntVar(llvm::StringRef Name,
                                             std::optional<int> LowerBound,
                                             std::optional<int> UpperBound) {
  assert(Name.size() <= 8 && "name too long for fixed MPS");
  size_t ID = Vars.size();
  Vars.emplace_back(VarInfo{std::string(Name), true, LowerBound, UpperBound});
  return Var{ID};
}

LinearProgram::Var
LinearProgram::makeRealVar(llvm::StringRef Name,
                           std::optional<double> LowerBound,
                           std::optional<double> UpperBound) {
  assert(Name.size() <= 8 && "name too long for fixed MPS");
  size_t ID = Vars.size();
  Vars.emplace_back(VarInfo{std::string(Name), false, LowerBound, UpperBound});
  return Var{ID};
}

LinearProgram::Expr &
LinearProgram::Expr::operator+=(const LinearProgram::Expr &Other) {
  Constant += Other.Constant;
  Items.append(Other.Items.begin(), Other.Items.end());
  return *this;
}

LinearProgram::Expr &
LinearProgram::Expr::operator-=(const LinearProgram::Expr &Other) {
  Constant -= Other.Constant;
  for (const auto &Item : Other.Items)
    Items.emplace_back(Item.first, -Item.second);
  return *this;
}

LinearProgram::Expr &LinearProgram::Expr::operator*=(double Other) {
  Constant *= Other;
  for (auto &Item : Items)
    Item.second *= Other;
  return *this;
}

void LinearProgram::Expr::reduce() {
  std::sort(Items.begin(), Items.end(), [](const auto &a, const auto &b) {
    return a.first.ID < b.first.ID;
  });
  llvm::SmallVector<std::pair<Var, double>, 2> new_items;
  for (const auto &item : Items) {
    // TODO: delete items when they cancel out (item.second == 0).
    if (!new_items.empty() && item.first.ID == new_items.back().first.ID)
      new_items.back().second += item.second;
    else
      new_items.emplace_back(item);
  }
  std::swap(Items, new_items);
}
