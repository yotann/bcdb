#include "memodb/Scripting.h"

#include <cstring>
#include <duktape.h>
#include <linenoise.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>

#include "memodb/Multibase.h"

using namespace memodb;

template <class T> struct unimplemented : std::false_type {};

template <class T, class Enable = void> struct ScriptingTypeTraits {
  static bool is(duk_context *ctx, duk_idx_t idx) { return false; }

  static T require(duk_context *ctx, duk_idx_t idx) {
    static_assert(unimplemented<T>::value, "not implemented");
  }

  static void push(duk_context *ctx, const T &value) {
    static_assert(unimplemented<T>::value, "not implemented");
  }
};

template <> struct ScriptingTypeTraits<const char *> {
  static void push(duk_context *ctx, const char *value) {
    duk_push_string(ctx, value);
  }
};

template <> struct ScriptingTypeTraits<llvm::StringRef> {
  static bool is(duk_context *ctx, duk_idx_t idx) {
    return duk_is_string(ctx, idx);
  }

  static llvm::StringRef require(duk_context *ctx, duk_idx_t idx) {
    duk_size_t size;
    const char *ptr = duk_require_lstring(ctx, idx, &size);
    return llvm::StringRef(ptr, size);
  }

  static void push(duk_context *ctx, const llvm::StringRef &value) {
    duk_push_lstring(ctx, value.data(), value.size());
  }
};

template <> struct ScriptingTypeTraits<std::string> {
  static bool is(duk_context *ctx, duk_idx_t idx) {
    return ScriptingTypeTraits<llvm::StringRef>::is(ctx, idx);
  }

  static std::string require(duk_context *ctx, duk_idx_t idx) {
    return ScriptingTypeTraits<llvm::StringRef>::require(ctx, idx).str();
  }

  static void push(duk_context *ctx, const std::string &value) {
    ScriptingTypeTraits<llvm::StringRef>::push(ctx, value);
  }
};

template <> struct ScriptingTypeTraits<llvm::ArrayRef<std::uint8_t>> {
  static bool is(duk_context *ctx, duk_idx_t idx) {
    return duk_is_buffer(ctx, idx);
  }

  static llvm::ArrayRef<std::uint8_t> require(duk_context *ctx, duk_idx_t idx) {
    duk_size_t size;
    void *ptr = duk_require_buffer_data(ctx, idx, &size);
    return llvm::ArrayRef<std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(ptr), size);
  }

  static void push(duk_context *ctx,
                   const llvm::ArrayRef<std::uint8_t> &value) {
    void *buffer = duk_push_fixed_buffer(ctx, value.size());
    if (!value.empty())
      std::memcpy(buffer, value.data(), value.size());
    duk_push_buffer_object(ctx, -1, 0, value.size(), DUK_BUFOBJ_UINT8ARRAY);
  }
};

template <> struct ScriptingTypeTraits<std::vector<std::uint8_t>> {
  static bool is(duk_context *ctx, duk_idx_t idx) {
    return ScriptingTypeTraits<llvm::ArrayRef<std::uint8_t>>::is(ctx, idx);
  }

  static std::vector<std::uint8_t> require(duk_context *ctx, duk_idx_t idx) {
    return ScriptingTypeTraits<llvm::ArrayRef<std::uint8_t>>::require(ctx, idx);
  }

  static void push(duk_context *ctx, const std::vector<std::uint8_t> &value) {
    ScriptingTypeTraits<llvm::ArrayRef<std::uint8_t>>::push(ctx, value);
  }
};

template <class T> struct ScriptingTypeTraits<std::optional<T>> {
  static bool is(duk_context *ctx, duk_idx_t idx) {
    return duk_is_null_or_undefined(ctx, idx) ||
           ScriptingTypeTraits<T>::is(ctx, idx);
  }

  static std::optional<T> require(duk_context *ctx, duk_idx_t idx) {
    if (duk_is_null_or_undefined(ctx, idx))
      return {};
    return ScriptingTypeTraits<T>::require(ctx, idx);
  }

  static void push(duk_context *ctx, const std::optional<T> &value) {
    if (value)
      ScriptingTypeTraits<T>::push(ctx, *value);
    else
      duk_push_null(ctx);
  }
};

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

template <> struct ScriptingTypeTraits<const Multibase *> {
  static bool is(duk_context *ctx, duk_idx_t idx) {
    duk_get_prop_literal(ctx, idx, DUK_HIDDEN_SYMBOL("Multibase"));
    bool result = duk_is_pointer(ctx, -1);
    duk_pop(ctx);
    return result;
  }

  static const Multibase *require(duk_context *ctx, duk_idx_t idx) {
    duk_get_prop_literal(ctx, idx, DUK_HIDDEN_SYMBOL("Multibase"));
    const Multibase *result =
        reinterpret_cast<const Multibase *>(duk_require_pointer(ctx, -1));
    duk_pop(ctx);
    return result;
  }
};

template <bool member_function, typename R, typename... ArgTypes>
class FunctionWrapper {
  template <std::size_t... ints>
  static duk_ret_t wrapper_helper(duk_context *ctx,
                                  std::index_sequence<ints...>) {
    if (member_function) {
      // Insert this as first argument.
      duk_push_this(ctx);
      duk_insert(ctx, 0);
    }
    duk_push_current_function(ctx);
    duk_get_prop_literal(ctx, -1, DUK_HIDDEN_SYMBOL("FunctionWrapper"));
    auto &func =
        *reinterpret_cast<FunctionWrapper *>(duk_require_pointer(ctx, -1));
    R result =
        func.function_(ScriptingTypeTraits<ArgTypes>::require(ctx, ints)...);
    ScriptingTypeTraits<R>::push(ctx, result);
    return 1;
  }

  static duk_ret_t wrapper(duk_context *ctx) {
    return wrapper_helper(ctx, std::index_sequence_for<ArgTypes...>{});
  }

  static duk_ret_t finalizer(duk_context *ctx) {
    duk_get_prop_literal(ctx, 0, DUK_HIDDEN_SYMBOL("FunctionWrapper"));
    delete reinterpret_cast<FunctionWrapper *>(duk_require_pointer(ctx, -1));
    return 0;
  }

  std::function<R(ArgTypes...)> function_;

public:
  template <class F>
  FunctionWrapper(duk_context *ctx, llvm::StringRef name, F f) : function_(f) {
    duk_push_c_function(ctx, &wrapper, sizeof...(ArgTypes));
    duk_push_pointer(ctx, this);
    duk_put_prop_literal(ctx, -2, DUK_HIDDEN_SYMBOL("FunctionWrapper"));
    duk_push_literal(ctx, "name");
    duk_push_lstring(ctx, name.data(), name.size());
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_FORCE);
    duk_push_c_lightfunc(ctx, &finalizer, 1, 1, 0);
    duk_set_finalizer(ctx, -2);
  }
};

template <class R, class... ArgTypes>
FunctionWrapper(duk_context *, llvm::StringRef, R(ArgTypes...))
    -> FunctionWrapper<false, R, ArgTypes...>;
template <class R, class C, class... ArgTypes>
FunctionWrapper(duk_context *, llvm::StringRef, R (C::*)(ArgTypes...))
    -> FunctionWrapper<true, R, C *, ArgTypes...>;
template <class R, class C, class... ArgTypes>
FunctionWrapper(duk_context *, llvm::StringRef, R (C::*)(ArgTypes...) const)
    -> FunctionWrapper<true, R, const C *, ArgTypes...>;

template <class F>
static void defineScriptingFunction(duk_context *ctx, duk_idx_t parent_idx,
                                    llvm::StringRef full_name, F f) {
  parent_idx = duk_require_normalize_index(ctx, parent_idx);
  llvm::StringRef name = full_name.rsplit("::").second;
  duk_push_lstring(ctx, name.data(), name.size());
  new FunctionWrapper(ctx, full_name, f);
  duk_def_prop(ctx, parent_idx,
               DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_ENUMERABLE |
                   DUK_DEFPROP_ENUMERABLE);
}

template <class T>
static void defineScriptingValue(duk_context *ctx, duk_idx_t parent_idx,
                                 llvm::StringRef name, const T &value) {
  parent_idx = duk_require_normalize_index(ctx, parent_idx);
  duk_push_lstring(ctx, name.data(), name.size());
  ScriptingTypeTraits<T>::push(ctx, value);
  duk_def_prop(ctx, parent_idx,
               DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_ENUMERABLE |
                   DUK_DEFPROP_ENUMERABLE);
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

  duk_push_object(ctx); // Multibase prototype
  defineScriptingFunction(ctx, -1, "Multibase::decodeWithoutPrefix",
                          &Multibase::decodeWithoutPrefix);
  defineScriptingFunction(ctx, -1, "Multibase::encode", &Multibase::encode);
  defineScriptingFunction(ctx, -1, "Multibase::encodeWithoutPrefix",
                          &Multibase::encodeWithoutPrefix);
  duk_freeze(ctx, -1);
  duk_push_object(ctx); // Multibase
  defineScriptingFunction(ctx, -1, "Multibase::decode", &Multibase::decode);
  Multibase::eachBase([&](const Multibase &base) {
    duk_push_string(ctx, base.name);
    duk_push_object(ctx); // Multibase instance
    duk_dup(ctx, -4);
    duk_set_prototype(ctx, -2);
    duk_push_pointer(ctx, const_cast<void *>((const void *)&base));
    duk_put_prop_literal(ctx, -2, DUK_HIDDEN_SYMBOL("Multibase"));
    defineScriptingValue(ctx, -1, "prefix", llvm::StringRef(&base.prefix, 1));
    defineScriptingValue(ctx, -1, "name", base.name);
    duk_freeze(ctx, -1);
    duk_def_prop(ctx, -3,
                 DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_ENUMERABLE |
                     DUK_DEFPROP_ENUMERABLE);
  });
  duk_freeze(ctx, -1);
  duk_put_prop_literal(ctx, parent_idx, "Multibase");
  duk_pop(ctx); // Multibase prototype

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
