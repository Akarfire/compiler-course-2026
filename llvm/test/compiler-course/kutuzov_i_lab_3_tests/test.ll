; RUN: opt -mtriple x86_64-unknown-linux-gnu -load-pass-plugin=%llvmshlibdir/kutuzov_i_lab_3_BACKEND%shlibext \
; RUN:   -passes=kutuzov_inline-x86 -S %s | FileCheck %s


define i32 @simple_func(i32 %x) {
  %y = add i32 %x, 1
  ret i32 %y
}

define void @void_func(i32 %x) {
  %y = add i32 %x, 1
  ret void
}

define i32 @test_simple(i32 %a) {
; CHECK-LABEL: define i32 @test_simple(
; CHECK-NOT:   call i32 @simple_func
; CHECK:       add i32 %a, 1
; CHECK:       ret
  %res = call i32 @simple_func(i32 %a)
  ret i32 %res
}

define void @test_void(i32 %a) {
; CHECK-LABEL: define void @test_void(
; CHECK-NOT:   call void @void_func
; CHECK:       add i32 %a, 1
; CHECK:       ret void
  call void @void_func(i32 %a)
  ret void
}


define i32 @big_func(i32 %x) {
  %1 = add i32 %x, 1
  %2 = add i32 %1, 1
  %3 = add i32 %2, 1
  %4 = add i32 %3, 1
  %5 = add i32 %4, 1
  %6 = add i32 %5, 1
  %7 = add i32 %6, 1
  %8 = add i32 %7, 1
  %9 = add i32 %8, 1
  %10 = add i32 %9, 1
  %11 = add i32 %10, 1
  %12 = add i32 %11, 1
  %13 = add i32 %12, 1
  %14 = add i32 %13, 1
  %15 = add i32 %14, 1
  %16 = add i32 %15, 1
  ret i32 %16
}

define i32 @test_big(i32 %a) {
; CHECK-LABEL: define i32 @test_big(
; CHECK:       call i32 @big_func
; CHECK:       ret
  %r = call i32 @big_func(i32 %a)
  ret i32 %r
}



define i32 @recursion(i32 %x) {
  %r = call i32 @recursion(i32 %x)
  ret i32 %r
}

define i32 @test_recursion(i32 %v) {
; CHECK-LABEL: define i32 @test_recursion(
; Three levels of inlining are done, then a final call remains.
; CHECK:       call i32 @recursion
; CHECK-NOT:   call i32 @recursion
; CHECK:       ret
  %ret = call i32 @recursion(i32 %v)
  ret i32 %ret
}



define i32 @debug_func(i32 %x) {
  %1 = add i32 %x, 1
  %2 = add i32 %1, 1
  %3 = add i32 %2, 1
  %4 = add i32 %3, 1
  %5 = add i32 %4, 1
  %6 = add i32 %5, 1
  %7 = add i32 %6, 1
  %8 = add i32 %7, 1
  %9 = add i32 %8, 1
  %10 = add i32 %9, 1
  %11 = add i32 %10, 1
  %12 = add i32 %11, 1
  %13 = add i32 %12, 1
  %14 = add i32 %13, 1
  %15 = add i32 %14, 1
  call void @llvm.dbg.value(metadata i32 %15, metadata !3, metadata !DIExpression()), !dbg !4
  ret i32 %15
}

define i32 @test_debug(i32 %a) {
; CHECK-LABEL: define i32 @test_debug(
; CHECK-NOT:   call i32 @debug_func
; CHECK:       add i32 %a, 1
; CHECK:       ret
  %res = call i32 @debug_func(i32 %a)
  ret i32 %res
}



define i32 @inline_me(i32 %x) {
  %y = mul i32 %x, 2
  ret i32 %y
}

define i32 @test_multiple_calls(i32 %a, i32 %b) {
; CHECK-LABEL: define i32 @test_multiple_calls(
; CHECK-NOT:   call i32 @inline_me
; CHECK:       mul i32 %a, 2
; CHECK:       mul i32 %b, 2
; CHECK:       ret
  %r1 = call i32 @inline_me(i32 %a)
  %r2 = call i32 @inline_me(i32 %b)
  %sum = add i32 %r1, %r2
  ret i32 %sum
}



declare void @llvm.dbg.value(metadata, metadata, metadata) #0
attributes #0 = { nounwind readnone speculatable willreturn }

!llvm.module.flags = !{!0}
!0 = !{i32 2, !"Debug Info Version", i32 3}
!llvm.dbg.cu = !{!1}
!1 = distinct !DICompileUnit(language: DW_LANG_C99, file: !2, producer: "clang", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !{}, splitDebugInlining: false)
!2 = !DIFile(filename: "test.c", directory: "/")
!3 = !DILocalVariable(name: "x", scope: !4, file: !2, line: 1, type: !5)
!4 = distinct !DISubprogram(name: "debug_func", scope: !2, file: !2, line: 1, type: !5, unit: !1)
!5 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)