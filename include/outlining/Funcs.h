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
NodeOrCID extracted_callee(Evaluator &evaluator, NodeRef func, NodeRef nodes);
NodeOrCID unique_callees(Evaluator &evaluator, NodeRef candidates_options,
                         NodeRef mod);
NodeOrCID ilp_problem(Evaluator &evaluator, NodeRef options, NodeRef mod);
NodeOrCID greedy_solution(Evaluator &evaluator, NodeRef options, NodeRef mod);
NodeOrCID extracted_caller(Evaluator &evaluator, NodeRef func, NodeRef callees);
NodeOrCID optimized(Evaluator &evaluator, NodeRef options, NodeRef mod);
NodeOrCID equivalent_pairs_in_group(Evaluator &evaluator, NodeRef options,
                                    NodeRef mod, NodeRef group);
NodeOrCID equivalent_pairs(Evaluator &evaluator, NodeRef options, NodeRef mod);

} // end namespace smout

#endif // BCDB_OUTLINING_FUNCS_H
