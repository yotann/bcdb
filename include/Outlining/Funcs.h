#ifndef BCDB_OUTLINING_FUNCS_H
#define BCDB_OUTLINING_FUNCS_H

#include "memodb/Node.h"
#include "memodb/Store.h"

namespace smout {

using memodb::Node;
using memodb::Store;

Node candidates(Store &store, const Node &func);

} // end namespace smout

#endif // BCDB_OUTLINING_FUNCS_H
