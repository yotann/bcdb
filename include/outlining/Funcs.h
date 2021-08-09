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
NodeOrCID candidates_total(Evaluator &evaluator, NodeRef options, NodeRef mod);
NodeOrCID grouped_candidates(Evaluator &evaluator, NodeRef options,
                             NodeRef mod);
NodeOrCID extracted_callees(Evaluator &evaluator, NodeRef func,
                            NodeRef node_sets);
NodeOrCID unique_callees(Evaluator &evaluator, NodeRef candidates_options,
                         NodeRef mod);
NodeOrCID ilp_problem(Evaluator &evaluator, NodeRef options, NodeRef mod);
NodeOrCID greedy_solution(Evaluator &evaluator, NodeRef options, NodeRef mod);
NodeOrCID extracted_caller(Evaluator &evaluator, NodeRef func, NodeRef callees);
NodeOrCID optimized(Evaluator &evaluator, NodeRef options, NodeRef mod);
NodeOrCID equivalent_pairs_in_group(Evaluator &evaluator, NodeRef options,
                                    NodeRef mod, NodeRef group);
NodeOrCID equivalent_pairs(Evaluator &evaluator, NodeRef options, NodeRef mod);

extern const char *candidates_version;
extern const char *candidates_total_version;
extern const char *grouped_candidates_version;
extern const char *extracted_callees_version;
extern const char *unique_callees_version;
extern const char *ilp_problem_version;
extern const char *greedy_solution_version;
extern const char *extracted_caller_version;
extern const char *optimized_version;
extern const char *equivalent_pairs_in_group_version;
extern const char *equivalent_pairs_version;

} // end namespace smout

#endif // BCDB_OUTLINING_FUNCS_H
