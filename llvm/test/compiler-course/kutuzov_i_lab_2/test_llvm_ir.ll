; RUN: opt -load-pass-plugin %llvmshlibdir/kutuzov_i_lab_2_LLVM_IR%pluginext \
; RUN: -passes=kutuzov_load_store_pass -S %s | FileCheck %s

define void @dead_store_elimination(ptr %ptr) {
; CHECK-LABEL: @dead_store_elimination(
; CHECK-NEXT:    %val = load i32, ptr %ptr
; CHECK-NEXT:    %result = add i32 %val, 5
; CHECK-NEXT:    ret void
  %val = load i32, ptr %ptr
  store i32 %val, ptr %ptr
  %result = add i32 %val, 5
  ret void
}

define void @constant_store_optimization(ptr %ptr) {
; CHECK-LABEL: @constant_store_optimization(
; CHECK-NEXT:    store i32 99, ptr %ptr
; CHECK-NEXT:    ret void
  store i32 99, ptr %ptr
  store i32 99, ptr %ptr
  ret void
}

define double @forward_double_value(ptr %ptr, double %x) {
; CHECK-LABEL: @forward_double_value(
; CHECK-NEXT:    store double %x, ptr %ptr
; CHECK-NEXT:    %sum = fadd double %x, 2.5
; CHECK-NEXT:    ret double %sum
  store double %x, ptr %ptr
  %loaded = load double, ptr %ptr
  %sum = fadd double %loaded, 2.5
  ret double %sum
}

define i32 @multiple_loads_optimization(ptr %ptr) {
; CHECK-LABEL: @multiple_loads_optimization(
; CHECK-NEXT:    %first = load i32, ptr %ptr
; CHECK-NEXT:    %tmp = sub i32 %first, 5
; CHECK-NEXT:    %res = mul i32 %first, %first
; CHECK-NEXT:    ret i32 %res
  %first = load i32, ptr %ptr
  %tmp = sub i32 %first, 5
  %second = load i32, ptr %ptr
  %res = mul i32 %first, %second
  ret i32 %res
}

define void @three_stores_elimination(ptr %ptr) {
; CHECK-LABEL: @three_stores_elimination(
; CHECK-NOT:     store i32 10
; CHECK-NOT:     store i32 20
; CHECK:         store i32 30, ptr %ptr
  store i32 10, ptr %ptr
  store i32 20, ptr %ptr
  store i32 30, ptr %ptr
  ret void
}

define i32 @different_pointers_no_alias(ptr %a, ptr %b) {
; CHECK-LABEL: @different_pointers_no_alias(
; CHECK:         %x = load i32, ptr %a
; CHECK-NEXT:    store i32 50, ptr %b
; CHECK-NEXT:    %result = add i32 %x, %x
  %x = load i32, ptr %a
  store i32 50, ptr %b
  %y = load i32, ptr %a
  %result = add i32 %x, %y
  ret i32 %result
}

declare void @external_func()
define i32 @function_call_barrier(ptr %ptr) {
; CHECK-LABEL: @function_call_barrier(
; CHECK:         store i32 42, ptr %ptr
; CHECK-NEXT:    call void @external_func()
; CHECK-NEXT:    %reload = load i32, ptr %ptr
  store i32 42, ptr %ptr
  call void @external_func()
  %reload = load i32, ptr %ptr
  ret i32 %reload
}

define i32 @volatile_operations(ptr %ptr) {
; CHECK-LABEL: @volatile_operations(
; CHECK:         store volatile i32 100, ptr %ptr
; CHECK:         %a = load volatile i32, ptr %ptr
; CHECK:         store atomic i32 200, ptr %ptr seq_cst, align 4
; CHECK:         %b = load atomic i32, ptr %ptr seq_cst, align 4
  store volatile i32 100, ptr %ptr
  %a = load volatile i32, ptr %ptr
  store atomic i32 200, ptr %ptr seq_cst, align 4
  %b = load atomic i32, ptr %ptr seq_cst, align 4
  %sum = add i32 %a, %b
  ret i32 %sum
}