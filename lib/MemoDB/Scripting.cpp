#include "memodb/Scripting.h"

#include <cstring>
#include <duktape.h>
#include <linenoise.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>

#include "memodb/Multibase.h"

using namespace memodb;

static void fatal_handler(void *, const char *Msg) {
  llvm::report_fatal_error(Msg);
}

duk_context *memodb::newScriptingContext() {
  duk_context *ctx =
      duk_create_heap(nullptr, nullptr, nullptr, nullptr, fatal_handler);
  if (!ctx)
    llvm::report_fatal_error("Couldn't create Duktape heap");
  return ctx;
}

static duk_ret_t print_alert(duk_context *ctx) {
  duk_push_literal(ctx, " ");
  duk_insert(ctx, 0);
  duk_join(ctx, duk_get_top(ctx) - 1);
  (duk_get_current_magic(ctx) == 2 ? llvm::errs() : llvm::outs())
      << duk_require_string(ctx, -1) << '\n';
  return 0;
}

static duk_ret_t multibase_decode(duk_context *ctx) {
  duk_size_t arg_len;
  const char *arg = duk_require_lstring(ctx, 0, &arg_len);
  auto result = Multibase::decode(llvm::StringRef(arg, arg_len));
  if (!result)
    return 0;
  void *buffer = duk_push_fixed_buffer(ctx, result->size());
  if (!result->empty())
    std::memcpy(buffer, result->data(), result->size());
  duk_push_buffer_object(ctx, -1, 0, result->size(), DUK_BUFOBJ_UINT8ARRAY);
  return 1;
}

static duk_ret_t multibase_decodeWithoutPrefix(duk_context *ctx) {
  duk_push_this(ctx);
  duk_get_prop_literal(ctx, -1, DUK_HIDDEN_SYMBOL("Multibase"));
  auto base = reinterpret_cast<const Multibase *>(duk_require_pointer(ctx, -1));
  duk_size_t arg_len;
  const char *arg = duk_require_lstring(ctx, 0, &arg_len);
  auto result = base->decodeWithoutPrefix(llvm::StringRef(arg, arg_len));
  if (!result)
    return 0;
  void *buffer = duk_push_fixed_buffer(ctx, result->size());
  if (!result->empty())
    std::memcpy(buffer, result->data(), result->size());
  duk_push_buffer_object(ctx, -1, 0, result->size(), DUK_BUFOBJ_UINT8ARRAY);
  return 1;
}

static duk_ret_t multibase_encode(duk_context *ctx) {
  duk_push_this(ctx);
  duk_get_prop_literal(ctx, -1, DUK_HIDDEN_SYMBOL("Multibase"));
  auto base = reinterpret_cast<const Multibase *>(duk_require_pointer(ctx, -1));
  duk_size_t arg_size;
  void *arg_ptr = duk_require_buffer_data(ctx, 0, &arg_size);
  llvm::ArrayRef<std::uint8_t> arg(
      reinterpret_cast<const std::uint8_t *>(arg_ptr), arg_size);
  auto result = base->encode(arg);
  duk_push_lstring(ctx, result.data(), result.size());
  return 1;
}

static duk_ret_t multibase_encodeWithoutPrefix(duk_context *ctx) {
  duk_push_this(ctx);
  duk_get_prop_literal(ctx, -1, DUK_HIDDEN_SYMBOL("Multibase"));
  auto base = reinterpret_cast<const Multibase *>(duk_require_pointer(ctx, -1));
  duk_size_t arg_size;
  void *arg_ptr = duk_require_buffer_data(ctx, 0, &arg_size);
  llvm::ArrayRef<std::uint8_t> arg(
      reinterpret_cast<const std::uint8_t *>(arg_ptr), arg_size);
  auto result = base->encodeWithoutPrefix(arg);
  duk_push_lstring(ctx, result.data(), result.size());
  return 1;
}

namespace {
#include "scripting_init.inc"
}

void memodb::setUpScripting(duk_context *ctx, duk_idx_t parent_idx) {
  parent_idx = duk_require_normalize_index(ctx, parent_idx);

  duk_push_c_lightfunc(ctx, print_alert, DUK_VARARGS, 1, 1);
  duk_put_global_literal(ctx, "print");
  duk_push_c_lightfunc(ctx, print_alert, DUK_VARARGS, 1, 2);
  duk_put_global_literal(ctx, "alert");

  duk_push_object(ctx); // Multibase

  duk_push_literal(ctx, "decode");
  duk_push_c_function(ctx, multibase_decode, 1);
  duk_push_literal(ctx, "name");
  duk_push_literal(ctx, "Multibase::decode");
  duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_FORCE);
  duk_def_prop(ctx, -3,
               DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_WRITABLE |
                   DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_HAVE_CONFIGURABLE |
                   DUK_DEFPROP_ENUMERABLE);

  duk_push_object(ctx); // Multibase prototype

  duk_push_literal(ctx, "decodeWithoutPrefix");
  duk_push_c_function(ctx, multibase_decodeWithoutPrefix, 1);
  duk_push_literal(ctx, "name");
  duk_push_literal(ctx, "Multibase::decodeWithoutPrefix");
  duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_FORCE);
  duk_def_prop(ctx, -3,
               DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_WRITABLE |
                   DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_HAVE_CONFIGURABLE |
                   DUK_DEFPROP_ENUMERABLE);

  duk_push_literal(ctx, "encode");
  duk_push_c_function(ctx, multibase_encode, 1);
  duk_push_literal(ctx, "name");
  duk_push_literal(ctx, "Multibase::encode");
  duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_FORCE);
  duk_def_prop(ctx, -3,
               DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_WRITABLE |
                   DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_HAVE_CONFIGURABLE |
                   DUK_DEFPROP_ENUMERABLE);

  duk_push_literal(ctx, "encodeWithoutPrefix");
  duk_push_c_function(ctx, multibase_encodeWithoutPrefix, 1);
  duk_push_literal(ctx, "name");
  duk_push_literal(ctx, "Multibase::encodeWithoutPrefix");
  duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_FORCE);
  duk_def_prop(ctx, -3,
               DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_WRITABLE |
                   DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_HAVE_CONFIGURABLE |
                   DUK_DEFPROP_ENUMERABLE);
  duk_freeze(ctx, -1);

  Multibase::eachBase([&](const Multibase &base) {
    duk_push_string(ctx, base.name);
    duk_push_object(ctx); // Multibase instance

    duk_dup(ctx, -3);
    duk_set_prototype(ctx, -2);

    duk_push_pointer(ctx, const_cast<void *>((const void *)&base));
    duk_put_prop_literal(ctx, -2, DUK_HIDDEN_SYMBOL("Multibase"));

    duk_push_literal(ctx, "prefix");
    duk_push_lstring(ctx, &base.prefix, 1);
    duk_def_prop(ctx, -3,
                 DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_ENUMERABLE |
                     DUK_DEFPROP_ENUMERABLE);

    duk_push_literal(ctx, "name");
    duk_push_string(ctx, base.name);
    duk_def_prop(ctx, -3,
                 DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_ENUMERABLE |
                     DUK_DEFPROP_ENUMERABLE);

    duk_freeze(ctx, -1);
    duk_def_prop(ctx, -4,
                 DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_ENUMERABLE |
                     DUK_DEFPROP_ENUMERABLE);
  });

  duk_freeze(ctx, -1);
  duk_pop(ctx); // Multibase prototype

  duk_put_prop_literal(ctx, parent_idx, "Multibase");

  duk_eval_lstring(ctx, reinterpret_cast<const char *>(scripting_init_js),
                   scripting_init_js_len);
  duk_push_global_stash(ctx);
  duk_call(ctx, 1);
}

int memodb::runScriptingFile(duk_context *ctx, llvm::StringRef filename) {
  auto mb_or_err = llvm::MemoryBuffer::getFile(filename);
  if (!mb_or_err) {
    llvm::errs() << "Could not read file \"" << filename << "\"\n";
    return 1;
  }
  duk_push_lstring(ctx, filename.data(), filename.size());
  auto rc = duk_pcompile_lstring_filename(
      ctx, 0, (*mb_or_err)->getBufferStart(), (*mb_or_err)->getBufferSize());
  if (rc) {
    llvm::errs() << "Could not compile file \"" << filename << "\":\n";
    llvm::errs() << duk_safe_to_string(ctx, -1);
    return 1;
  } else {
    rc = duk_pcall(ctx, 0);
    if (rc) {
      llvm::errs() << "Error running file \"" << filename << "\":\n";
      llvm::errs() << duk_safe_to_stacktrace(ctx, -1);
      duk_pop(ctx);
      return 1;
    }
    duk_pop(ctx);
    return 0;
  }
}

static duk_context *g_completion_ctx = nullptr;

static duk_ret_t repl_add_completion(duk_context *ctx) {
  const char *Str = duk_require_string(ctx, 0);
  auto Arg =
      reinterpret_cast<linenoiseCompletions *>(duk_require_pointer(ctx, 1));
  linenoiseAddCompletion(Arg, Str);
  return 0;
}

static void repl_completion(const char *Input, linenoiseCompletions *Arg) {
  if (!g_completion_ctx)
    return;
  duk_push_global_stash(g_completion_ctx);
  duk_get_prop_string(g_completion_ctx, -1, "linenoiseCompletion");
  duk_push_string(g_completion_ctx, Input ? Input : "");
  duk_push_c_lightfunc(g_completion_ctx, repl_add_completion, 2, 2, 0);
  duk_push_pointer(g_completion_ctx, (void *)Arg);
  duk_call(g_completion_ctx, 3);
  duk_pop_2(g_completion_ctx);
}

static char *repl_hints(const char *Input, int *Color, int *Bold) {
  if (!g_completion_ctx)
    return nullptr;
  duk_push_global_stash(g_completion_ctx);
  duk_get_prop_string(g_completion_ctx, -1, "linenoiseHints");
  duk_push_string(g_completion_ctx, Input ? Input : "");
  duk_call(g_completion_ctx, 1);
  if (!duk_is_object(g_completion_ctx, -1)) {
    duk_pop_2(g_completion_ctx);
    return nullptr;
  }

  duk_get_prop_literal(g_completion_ctx, -1, "hints");
  const char *Tmp = duk_get_string(g_completion_ctx, -1);
  char *Result = Tmp ? strdup(Tmp) : nullptr;
  duk_pop(g_completion_ctx);

  duk_get_prop_literal(g_completion_ctx, -1, "color");
  *Color = duk_get_int_default(g_completion_ctx, -1, -1);
  duk_pop(g_completion_ctx);

  duk_get_prop_literal(g_completion_ctx, -1, "bold");
  *Bold = duk_get_int_default(g_completion_ctx, -1, 0);
  duk_pop(g_completion_ctx);

  duk_pop_2(g_completion_ctx);
  return Result;
}

static void repl_free_hints(void *Hints) { free(Hints); }

void memodb::startREPL(duk_context *ctx) {
  duk_idx_t expected_top = duk_get_top(ctx);
  g_completion_ctx = ctx;
  linenoiseSetCompletionCallback(repl_completion);
  linenoiseSetFreeHintsCallback(repl_free_hints);
  linenoiseSetHintsCallback(repl_hints);
  linenoiseSetMultiLine(1);
  linenoiseHistorySetMaxLen(1000);

  while (char *line = linenoise("> ")) {
    assert(duk_get_top(ctx) == expected_top);
    linenoiseHistoryAdd(line);
    int RC = duk_peval_string(ctx, line);
    if (RC) {
      llvm::errs() << duk_safe_to_stacktrace(ctx, -1) << '\n';
      duk_pop(ctx);
    } else {
      duk_push_global_stash(ctx);
      duk_get_prop_literal(ctx, -1, "dukFormat");
      duk_dup(ctx, -3);
      duk_call(ctx, 1);
      llvm::outs() << "= " << duk_to_string(ctx, -1) << '\n';
      duk_pop_3(ctx);
    }
    llvm::outs().flush();
    llvm::errs().flush();
    linenoiseFree(line);
  }
}
