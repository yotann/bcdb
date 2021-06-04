#ifndef MEMODB_SCRIPTING_H
#define MEMODB_SCRIPTING_H

#include <duktape.h>

namespace llvm {
class StringRef;
} // namespace llvm

namespace memodb {

duk_context *newScriptingContext();
void setUpScripting(duk_context *ctx, duk_idx_t parent_idx);
int runScriptingFile(duk_context *ctx, llvm::StringRef filename);
void startREPL(duk_context *ctx);

} // namespace memodb

#endif // MEMODB_SCRIPTING_H
