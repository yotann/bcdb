#ifndef BCDB_OUTLINING_FUNCS_H
#define BCDB_OUTLINING_FUNCS_H

#include "memodb/Evaluator.h"
#include "memodb/Node.h"

namespace smout {

using memodb::Evaluator;
using memodb::Link;
using memodb::Node;
using memodb::NodeOrCID;

NodeOrCID actual_size(Evaluator &evaluator, Link func);
NodeOrCID candidates(Evaluator &evaluator, Link options, Link func);
NodeOrCID grouped_candidates(Evaluator &evaluator, Link options, Link mod);
NodeOrCID extracted_callees(Evaluator &evaluator, Link func, Link node_sets);
NodeOrCID grouped_callees_for_function(Evaluator &evaluator, Link options,
                                       Link grouped_candidates, Link func);
NodeOrCID grouped_callees(Evaluator &evaluator, Link options, Link mod);
NodeOrCID ilp_problem(Evaluator &evaluator, Link options, Link mod);
NodeOrCID greedy_solution(Evaluator &evaluator, Link options, Link mod);
NodeOrCID extracted_caller(Evaluator &evaluator, Link func, Link callees);
NodeOrCID outlined_module(Evaluator &evaluator, Link mod, Link solution);
NodeOrCID optimized(Evaluator &evaluator, Link options, Link mod);
NodeOrCID refinements_for_set(Evaluator &evaluator, Link options, Link members);
NodeOrCID refinements_for_group(Evaluator &evaluator, Link options,
                                Link members);
NodeOrCID grouped_refinements(Evaluator &evaluator, Link options, Link mod);

extern const char *actual_size_version;
extern const char *candidates_version;
extern const char *grouped_candidates_version;
extern const char *extracted_callees_version;
extern const char *grouped_callees_for_function_version;
extern const char *grouped_callees_version;
extern const char *ilp_problem_version;
extern const char *greedy_solution_version;
extern const char *extracted_caller_version;
extern const char *outlined_module_version;
extern const char *optimized_version;
extern const char *refinements_for_set_version;
extern const char *refinements_for_group_version;
extern const char *grouped_refinements_version;

void registerFuncs(Evaluator &evaluator);

} // end namespace smout

#endif // BCDB_OUTLINING_FUNCS_H
