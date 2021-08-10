#ifndef BCDB_OUTLINING_FUNCS_H
#define BCDB_OUTLINING_FUNCS_H

#include "memodb/Evaluator.h"
#include "memodb/Node.h"

namespace smout {

using memodb::Evaluator;
using memodb::Node;
using memodb::NodeOrCID;
using memodb::NodeRef;

NodeOrCID candidates(Evaluator &evaluator, NodeRef options, NodeRef func);
NodeOrCID grouped_candidates(Evaluator &evaluator, NodeRef options,
                             NodeRef mod);
NodeOrCID extracted_callees(Evaluator &evaluator, NodeRef func,
                            NodeRef node_sets);
NodeOrCID grouped_callees_for_function(Evaluator &evaluator, NodeRef options,
                                       NodeRef grouped_candidates,
                                       NodeRef func);
NodeOrCID grouped_callees(Evaluator &evaluator, NodeRef options, NodeRef mod);
NodeOrCID ilp_problem(Evaluator &evaluator, NodeRef options, NodeRef mod);
NodeOrCID greedy_solution(Evaluator &evaluator, NodeRef options, NodeRef mod);
NodeOrCID extracted_caller(Evaluator &evaluator, NodeRef func, NodeRef callees);
NodeOrCID optimized(Evaluator &evaluator, NodeRef options, NodeRef mod);
NodeOrCID refinements_for_group(Evaluator &evaluator, NodeRef options,
                                NodeRef members);
NodeOrCID grouped_refinements(Evaluator &evaluator, NodeRef options,
                              NodeRef mod);

extern const char *candidates_version;
extern const char *grouped_candidates_version;
extern const char *extracted_callees_version;
extern const char *grouped_callees_for_function_version;
extern const char *grouped_callees_version;
extern const char *ilp_problem_version;
extern const char *greedy_solution_version;
extern const char *extracted_caller_version;
extern const char *optimized_version;
extern const char *refinements_for_group_version;
extern const char *grouped_refinements_version;

void registerFuncs(Evaluator &evaluator);

} // end namespace smout

#endif // BCDB_OUTLINING_FUNCS_H
