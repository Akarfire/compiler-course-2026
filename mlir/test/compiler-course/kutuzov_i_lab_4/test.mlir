// RUN: mlir-opt -load-pass-plugin=%mlir_lib_dir/kutuzov_i_lab_4_MLIR%shlibext --pass-pipeline="builtin.module(kutuzov_loop_fuse)" %s | FileCheck %s

// CHECK-LABEL: @should_fuse
// CHECK: scf.for
// CHECK: arith.constant
// CHECK: memref.store
// CHECK: arith.constant
// CHECK: memref.store
// CHECK-NOT: scf.for
// CHECK: return

func.func @should_fuse(%A: memref<10xf32>) {
  %c0 = arith.constant 0 : index
  %c10 = arith.constant 10 : index
  %c1 = arith.constant 1 : index
  
  scf.for %i = %c0 to %c10 step %c1 {
    %val = arith.constant 0.0 : f32
    memref.store %val, %A[%i] : memref<10xf32>
    scf.yield
  }
  
  scf.for %j = %c0 to %c10 step %c1 {
    %val = arith.constant 1.0 : f32
    memref.store %val, %A[%j] : memref<10xf32>
    scf.yield
  }
  
  return
}


// CHECK-LABEL: @should_not_fuse_dependency
// CHECK: scf.for
// CHECK: scf.for
// CHECK: return

func.func @should_not_fuse_dependency(%A: memref<10xf32>, %B: memref<10xindex>) {
  %c0 = arith.constant 0 : index
  %c10 = arith.constant 10 : index
  %c1 = arith.constant 1 : index
  
  %result = scf.for %i = %c0 to %c10 step %c1 iter_args(%sum = %c0) -> (index) {
    %val = arith.constant 1.0 : f32
    memref.store %val, %A[%i] : memref<10xf32>
    scf.yield %sum : index
  }
  
  // This loop uses %result - should prevent fusion
  scf.for %j = %c0 to %c10 step %c1 {
    memref.store %result, %B[%j] : memref<10xindex>
    scf.yield
  }
  
  return
}


// CHECK-LABEL: @should_not_fuse_different_bounds
// CHECK: scf.for
// CHECK: scf.for
// CHECK: return

func.func @should_not_fuse_different_bounds() {
  %c0 = arith.constant 0 : index
  %c10 = arith.constant 10 : index
  %c20 = arith.constant 20 : index
  %c1 = arith.constant 1 : index
  
  scf.for %i = %c0 to %c10 step %c1 {
    %val = arith.constant 0.0 : f32
    scf.yield
  }
  
  scf.for %j = %c0 to %c20 step %c1 {
    %val = arith.constant 1.0 : f32
    scf.yield
  }
  
  return
}



// CHECK-LABEL: @fuse_three_loops
// CHECK: scf.for
// CHECK-NOT: scf.for
// CHECK: return

func.func @fuse_three_loops(%A: memref<10xf32>) {
  %c0 = arith.constant 0 : index
  %c10 = arith.constant 10 : index
  %c1 = arith.constant 1 : index

  scf.for %i = %c0 to %c10 step %c1 {
    %val = arith.constant 0.0 : f32
    memref.store %val, %A[%i] : memref<10xf32>
    scf.yield
  }

  scf.for %j = %c0 to %c10 step %c1 {
    %val = arith.constant 1.0 : f32
    memref.store %val, %A[%j] : memref<10xf32>
    scf.yield
  }

  scf.for %k = %c0 to %c10 step %c1 {
    %val = arith.constant 2.0 : f32
    memref.store %val, %A[%k] : memref<10xf32>
    scf.yield
  }

  return
}



// CHECK-LABEL: @fuse_with_conditional
// CHECK: scf.for
// CHECK-NOT: scf.for
// CHECK: return

func.func @fuse_with_conditional(%A: memref<10xf32>, %B: memref<10xf32>, %cond: i1) {
  %c0 = arith.constant 0 : index
  %c10 = arith.constant 10 : index
  %c1 = arith.constant 1 : index
  %val_true = arith.constant 5.0 : f32
  %val_false = arith.constant -5.0 : f32

  scf.for %i = %c0 to %c10 step %c1 {
    %chosen = scf.if %cond -> (f32) {
      scf.yield %val_true : f32
    } else {
      scf.yield %val_false : f32
    }
    memref.store %chosen, %A[%i] : memref<10xf32>
    scf.yield
  }

  scf.for %j = %c0 to %c10 step %c1 {
    %val = arith.constant 1.0 : f32
    memref.store %val, %B[%j] : memref<10xf32>
    scf.yield
  }

  return
}



// CHECK-LABEL: @fuse_iter_args
// CHECK: scf.for
// CHECK: iter_args(
// CHECK: %{{.*}} = arith.addf
// CHECK: %{{.*}} = arith.mulf
// CHECK-NOT: scf.for
// CHECK: return

func.func @fuse_iter_args(%A: memref<10xf32>, %B: memref<10xf32>) {
  %c0 = arith.constant 0 : index
  %c10 = arith.constant 10 : index
  %c1 = arith.constant 1 : index
  %init_sum = arith.constant 0.0 : f32
  %init_prod = arith.constant 1.0 : f32

  %sum = scf.for %i = %c0 to %c10 step %c1 iter_args(%s = %init_sum) -> (f32) {
    %val = memref.load %A[%i] : memref<10xf32>
    %new_s = arith.addf %s, %val : f32
    scf.yield %new_s : f32
  }

  %prod = scf.for %j = %c0 to %c10 step %c1 iter_args(%p = %init_prod) -> (f32) {
    %val = memref.load %B[%j] : memref<10xf32>
    %new_p = arith.mulf %p, %val : f32
    scf.yield %new_p : f32
  }

  return
}