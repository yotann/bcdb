; REQUIRES: llvm7
; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f0     | FileCheck --check-prefix=DEFINE %s

; DEFINE: define i32 @0() !dbg !4
define i32 @f0() !dbg !10 {
  ; DEFINE: ret i32 1, !dbg !8
  ret i32 1, !dbg !14
}

define i32 @f1() !dbg !15 {
  ret i32 2, !dbg !16
}

; DEFINE: !llvm.dbg.cu = !{!0}
!llvm.dbg.cu = !{!0, !3}
!llvm.ident = !{!5, !5}
; DEFINE: !llvm.module.flags = !{!3}
!llvm.module.flags = !{!6, !7, !8, !9}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang version 5.0.2 (tags/RELEASE_502/final)", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !2)
!1 = !DIFile(filename: "f1.c", directory: "/tmp")
!2 = !{}
!3 = distinct !DICompileUnit(language: DW_LANG_C99, file: !4, producer: "clang version 5.0.2 (tags/RELEASE_502/final)", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !2)
!4 = !DIFile(filename: "f2.c", directory: "/tmp")
!5 = !{!"clang version 5.0.2 (tags/RELEASE_502/final)"}
!6 = !{i32 2, !"Dwarf Version", i32 4}
!7 = !{i32 2, !"Debug Info Version", i32 3}
!8 = !{i32 1, !"wchar_size", i32 4}
!9 = !{i32 7, !"PIC Level", i32 2}
!10 = distinct !DISubprogram(name: "f0", scope: !1, file: !1, line: 1, type: !11, isLocal: false, isDefinition: true, scopeLine: 1, isOptimized: true, unit: !0, retainedNodes: !2)
!11 = !DISubroutineType(types: !12)
!12 = !{!13}
!13 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!14 = !DILocation(line: 2, column: 3, scope: !10)
!15 = distinct !DISubprogram(name: "f1", scope: !4, file: !4, line: 1, type: !11, isLocal: false, isDefinition: true, scopeLine: 1, isOptimized: true, unit: !3, retainedNodes: !2)
!16 = !DILocation(line: 2, column: 3, scope: !15)
