#include <stddef.h>

#define RETURN_NULL_OTHERWISE(sth) if ((sth) != 42) {                \
        return NULL;                              \
    }

int *foo(int *status) {
  int st = *status;
  RETURN_NULL_OTHERWISE(st);
  return status;
}

int main(int x) {
  int *result = foo(&x);
  return *result;
}

// REQUIRES: z3
// RUN: %clang %s -emit-llvm -c -g -O0 -Xclang -disable-O0-optnone -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --use-guided-search=error --debug-localization --location-accuracy --analysis-reproduce=%s.sarif %t1.bc 2>&1 | FileCheck %s

// CHECK: Industry/MacroLocalization.c:15:10-15:10 # 3 ~> %9 in function main;
// CHECK: Industry/MacroLocalization.c:9:3-9:3 33 # 3 ~> %12 in function foo;
// CHECK: Target: %7 in function main
// CHECK: Target: %12 in function foo
// CHECK: Target bc7beead5f814c9deac02e8e9e6eef08: error in %9 in function main
// CHECK: KLEE: WARNING: 100.00% NullPointerException True Positive at trace bc7beead5f814c9deac02e8e9e6eef08
