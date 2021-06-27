#include "bcdb/Outlining/LinearProgram.h"

#include <llvm/Support/raw_ostream.h>
#include <map>

using namespace bcdb;
using namespace llvm;

LinearProgram::LinearProgram(llvm::StringRef Name) : Name(Name) {}

void LinearProgram::writeFreeMPS(llvm::raw_ostream &OS) {
  // http://lpsolve.sourceforge.net/5.5/mps-format.htm

  OS << "NAME " << Name << "\n";

  OS << "ROWS\n";
  OS << " N " << ObjectiveName << "\n";
  for (size_t i = 0; i < Constraints.size(); i++) {
    if (Constraints[i].Type == Constraint::LE)
      OS << " L ";
    else if (Constraints[i].Type == Constraint::GE)
      OS << " G ";
    else
      OS << " E ";
    OS << ConstraintNames[i] << "\n";
  }

  std::map<size_t, std::vector<std::pair<llvm::StringRef, double>>> Columns;
  for (const auto &Item : Objective.Items)
    Columns[Item.first.ID].emplace_back(ObjectiveName, Item.second);
  for (size_t i = 0; i < Constraints.size(); i++)
    for (const auto &Item : Constraints[i].LHS.Items)
      Columns[Item.first.ID].emplace_back(ConstraintNames[i], Item.second);

  OS << "COLUMNS\n";
  for (const auto &Column : Columns) {
    for (const auto &Item : Column.second) {
      if (Item.second != 0) {
        OS << " " << Vars[Column.first].Name << " " << Item.first << " "
           << Item.second << "\n";
      }
    }
  }

  OS << "RHS\n";
  if (Objective.Constant != 0)
    OS << " RHS1 " << ObjectiveName << " " << Objective.Constant << "\n";
  for (size_t i = 0; i < Constraints.size(); i++)
    if (Constraints[i].LHS.Constant != 0)
      OS << " RHS1 " << ConstraintNames[i] << " "
         << -Constraints[i].LHS.Constant << "\n";

  OS << "BOUNDS\n";
  for (const auto &Info : Vars) {
    if (Info.IsInteger) {
      if (Info.LowerBound == 0 && Info.UpperBound == 1) {
        OS << "BV BND1 " << Info.Name << "\n";
      } else {
        if (Info.LowerBound != 0 || !Info.UpperBound)
          OS << "LI BND1 " << Info.Name << " "
             << static_cast<int>(*Info.LowerBound) << "\n";
        if (Info.UpperBound)
          OS << "UI BND1 " << Info.Name << " "
             << static_cast<int>(*Info.UpperBound) << "\n";
      }
    } else {
      if (!Info.LowerBound && !Info.UpperBound) {
        OS << " FR BND1 " << Info.Name << "\n";
      } else if (!Info.LowerBound) {
        OS << " MI BND1 " << Info.Name << "\n";
        if (Info.UpperBound != 0)
          OS << " UP BND1 " << Info.Name << " " << *Info.UpperBound << "\n";
      } else {
        if (Info.LowerBound != 0)
          OS << " LO BND1 " << Info.Name << " " << *Info.LowerBound << "\n";
        if (Info.UpperBound)
          OS << " UP BND1 " << Info.Name << " " << *Info.UpperBound << "\n";
      }
    }
  }

  OS << "ENDATA\n";
}

LinearProgram::Var LinearProgram::makeBoolVar(llvm::StringRef Name) {
  return makeIntVar(Name, 0, 1);
}

LinearProgram::Var LinearProgram::makeIntVar(llvm::StringRef Name,
                                             std::optional<int> LowerBound,
                                             std::optional<int> UpperBound) {
  size_t ID = Vars.size();
  Vars.emplace_back(VarInfo{std::string(Name), true, LowerBound, UpperBound});
  return Var{ID};
}

LinearProgram::Var
LinearProgram::makeRealVar(llvm::StringRef Name,
                           std::optional<double> LowerBound,
                           std::optional<double> UpperBound) {
  size_t ID = Vars.size();
  Vars.emplace_back(VarInfo{std::string(Name), false, LowerBound, UpperBound});
  return Var{ID};
}

LinearProgram::Expr &
LinearProgram::Expr::operator+=(const LinearProgram::Expr &Other) {
  Constant += Other.Constant;
  for (const auto &Item : Other.Items) {
    bool Found = false;
    for (auto &ResultItem : Items) {
      if (ResultItem.first.ID == Item.first.ID) {
        ResultItem.second += Item.second;
        Found = true;
        break;
      }
    }
    if (!Found)
      Items.emplace_back(Item);
  }
  return *this;
}

LinearProgram::Expr &
LinearProgram::Expr::operator-=(const LinearProgram::Expr &Other) {
  Constant -= Other.Constant;
  for (const auto &Item : Other.Items) {
    bool Found = false;
    for (auto &ResultItem : Items) {
      if (ResultItem.first.ID == Item.first.ID) {
        ResultItem.second -= Item.second;
        Found = true;
        break;
      }
    }
    if (!Found)
      Items.emplace_back(Item.first, -Item.second);
  }
  return *this;
}

LinearProgram::Expr &LinearProgram::Expr::operator*=(double Other) {
  Constant *= Other;
  for (auto &Item : Items)
    Item.second *= Other;
  return *this;
}
