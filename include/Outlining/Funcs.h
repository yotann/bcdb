#ifndef BCDB_OUTLINING_FUNCS_H
#define BCDB_OUTLINING_FUNCS_H

#include "memodb/Evaluator.h"
#include "memodb/Node.h"

namespace smout {

using memodb::Evaluator;
using memodb::Node;

Node candidates(Evaluator &evaluator, const Node &func);
Node candidates_total(Evaluator &evaluator, const Node &mod);

} // end namespace smout

#endif // BCDB_OUTLINING_FUNCS_H
