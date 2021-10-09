#include "bcdb/Context.h"

#include <algorithm>

#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

using namespace bcdb;
using namespace llvm;

static const std::array KNOWN_MD_KINDS = {
    // Fixed kinds from FixedMetadataKinds.def, already added by LLVMContext.
    "dbg",
    "tbaa",
    "prof",
    "fpmath",
    "range",
    "tbaa.struct",
    "invariant.load",
    "alias.scope",
    "noalias",
    "nontemporal",
    "llvm.mem.parallel_loop_access",
    "nonnull",
    "dereferenceable",
    "dereferenceable_or_null",
    "make.implicit",
    "unpredictable",
    "invariant.group",
    "align",
    "llvm.loop",
    "type",
    "section_prefix",
    "absolute_symbol",
    "associated",
    "callees",
    "irr_loop",
    "llvm.access.group",
    "callback",
    "llvm.preserve.access.index",
    "vcall_visibility",
    "noundef",
    "annotation",

    // Non-fixed kinds used by LLVM or Clang.
    "amdgpu.noclobber",
    "amdgpu.uniform",
    "callalign",
    "clang.arc.copy_on_escape",
    "clang.arc.no_objc_arc_exceptions",
    "clang.decl.ptr",
    "clang.imprecise_release",
    "GNUObjCMessageSend",
    "heapallocsite",
    "intel_reqd_sub_group_size",
    "is_base_value",
    "kernel_arg_access_qual",
    "kernel_arg_addr_space",
    "kernel_arg_base_type",
    "kernel_arg_name",
    "kernel_arg_type",
    "kernel_arg_type_qual",
    "nosanitize",
    "reqd_work_group_size",
    "srcloc",
    "structurizecfg.uniform",
    "vec_type_hint",
    "wasm.index",
    "work_group_size_hint",
};

#if LLVM_VERSION_MAJOR >= 11
static const std::array KNOWN_BUNDLE_TAGS = {
    // Fixed tags already added by LLVMContext.
    "deopt",
    "funclet",
    "gc-transition",
    "cfguardtarget",
    "preallocated",
    "gc-live",
    "clang.arc.attachedcall",

    // Non-fixed tags used by LLVM or Clang.
    "align",
    "ExplicitUse",
};
#endif

static const std::array KNOWN_SYNC_SCOPES = {
    // Fixed names already added by LLVMContext.
    "singlethread",
    "",

    // Non-fixed names used by LLVM or Clang.
    "agent",
    "agent-one-as",
    "one-as",
    "singlethread-one-as",
    "wavefront",
    "wavefront-one-as",
    "workgroup",
    "workgroup-one-as",
};

Context::Context() {
  // When writing a bitcode module, LLVM includes the names of all MD kinds
  // that were ever used in the LLVMContext, even if they aren't used by the
  // module in question. That can cause different bitcode to be created for
  // identical modules. To improve deduplication, we initialize the LLVMContext
  // with a consistent set of MD kinds.
  for (const auto &kind : KNOWN_MD_KINDS)
    context.getMDKindID(kind);

  // Same for operand bundle tags and sync scope names.
#if LLVM_VERSION_MAJOR >= 11
  for (const auto &tag : KNOWN_BUNDLE_TAGS)
    context.getOrInsertBundleTag(tag);
#endif
  for (const auto &ssn : KNOWN_SYNC_SCOPES)
    context.getOrInsertSyncScopeID(ssn);
}

Context::operator LLVMContext &() { return context; }

void Context::checkWarnings(const LLVMContext &context) {
  auto checkVec = [](const auto &vec, const auto &known, const char *type) {
    for (size_t i = 0; i < vec.size(); ++i) {
      if (i >= known.size() || known[i] != vec[i]) {
        errs() << "WARNING: unknown " << type << " \"" << vec[i]
               << "\", may prevent deduplication\n";
        return;
      }
    }
  };

  SmallVector<StringRef, 64> vec;
  context.getMDKindNames(vec);
  checkVec(vec, KNOWN_MD_KINDS, "MD kind");

#if LLVM_VERSION_MAJOR >= 11
  vec.clear();
  context.getOperandBundleTags(vec);
  checkVec(vec, KNOWN_BUNDLE_TAGS, "operand bundle");
#endif

  vec.clear();
  context.getSyncScopeNames(vec);
  checkVec(vec, KNOWN_SYNC_SCOPES, "sync scope");
}
